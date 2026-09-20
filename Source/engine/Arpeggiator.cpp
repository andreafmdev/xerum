#include "engine/Arpeggiator.h"
#include "parameters/ParameterDenormalise.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine
{
namespace
{
/**
 * Quanti quarti dura un passo, per le quattro divisioni di `arpRate`.
 *
 * L'ordine e' quello di ARP_DIVS in WebUI/src/synth/mapping.ts e di Label::ArpRate in
 * Source/parameters/ParameterMapping.h — "1/32", "1/16", "1/8", "1/4" — e l'indice lo sceglie
 * params::arpDivisionIndex, la stessa funzione dell'etichetta, cosi' il knob non puo' mostrare
 * una divisione e suonarne un'altra.
 */
constexpr double kBeatsPerStep[] = { 0.125, 0.25, 0.5, 1.0 };

/** Tolleranza sull'arrotondamento al campione dei confini. Mezzo milionesimo di campione: sotto
    qualunque errore che l'aritmetica in doppia precisione possa accumulare su un blocco, e ben
    sopra la risoluzione che serve per non far scivolare un confine di un campione intero. */
constexpr double kBoundaryEpsilon = 1.0e-6;

/** Il primo campione intero >= `pos`. Negativo: zero. */
int sampleAtOrAfter (double pos) noexcept
{
    if (pos <= 0.0)
        return 0;

    return (int) std::ceil (pos - kBoundaryEpsilon);
}

/** Resto sempre non negativo: `n % m` in C++ non lo e' per n negativo, e qui `n` puo' esserlo
    (un host con pre-roll manda PPQ negativi). */
int positiveMod (std::int64_t n, int m) noexcept
{
    if (m <= 0)
        return 0;

    const auto r = (int) (n % (std::int64_t) m);
    return r < 0 ? r + m : r;
}

/** Divisione intera arrotondata verso il basso, anche per `n` negativo. */
std::int64_t floorDiv2 (std::int64_t n) noexcept
{
    return n >= 0 ? n / 2 : -(((-n) + 1) / 2);
}
} // namespace

double arpBeatsPerStep (float raw) noexcept
{
    return kBeatsPerStep[params::arpDivisionIndex (raw)];
}

void Arpeggiator::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

    // Tutta la memoria del buffer di uscita si chiede **qui**. Vedi il commento della classe per
    // il solo caso in cui process() puo' richiederne ancora.
    output_.ensureSize (kMidiReserveBytes);

    reset();
}

void Arpeggiator::reset() noexcept
{
    output_.clear();
    heldCount_ = 0;

    for (auto& s : sounding_)
        s.active = false;

    active_ = false;
    sustain_ = false;
    nextStepIndex_ = 0;
    lastFiredStep_ = INT64_MIN;
    nextStepPos_ = 0.0;
    currentStep_ = 0;
    randomState_ = 0x9e3779b9u;
    synced_ = false;
    blockStartPpq_ = 0.0;
}

std::uint32_t Arpeggiator::nextRandom() noexcept
{
    // xorshift32. Tre spostamenti e tre xor: nessuna allocazione, nessuno stato globale, nessun
    // std::random_device costruito sul thread audio (che e' cio' che fa Odin, e che puo' aprire
    // /dev/urandom dentro il callback audio).
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return randomState_;
}

void Arpeggiator::addHeld (int note, int channel) noexcept
{
    // Gia' tenuto: si aggiorna il canale e basta. Un doppio note-on senza note-off in mezzo non
    // deve poter far comparire due volte lo stesso tasto nella sequenza.
    for (int i = 0; i < heldCount_; ++i)
        if (held_[i].note == note)
        {
            held_[i].channel = channel;

            // Il dito ha ripreso il tasto: da adesso lo tiene lui. Senza questa riga, alzare il
            // pedale toglierebbe dalla sequenza una nota che qualcuno sta premendo.
            held_[i].latched = false;
            return;
        }

    if (heldCount_ >= kMaxHeldKeys)
        return;

    // Insertion sort: la lista resta ordinata per numero di nota, che e' cio' che rende Up e
    // Down due letture della stessa lista invece di due liste. std::sort qui sarebbe una
    // chiamata di libreria sul thread audio con complessita' non garantita; questo e' uno
    // spostamento di al piu' quindici elementi di otto byte.
    int i = heldCount_;

    while (i > 0 && held_[i - 1].note > note)
    {
        held_[i] = held_[i - 1];
        --i;
    }

    held_[i].note = note;
    held_[i].channel = channel;
    held_[i].latched = false;
    ++heldCount_;
}

