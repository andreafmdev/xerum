#include "dsp/WavetableOscillator.h"

#include <cmath>

namespace dsp
{
namespace
{
/**
 * Interpolazione di **Lagrange di grado 3** sui quattro campioni x[-1], x[0], x[1], x[2]
 * attorno alla posizione richiesta. Sostituisce la lineare a due punti, che era lo stadio
 * che dominava il pavimento di aliasing dello strumento: la lineare sbaglia come la
 * derivata seconda della tavola, questa come la quarta, e con una tavola liscia e'
 * tutt'altra pendenza — misurato: 14 dB piu' in basso nel caso peggiore (43.2 Hz, 512
 * armoniche in tavola) e altri ~11 dB per ogni dimezzamento delle armoniche, cioe' per ogni
 * ottava che si sale sulla tastiera.
 *
 * Sono gli stessi quattro punti che legge la Catmull-Rom di Vital. Lagrange misura meglio
 * perche' passa **per** i quattro campioni invece di imporre la continuita' della derivata:
 * qui la tavola e' gia' band-limited, la derivata non ha salti da lisciare e vincolarla
 * costa solo precisione. Grado 5 (6 punti) non si fa: cosi' com'e', l'interpolazione sta
 * gia' ~34 dB sotto il pavimento della piramide mipmap — l'alias peggiore sulla tastiera e'
 * -107 dB e a farlo e' la piramide, non lei — quindi scendere ancora non si sentirebbe e
 * costerebbe una volta e mezzo le letture.
 *
 * La formula e' quella di `InterpolatorLagrange3` di Signalsmith-Audio/dsp (`delay.h`, MIT),
 * scritta a mano invece che adottata: li' e' il caso particolare di un template a grado
 * arbitrario con l'albero dei prodotti valutato a compile time (tecnica di Franck), e a
 * grado fisso 3 quel macchinario si riduce alle quattro righe qui sotto. Vendorare l'header
 * per ottenerle avrebbe portato dentro un file da tenere allineato a monte.
 *
 * Sviluppata in forma di Horner: nessuna divisione, nessuna chiamata a libm, tre
 * moltiplicazioni per la valutazione. A `fraction` esattamente 0 i tre gradi si annullano e
 * resta c0 = x[0]: l'uscita a fase intera **e'** il campione, non una media dei vicini.
 *
 * `mask` vale `size - 1` e avvolge entrambi i lati senza un ramo: il frame e' ciclico e la
 * finestra a 4 punti esce da tutt'e due i bordi, non piu' solo dall'ultimo campione. Vale
 * perche' `size` e' una potenza di due — `parseXwt` rifiuta i blob che non lo sono, ed e'
 * l'unica porta d'ingresso di una tavola. Se un giorno non lo fosse piu', il masking
 * resterebbe comunque dentro il buffer (`i & (size - 1) <= size - 1`): si perderebbe la
 * ciclicita', non la sicurezza della memoria.
 */
float sampleAt (const float* frame, int size, int mask, int index, float fraction) noexcept
{
    // `index + size - 1` invece di `index - 1`: stesso risultato dopo la maschera, ma
    // l'argomento non diventa mai negativo e non c'e' da ragionare sul complemento a due.
    const float xm1 = frame[(index + size - 1) & mask];
    const float x0 = frame[index];
    const float x1 = frame[(index + 1) & mask];
    const float x2 = frame[(index + 2) & mask];

    const float c0 = x0;
    const float c1 = x1 - (1.0f / 3.0f) * xm1 - 0.5f * x0 - (1.0f / 6.0f) * x2;
    const float c2 = 0.5f * (xm1 + x1) - x0;
    const float c3 = (1.0f / 6.0f) * (x2 - xm1) + 0.5f * (x0 - x1);

    return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
}
} // namespace

float levelForFrequency (float frequencyHz, double sampleRate, int frameSize) noexcept
{
    // Confronti scritti in positivo apposta: un NaN li fa fallire tutti e finisce qui,
    // invece di propagarsi fino al cast a int in updateLevel() — che su un NaN sarebbe
    // undefined behaviour. Il vecchio ciclo while era immune per caso, questo no.
    if (! (frequencyHz > 0.0f) || ! (sampleRate > 0.0) || frameSize <= 1)
        return (float) MipTable::kMaxLevel;

    // Quante armoniche stanno sotto Nyquist a questa frequenza.
    const double maxHarmonics = sampleRate / (2.0 * (double) frequencyHz);

    // Il livello k conserva (frameSize >> k) / 2 armoniche, cioè (frameSize / 2) / 2^k:
    // ne bastano `t` per scendere da frameSize / 2 a maxHarmonics, e `ceil(t)` è il primo
    // livello intero che ci arriva. Si restituisce t + 1 e non ceil(t) perché la parte
    // frazionaria deve portare il peso del crossfade (vedi l'header): floor(t + 1) è
    // proprio ceil(t), e frac(t + 1) va da 0 a 1 mentre la frequenza attraversa l'ottava
    // in cui quel livello resta il primo sicuro. Al confine successivo il livello sicuro
    // diventa quello dopo e il peso riparte da 0 — cioè esattamente dal suono su cui si
    // era arrivati un istante prima: nessun salto.
    const double t = std::log2 ((double) (frameSize / 2) / maxHarmonics);

    return (float) juce::jlimit (0.0, (double) MipTable::kMaxLevel, t + 1.0);
}

void WavetableOscillator::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
    updateLevel();
}

