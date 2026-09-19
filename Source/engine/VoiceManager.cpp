#include "engine/VoiceManager.h"

namespace engine
{
void VoiceManager::prepare (double sampleRate) noexcept
{
    for (auto& voice : voices_)
        voice.prepare (sampleRate);

    nextStartOrder_ = 1;
    held_.clear();
    monoVoiceIndex_ = -1;
    lastStartedNote_ = -1;
    soundedSinceLastNoteOn_ = false;
}

void VoiceManager::reset() noexcept
{
    for (auto& voice : voices_)
        voice.reset();

    nextStartOrder_ = 1;
    held_.clear();
    monoVoiceIndex_ = -1;
    lastStartedNote_ = -1;
    soundedSinceLastNoteOn_ = false;
}

SynthVoice* VoiceManager::findFreeVoice() noexcept
{
    for (auto& voice : voices_)
        if (! voice.isActive())
            return &voice;

    return nullptr;
}

SynthVoice* VoiceManager::findVoiceForNote (int midiNote) noexcept
{
    // Le voci in dissolvenza da furto sono escluse, e non e' un dettaglio: conservano il numero
    // di nota che avevano, quindi senza questo filtro una coda potrebbe essere ribattuta da
    // retrigger() (l'inviluppo ripartirebbe mentre la dissolvenza continua a scendere) o
    // fermata da un note-off destinato alla voce che l'ha sostituita.
    for (auto& voice : voices_)
        if (voice.isActive() && ! voice.isFading() && voice.getMidiNote() == midiNote)
            return &voice;

    return nullptr;
}

int VoiceManager::countActive() const noexcept
{
    int count = 0;

    for (const auto& voice : voices_)
        if (voice.isActive() && ! voice.isFading())
            ++count;

    return count;
}

int VoiceManager::countSounding() const noexcept
{
    int count = 0;

    for (const auto& voice : voices_)
        if (voice.isActive())
            ++count;

    return count;
}

const SynthVoice& VoiceManager::getVoice (int index) const noexcept
{
    const auto clamped = index >= 0 && index < poolSize ? index : 0;
    return voices_[static_cast<size_t> (clamped)];
}

SynthVoice* VoiceManager::chooseVictim() noexcept
{
    SynthVoice* released = nullptr;
    SynthVoice* oldest = nullptr;

    for (auto& voice : voices_)
    {
        if (! voice.isActive() || voice.isFading())
            continue;

        if (oldest == nullptr || voice.getStartOrder() < oldest->getStartOrder())
            oldest = &voice;

        if (! voice.isReleasing())
            continue;

        // A parita' di livello vince la piu' vecchia: il confronto stretto sul livello lascia
        // in piedi la prima trovata, e il secondo ramo la sostituisce solo se e' partita prima.
        // Serve davvero, e il caso non e' esotico: con sustain a zero un accordo rilasciato ha
        // tutte le voci esattamente a livello zero, e senza questo l'ordine del pool deciderebbe
        // al posto del criterio.
        if (released == nullptr
            || voice.getAmplitudeLevel() < released->getAmplitudeLevel()
            || (voice.getAmplitudeLevel() == released->getAmplitudeLevel()
                && voice.getStartOrder() < released->getStartOrder()))
            released = &voice;
    }

    return released != nullptr ? released : oldest;
}

SynthVoice* VoiceManager::reclaimQuietestFading() noexcept
{
    SynthVoice* quietest = nullptr;

    for (auto& voice : voices_)
    {
        if (! voice.isActive() || ! voice.isFading())
            continue;

        if (quietest == nullptr || voice.getAmplitudeLevel() < quietest->getAmplitudeLevel())
            quietest = &voice;
    }

    if (quietest != nullptr)
        quietest->kill();

    return quietest;
}

SynthVoice* VoiceManager::monoVoice() noexcept
{
    if (monoVoiceIndex_ < 0)
        return nullptr;

    auto& voice = voices_[(size_t) monoVoiceIndex_];

    // Una voce il cui inviluppo si e' spento non e' piu' "la voce del mono": la nota successiva
    // deve prendersi uno slot pulito, non ribattere un inviluppo gia' finito. Idem per una in
    // dissolvenza da furto — che in mono non dovrebbe mai capitare, visto che la polifonia e'
    // uno, ma il filtro costa un confronto e toglie di mezzo la domanda.
    if (! voice.isActive() || voice.isFading())
    {
        monoVoiceIndex_ = -1;
        return nullptr;
    }

    return &voice;
}

void VoiceManager::monoNoteOn (int midiNote, float velocity) noexcept
{
    // Prima di aggiungere: e' questo che distingue "un altro tasto era gia' giu'" da "questa e'
    // la prima nota della frase", e con la lista gia' aggiornata la domanda non sarebbe piu'
    // rispondibile. Ribattere un tasto gia' premuto lascia il conteggio dov'e', quindi una nota
    // ribattuta da sola non e' legato — e infatti ritriggera, in tutti e due i modi.
    const auto wasHolding = held_.count > 0;
    const auto glides = canGlideFromLastNote();

    held_.add (midiNote, velocity);

    auto* voice = monoVoice();

    if (voice == nullptr)
    {
        // Nessuna voce viva: la frase comincia adesso. Si passa dal percorso normale di
        // findFreeVoice/start, cosi' filtro, fase e inviluppi partono puliti come in Poly.
        voice = findFreeVoice();

        if (voice == nullptr)
            voice = reclaimQuietestFading();

        if (voice == nullptr)
            return;

        voice->start (midiNote, velocity, nextStartOrder_++);

        // Il glide c'e' anche qui, alle stesse condizioni di Poly: serve un tasto gia' premuto e
        // una nota precedente che abbia suonato. In pratica ci si arriva quando la voce si era
        // spenta del tutto mentre un altro tasto era ancora giu' — raro, ma non impossibile con
        // un release corto e un accordo tenuto.
        if (glides)
            voice->glideFrom ((float) lastStartedNote_);

        noteStarted (midiNote);
        monoVoiceIndex_ = (int) (voice - voices_.data());
        return;
    }

    // La voce c'e' gia': si cambia nota su quella, senza ucciderla e senza ricominciare da capo.
    // Vale anche — anzi, soprattutto — quando la nota precedente e' ancora in **release**: il
    // tasto era stato lasciato ma la coda sta ancora suonando, e riprendere quella voce invece
    // di aprirne una nuova e' cio' che fa Surge (`reclaimVoiceFor`). Il vantaggio non e' il
    // risparmio di uno slot: e' che inviluppo, fase e stato del filtro non vengono azzerati, e
    // che un glide lasciato a meta' riparte da dove era arrivato (lo fa glideToNote).
    //
    // `glides` falso non vuol dire "non cambiare nota": vuol dire cambiarla di colpo. Il caso
    // tipico e' proprio quello di sopra — la nota precedente era in release, nessun tasto era
    // giu' — dove un glide sarebbe un glissando fra due note che l'esecutore ha suonato
    // staccate. E' anche lo *slide* del 303: scivola solo quando le due note si sovrappongono.
    voice->glideToNote (midiNote, glides);

    // L'unica riga in cui Mono e Legato differiscono. In Legato si ritriggera soltanto se non
    // c'era gia' un tasto premuto: senza niente a cui legarsi, una nota nuova e' una nota nuova.
    if (mode_ == VoiceMode::mono || ! wasHolding)
        voice->retrigger (velocity);

    // In legato la velocity **non** si aggiorna, e non e' una dimenticanza: `vel` e' una
    // sorgente del mod matrix, e riscriverla a nota tenuta farebbe saltare di colpo tutto cio'
    // che ne dipende — lo stesso difetto di categoria del clic che retrigger() esiste per
    // evitare. Chi vuole che la seconda nota si senta piu' forte ha il ritrigger, cioe' Mono.

    noteStarted (midiNote);
}

void VoiceManager::monoNoteOff (int midiNote) noexcept
{
    // Prima di togliere: "la nota rilasciata era quella che si stava sentendo?". Se non lo era
    // — si e' lasciato un tasto di sotto tenendo quello di sopra — non deve succedere niente,
    // ed e' esattamente la condizione che rende un trillo tenendo una nota bassa una cosa
    // suonabile invece di un salto d'intonazione a ogni dito che si alza.
    const auto wasSounding = held_.last().note == midiNote;

    if (! held_.remove (midiNote))
        return; // un note-off per un tasto che non risulta premuto: niente da fare

    auto* voice = monoVoice();

    if (voice == nullptr)
        return;

    if (held_.count == 0)
    {
        voice->stop();
        return;
    }

    if (! wasSounding)
        return;

    // Si torna sull'ultimo tasto ancora premuto. E' il modello di Odin 2, e la sua meta' meno
    // ovvia: senza, lasciare la nota di sopra di un bicordo terrebbe la voce ferma li' in
    // release mentre un tasto e' ancora giu'.
    const auto fallback = held_.last();

    // Qui il glide non ha condizioni da verificare, e non e' un'eccezione alla regola di
    // canGlideFromLastNote(): e' la regola, gia' soddisfatta per costruzione. Il tasto su cui si
    // torna e' premuto adesso ed era premuto anche mentre la nota appena lasciata suonava — le
    // due note si sono sovrapposte per definizione, altrimenti non ci sarebbe niente a cui
    // tornare. E' il caso piu' legato che esista.
    voice->glideToNote (fallback.note, true);

    // In Mono il ritorno e' una nota nuova a tutti gli effetti e l'inviluppo riparte, con la
    // velocity con cui *quel* tasto era stato premuto — non con quella della nota appena
    // lasciata. In Legato no: il dito non ha mai smesso di premere, e l'inviluppo prosegue.
    if (mode_ == VoiceMode::mono)
        voice->retrigger (fallback.velocity);

    // `lastStartedNote_` si aggiorna, `soundedSinceLastNoteOn_` **no**: quest'ultimo conta i
    // campioni dall'ultimo note-on, e qui un note-on non c'e' stato — c'e' stato un note-off.
    // Azzerarlo direbbe che la nota su cui siamo tornati non ha ancora suonato, il che e' falso
    // (sta suonando da prima), e toglierebbe il glide alla nota successiva.
    lastStartedNote_ = fallback.note;
}

void VoiceManager::noteOn (int midiNote, float velocity) noexcept
{
    if (mode_ != VoiceMode::poly)
    {
        monoNoteOn (midiNote, velocity);
        return;
    }

    // La lista dei tasti si tiene aggiornata **anche in Poly**, dove nessuno la usa per
    // decidere quale voce suoni: serve solo a sapere se un tasto fosse gia' premuto quando
    // questa nota e' arrivata, cioe' a distinguere una nota legata da una isolata. Costa uno
    // scorrimento di un array di sedici interi per nota.
    const auto glides = canGlideFromLastNote();

    held_.add (midiNote, velocity);

    // Nota ribattuta mentre suona ancora: si riparte sulla stessa voce invece di ucciderla e
    // riprenderla da capo. kill() azzerava inviluppo, fase e filtro in un colpo, cioe' un
    // gradino da ampiezza piena a zero fra due campioni: il clic che si sentiva a ogni nota
    // ripetuta, anche legato.
    if (auto* existing = findVoiceForNote (midiNote))
    {
        existing->retrigger (velocity);
        noteStarted (midiNote);
        return;
    }

    // Il furto scatta sulla **polifonia** piena, non sul pool pieno: gli slot di margine non
    // sono voci in piu' da suonare, sono il posto dove le code dei furti finiscono di spegnersi.
    // Confondere i due numeri qui vorrebbe dire una polifonia di poolSize e nessun margine.
    if (countActive() >= maxVoices)
        if (auto* victim = chooseVictim())
            victim->beginStealFade();

    // E questa e' l'altra meta': la nota nuova prende una voce **davvero libera**, non quella
    // appena rubata. La voce rubata resta dov'e' a dissolvere, e nessuno la azzera fra due
    // campioni adiacenti.
    SynthVoice* voice = findFreeVoice();

    if (voice == nullptr)
        voice = reclaimQuietestFading();

    if (voice == nullptr)
        return; // irraggiungibile: countActive() <= maxVoices e il pool ne ha stealFadeMargin in piu'

    voice->start (midiNote, velocity, nextStartOrder_++);

    // Il glide in Poly: una voce nuova scivola dall'**ultima nota suonata**, non da una nota
    // sua. E' il portamento poly di Vital, ed e' l'unica definizione sensata quando le voci sono
    // piu' d'una — "la nota precedente di questa voce" sarebbe quella che occupava lo stesso
    // slot del pool, cioe' un dato di implementazione che l'esecutore non ha modo di prevedere.
    //
    // Ma solo fra note **legate**: e' `glides` a dirlo, ed e' quello che impedisce a un accordo
    // di partire smerdato e a una frase nuova dopo un silenzio di arrivarci strisciando. Vedi
    // canGlideFromLastNote().
    //
    // Con `glide` a zero glideFrom() ricade comunque nel bypass di beginGlide() e non tocca
    // niente: frequenza e cutoff restano quelli che start() ha appena scritto, bit per bit.
    if (glides)
        voice->glideFrom ((float) lastStartedNote_);

    noteStarted (midiNote);
}

void VoiceManager::noteOff (int midiNote) noexcept
{
    if (mode_ != VoiceMode::poly)
    {
        monoNoteOff (midiNote);
        return;
    }

    held_.remove (midiNote);

    if (auto* voice = findVoiceForNote (midiNote))
        voice->stop();
}

void VoiceManager::allNotesOff() noexcept
{
    held_.clear();

    for (auto& voice : voices_)
        voice.stop();
}

void VoiceManager::allSoundOff() noexcept
{
    held_.clear();
    monoVoiceIndex_ = -1;

    for (auto& voice : voices_)
        voice.kill();
}

void VoiceManager::render (float* outL, float* outR, int numSamples) noexcept
{
    // Un campione reso e' del tempo passato, ed e' cio' che separa due note legate dalle note di
    // uno stesso accordo: vedi canGlideFromLastNote(). Sta qui e non in setParams() perche'
    // setParams gira una volta per blocco anche quando poi non si rende niente.
    if (numSamples > 0)
        soundedSinceLastNoteOn_ = true;

    for (auto& voice : voices_)
        voice.render (outL, outR, numSamples);
}

void VoiceManager::setWavetable (const dsp::MipTable* table) noexcept
{
    for (auto& voice : voices_)
        voice.setWavetable (table);
}

void VoiceManager::setParams (const EngineParams& p) noexcept
{
    // Cambiare modo di voce **rilascia** quello che sta suonando, e non e' pigrizia: le tre
    // modalita' tengono la contabilita' delle note in posti diversi (in Poly la lista dei tasti
    // non viene aggiornata affatto, perche' nessuno la legge), quindi passare da una all'altra a
    // note premute lascerebbe dei note-off senza destinatario — cioe' note appese. Rilasciare
    // invece di uccidere: le code scendono con il loro release, nessun clic.
    //
    // Il confronto e' l'unica cosa che gira a modo fermo, e a modo fermo su Poly — il default —
    // non succede niente di niente.
    if (p.voiceMode != mode_)
    {
        mode_ = p.voiceMode;
        allNotesOff();
        monoVoiceIndex_ = -1;
        lastStartedNote_ = -1;
    }

    for (auto& voice : voices_)
        voice.setParams (p);
}

void VoiceManager::setGlobalLfoLevel (float level) noexcept
{
    for (auto& voice : voices_)
        voice.setGlobalLfoLevel (level);
}

VoiceManager::SourceLevels VoiceManager::getSourceLevels() const noexcept
{
    // Le code dei furti sono saltate: mostrano l'inviluppo di una nota che l'esecutore ha gia'
    // smesso di suonare, e per un meter sarebbe un valore che non corrisponde a niente di
    // premuto. "La prima voce attiva" resta la regola, solo fra quelle che contano.
    for (const auto& voice : voices_)
        if (voice.isActive() && ! voice.isFading())
            return { voice.getSourceLevel (ModSource::lfo),
                     voice.getSourceLevel (ModSource::env),
                     voice.getSourceLevel (ModSource::env2),
                     voice.getSourceLevel (ModSource::vel) };

    return {};
}
} // namespace engine