void Arpeggiator::removeHeld (int note) noexcept
{
    for (int i = 0; i < heldCount_; ++i)
        if (held_[i].note == note)
        {
            // A pedale giu' il tasto resta nella sequenza: cambia solo chi lo tiene. Marcare
            // invece di togliere e' cio' che rende il latch una riga sola — la lista non si
            // accorcia, l'ordinamento per numero di nota non si tocca, e `heldCount_ > 0`
            // continua a tenere l'arp che gira senza che nessun altro ramo sappia del pedale.
            if (sustain_)
            {
                held_[i].latched = true;
                return;
            }

            for (int j = i; j + 1 < heldCount_; ++j)
                held_[j] = held_[j + 1];

            --heldCount_;
            return;
        }
}

void Arpeggiator::dropLatchedKeys() noexcept
{
    // Compattazione in un passo solo: si riscrive la lista tenendo i soli tasti ancora sotto un
    // dito. Chiamare removeHeld() in un ciclo sarebbe quadratico e, peggio, richiederebbe di
    // scorrere all'indietro una lista che si accorcia.
    int kept = 0;

    for (int i = 0; i < heldCount_; ++i)
        if (! held_[i].latched)
            held_[kept++] = held_[i];

    heldCount_ = kept;
}

void Arpeggiator::addSounding (int note, int channel, double offPos, int s) noexcept
{
    for (auto& v : sounding_)
        if (! v.active)
        {
            v.note = note;
            v.channel = channel;
            v.offPos = offPos;
            v.active = true;
            return;
        }

    // Slot esauriti: strutturalmente irraggiungibile con un gate <= 100 % e sedici slot. Se
    // succedesse, la nota nuova non deve prendere in silenzio il posto di una vecchia — quella
    // resterebbe appesa per sempre. Si sceglie la piu' vicina alla fine, le si manda il note-off
    // **adesso** e le si subentra: una nota accorciata invece di una appesa.
    jassertfalse;

    Sounding* oldest = &sounding_[0];

    for (auto& v : sounding_)
        if (v.offPos < oldest->offPos)
            oldest = &v;

    output_.addEvent (juce::MidiMessage::noteOff (oldest->channel, oldest->note), s);

    oldest->note = note;
    oldest->channel = channel;
    oldest->offPos = offPos;
    oldest->active = true;
}

void Arpeggiator::killSounding (int note, int s) noexcept
{
    for (auto& v : sounding_)
        if (v.active && v.note == note)
        {
            output_.addEvent (juce::MidiMessage::noteOff (v.channel, v.note), s);
            v.active = false;
        }
}

void Arpeggiator::flushSounding (int s) noexcept
{
    for (auto& v : sounding_)
        if (v.active)
        {
            output_.addEvent (juce::MidiMessage::noteOff (v.channel, v.note), s);
            v.active = false;
        }
}

int Arpeggiator::sequenceLength (ArpMode mode, int expanded) const noexcept
{
    if (expanded <= 0)
        return 1;

    // UpDn non ripete gli estremi: con tre note la sequenza e' 0 1 2 1, lunga 2N-2. Con una o due
    // note la specchiatura non aggiunge niente (0, oppure 0 1) e la lunghezza resta N.
    if (mode == ArpMode::upDown)
        return expanded <= 2 ? expanded : 2 * expanded - 2;

    return expanded;
}

int Arpeggiator::expandedIndexFor (ArpMode mode, int index, int expanded) noexcept
{
    switch (mode)
    {
        case ArpMode::down:
            return expanded - 1 - index;

        case ArpMode::upDown:
            return index < expanded ? index : 2 * expanded - 2 - index;

        case ArpMode::random:
            // Il contatore dei passi avanza comunque (serve alla griglia): qui si ignora `index`
            // e si pesca. Nessuno shuffle, nessuna allocazione.
            return (int) (nextRandom() % (std::uint32_t) expanded);

        case ArpMode::up:
            break;
    }

    return index;
}

double Arpeggiator::stepDurationSamples (std::int64_t n) const noexcept
{
    return positiveMod (n, 2) == 0 ? evenStepSamples_ : oddStepSamples_;
}

double Arpeggiator::positionOfStep (std::int64_t n) const noexcept
{
    const auto beat = (double) floorDiv2 (n) * pairBeats_ + (positiveMod (n, 2) == 0 ? 0.0 : longBeats_);

    return (beat - blockStartPpq_) * samplesPerBeat_;
}