void WavetableOscillator::reset() noexcept
{
    phase_ = 0.0;
}

void WavetableOscillator::resetToPhase (float normalisedPhase) noexcept
{
    // std::fmod e non un while: la fase arriva da chi calcola i/N, non da un loop audio, ma
    // un NaN qui manderebbe in undefined behaviour il cast a intero dentro getSample(). Il
    // confronto scritto in positivo lo intercetta, come in levelForFrequency.
    const auto wrapped = (double) normalisedPhase - std::floor ((double) normalisedPhase);
    phase_ = (wrapped >= 0.0 && wrapped < 1.0) ? wrapped : 0.0;
}

void WavetableOscillator::setTable (const MipTable* table) noexcept
{
    table_ = table;
    frameLo_ = frameHi_ = 0;
    frameMix_ = 0.0f;
    updateLevel();
}

void WavetableOscillator::setFrequencyHz (float hz) noexcept
{
    frequencyHz_ = hz;

    // L'avvolgimento per campione in getSample() somma o sottrae 1 una sola volta:
    // basta finché |phaseIncrement_| < 1. Con un incremento più grande (frequenza
    // sopra la sample rate, positiva o negativa) un solo passo non riporterebbe la
    // fase in [0, 1) e il cast a intero su una fase fuori range sarebbe undefined
    // behaviour. juce::jlimit è solo un confronto: niente libm.
    const double increment = (double) hz / sampleRate_;
    phaseIncrement_ = juce::jlimit (-1.0, 1.0, increment);
    updateLevel();
}

void WavetableOscillator::setFramePosition (float normalised) noexcept
{
    if (table_ == nullptr)
        return;

    const int lastFrame = table_->getNumFrames() - 1;
    const float position = juce::jlimit (0.0f, 1.0f, normalised) * (float) lastFrame;

    frameLo_ = (int) position;
    frameHi_ = frameLo_ < lastFrame ? frameLo_ + 1 : lastFrame;
    frameMix_ = position - (float) frameLo_;
}

void WavetableOscillator::updateLevel() noexcept
{
    if (table_ == nullptr)
    {
        levelLo_ = levelHi_ = 0;
        levelMix_ = 0.0f;
        return;
    }

    const auto level = levelForFrequency (frequencyHz_, sampleRate_, table_->getFrameSize());

    // levelForFrequency è già limitata a [0, kMaxLevel], quindi il troncamento sta in
    // range; jlimit è solo un paio di confronti e mette al riparo da un NaN che arrivasse
    // da una sample rate assurda.
    levelLo_ = juce::jlimit (0, MipTable::kMaxLevel, (int) level);
    levelHi_ = juce::jmin (levelLo_ + 1, MipTable::kMaxLevel);
    levelMix_ = juce::jlimit (0.0f, 1.0f, level - (float) levelLo_);
}

float WavetableOscillator::getSample() noexcept
{
    if (table_ == nullptr)
        return 0.0f;

    // Tutti i livelli sono lunghi frameSize: cambia il contenuto (armoniche), non la
    // lunghezza del buffer, quindi l'interpolazione lavora sempre su una tavola
    // largamente sovracampionata.
    const int size = table_->getFrameSize();
    const int mask = size - 1;
    const double position = phase_ * (double) size;
    const int index = juce::jlimit (0, size - 1, (int) position);
    const auto fraction = (float) (position - (double) index);

    // Morph fra i due frame, fatto una volta per livello; poi crossfade fra i due livelli.
    // L'ordine non conta (è un'interpolazione bilineare), conta che siano 4 interpolazioni
    // da 4 tap l'una: 16 letture di tavola per campione, il doppio della lineare.
    const auto atLevel = [this, size, mask, index, fraction] (int level)
    {
        const float lo = sampleAt (table_->samples (frameLo_, level), size, mask, index, fraction);
        const float hi = sampleAt (table_->samples (frameHi_, level), size, mask, index, fraction);
        return lo + (hi - lo) * frameMix_;
    };

    const float bright = atLevel (levelLo_);
    const float dark = levelHi_ != levelLo_ ? atLevel (levelHi_) : bright;

    // |phaseIncrement_| <= 1 (garantito in setFrequencyHz): un solo passo in ciascuna
    // direzione riporta sempre phase_ in [0, 1), che è ciò che rende sicuro il cast
    // sopra a ogni chiamata successiva.
    phase_ += phaseIncrement_;
    if (phase_ >= 1.0)
        phase_ -= 1.0;
    else if (phase_ < 0.0)
        phase_ += 1.0;

    return bright + (dark - bright) * levelMix_;
}
} // namespace dsp