void Arpeggiator::fireStep (int s, const ArpConfig& cfg) noexcept
{
    const auto n = nextStepIndex_;

    // I due indici: la griglia si avvolge su kArpSteps, la sequenza su tasti x ottave. Sono lo
    // stesso contatore letto con due moduli diversi — e' tutto il "doppio indice" di Odin.
    currentStep_ = positiveMod (n, kArpSteps);

    const auto level = cfg.steps != nullptr ? cfg.steps->steps[currentStep_] : 1.0f;

    if (level > 0.0f && heldCount_ > 0)
    {
        const auto expanded = heldCount_ * std::clamp (cfg.octaves, 1, 4);
        const auto index = positiveMod (n, sequenceLength (cfg.mode, expanded));
        const auto expandedIndex = std::clamp (expandedIndexFor (cfg.mode, index, expanded), 0, expanded - 1);

        const auto& key = held_[expandedIndex % heldCount_];
        const auto note = juce::jlimit (0, 127, key.note + 12 * (expandedIndex / heldCount_));

        // Il note-off **prima** del note-on, allo stesso campione: con gate pieno e nota ripetuta
        // l'ordine inverso spegnerebbe la voce appena avviata (e' il bug di Odin). juce::MidiBuffer
        // conserva l'ordine d'inserimento a parita' di timestamp, quindi qui basta l'ordine delle
        // due righe.
        killSounding (note, s);

        output_.addEvent (juce::MidiMessage::noteOn (key.channel, note, juce::jlimit (1.0f / 127.0f, 1.0f, level)), s);

        // Il gate e' una frazione della durata **di questo passo**, non del passo medio: con lo
        // swing i due passi della coppia durano diverso, e un gate al 50 % deve valere meta' di
        // ciascuno. Il minimo di un campione evita il note-off nello stesso istante del note-on.
        const auto gate = std::max (1.0, (double) std::clamp (cfg.gate01, 0.0f, 1.0f) * stepDurationSamples (n));

        // La base e' la posizione **ideale** del confine, non il campione arrotondato: cosi' il
        // gate non accumula l'errore di arrotondamento passo dopo passo. Il max con `s` copre il
        // solo caso in cui il confine cade prima dell'inizio del blocco (il transport e' saltato
        // dentro un passo gia' cominciato), dove il note-off deve comunque venire dopo il note-on.
        addSounding (note, key.channel, std::max (nextStepPos_, (double) s) + gate, s);
    }

    lastFiredStep_ = n;
    nextStepIndex_ = n + 1;

    // In sync il confine successivo si rilegge dalla griglia dell'host, non si somma: sommare
    // farebbe derivare l'arp dal PPQ di un campione ogni volta che la durata non e' intera.
    // Libero, invece, si somma sulla posizione **frazionaria** — e' cio' che tiene i confini a
    // 12000.0 e non a 11999, 23998, 35997.
    nextStepPos_ = synced_ ? positionOfStep (n + 1) : nextStepPos_ + stepDurationSamples (n);
}

void Arpeggiator::emitUntil (int limit, const ArpConfig& cfg) noexcept
{
    for (;;)
    {
        double bestPos = std::numeric_limits<double>::max();
        Sounding* gateOff = nullptr;

        for (auto& v : sounding_)
            if (v.active && v.offPos < bestPos)
            {
                bestPos = v.offPos;
                gateOff = &v;
            }

        // Confronto stretto: a parita' di campione vince il gate-off, che e' l'ordine giusto
        // quando il passo successivo ripete la stessa nota.
        if (nextStepPos_ < bestPos)
        {
            bestPos = nextStepPos_;
            gateOff = nullptr;
        }
        else if (gateOff == nullptr)
        {
            return;
        }

        const auto s = sampleAtOrAfter (bestPos);

        if (s >= limit)
            return;

        if (gateOff != nullptr)
        {
            output_.addEvent (juce::MidiMessage::noteOff (gateOff->channel, gateOff->note), s);
            gateOff->active = false;
        }
        else
        {
            fireStep (s, cfg);
        }
    }
}

void Arpeggiator::process (juce::MidiBuffer& midi, int numSamples, const ArpConfig& cfg,
                           const ArpTransport& transport) noexcept
{
    if (numSamples <= 0)
        return;

    if (! cfg.on)
    {
        // **La riga della non-regressione**: con l'arp spento e niente da spegnere si esce senza
        // toccare il buffer, quindi l'uscita e' bit per bit quella di prima che l'arp esistesse.
        if (! active_)
            return;

        // Spegnimento a caldo: i note-off delle note ancora in suono vanno scritti dentro il
        // buffer dell'host (non nel nostro: qui non c'e' nessuno swap), altrimenti restano
        // appese fino al prossimo allNotesOff.
        for (auto& v : sounding_)
            if (v.active)
            {
                midi.addEvent (juce::MidiMessage::noteOff (v.channel, v.note), 0);
                v.active = false;
            }

        heldCount_ = 0;
        sustain_ = false;
        active_ = false;
        nextStepIndex_ = 0;
        lastFiredStep_ = INT64_MIN;
        nextStepPos_ = 0.0;
        currentStep_ = 0;
        return;
    }

    active_ = true;

    const auto bpm = transport.bpm > 0.0 ? transport.bpm : 120.0;
    const auto beats = arpBeatsPerStep (cfg.rateRaw);
    const auto swing = (double) std::clamp (cfg.swing01, 0.0f, 1.0f);

    samplesPerBeat_ = 60.0 / bpm * sampleRate_;
    pairBeats_ = 2.0 * beats;
    longBeats_ = beats * (1.0 + 0.5 * swing);

    // T_pari = T0 (1 + s/2), T_dispari = T0 (1 - s/2): la somma della coppia resta 2 T0, quindi
    // lo swing sposta il secondo ottavo senza cambiare il tempo. Il minimo di un campione e' la
    // sola cosa che impedisce a un rate assurdo (BPM enormi) di far girare emitUntil a vuoto.
    evenStepSamples_ = std::max (1.0, longBeats_ * samplesPerBeat_);
    oddStepSamples_ = std::max (1.0, beats * (1.0 - 0.5 * swing) * samplesPerBeat_);

    const auto wasSynced = synced_;
    synced_ = transport.isPlaying;

    if (synced_)
    {
        // Riancoraggio sul PPQ, a ogni blocco: un salto del cursore, un loop o un cambio di tempo
        // riagganciano l'arp alla timeline invece di farlo derivare per il resto della sessione.
        blockStartPpq_ = transport.ppqPosition;

        const auto pairIndex = std::floor (blockStartPpq_ / pairBeats_);
        const auto withinPair = blockStartPpq_ - pairIndex * pairBeats_;
        const auto current = (std::int64_t) (2.0 * pairIndex) + (withinPair < longBeats_ ? 0 : 1);

        // Se il passo in cui cade l'inizio del blocco non e' ancora stato emesso — transport
        // appena partito, salto, loop — va emesso subito, al campione zero; altrimenti si punta
        // gia' al confine successivo. E' `lastFiredStep_` a impedire che il riancoraggio
        // riemetta lo stesso passo a ogni blocco.
        nextStepIndex_ = current != lastFiredStep_ ? current : current + 1;
        nextStepPos_ = positionOfStep (nextStepIndex_);
    }
    else if (wasSynced)
    {
        // Il transport si e' fermato: da qui in poi comanda il contatore libero, e il confine
        // successivo e' quello che il PPQ aveva gia' fissato. Nessun salto, nessun ritriggero.
        nextStepPos_ = std::max (0.0, nextStepPos_);
    }

    output_.clear();
    output_.ensureSize (kMidiReserveBytes);

    const auto lastSample = std::max (0, numSamples - 1);

    for (const auto metadata : midi)
    {
        const auto eventPos = juce::jlimit (0, lastSample, metadata.samplePosition);

        emitUntil (eventPos, cfg);

        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            // Primo tasto dopo il silenzio, con il transport fermo: la griglia riparte da qui,
            // cosi' la prima nota suona sotto il dito invece che al prossimo confine di una
            // griglia che nessuno vede. Con il transport in moto la griglia e' dell'host e non si
            // tocca.
            if (heldCount_ == 0 && ! synced_)
            {
                nextStepIndex_ = 0;
                lastFiredStep_ = INT64_MIN;
                nextStepPos_ = (double) eventPos;
            }

            addHeld (message.getNoteNumber(), message.getChannel());
        }
        else if (message.isNoteOff())
        {
            removeHeld (message.getNoteNumber());
        }
        else if (message.isSustainPedalOn() || message.isSustainPedalOff())
        {
            // Il latch. Il messaggio passa comunque a valle dal ramo dei controller piu' sotto
            // — vedi `sustain_` — quindi qui si interpreta soltanto.
            sustain_ = message.isSustainPedalOn();

            if (! sustain_)
                dropLatchedKeys();

            output_.addEvent (message, eventPos);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            // Passa intatto — le voci devono vederlo — ma prima si chiudono le note emesse: il
            // messaggio dell'host parla dei tasti, non dell'arpeggio che ci sta sopra.
            heldCount_ = 0;
            sustain_ = false;
            flushSounding (eventPos);
            output_.addEvent (message, eventPos);
        }
        else
        {
            // CC, pitch bend, aftertouch: l'arp non li interpreta e non li sposta.
            output_.addEvent (message, eventPos);
        }
    }

    emitUntil (numSamples, cfg);

    // Le posizioni sono relative all'inizio del blocco: qui si riportano all'inizio del prossimo.
    nextStepPos_ -= (double) numSamples;

    for (auto& v : sounding_)
        if (v.active)
            v.offPos -= (double) numSamples;

    // Lo scambio, non una copia: due puntatori, nessuna allocazione. Da qui in poi `midi` e'
    // l'arpeggio e `output_` e' il buffer che l'host ci aveva dato.
    midi.swapWith (output_);
}
} // namespace engine
