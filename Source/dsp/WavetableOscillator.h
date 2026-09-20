#pragma once

#include "dsp/MipTable.h"


namespace dsp
{
/**
 * Livello **frazionario** della piramide da suonare a questa frequenza.
 *
 * Vale `t + 1`, dove `t = log2((frameSize / 2) / maxHarmonics)` e
 * `maxHarmonics = sampleRate / (2 * frequencyHz)`: `ceil(t)` è il primo livello le cui
 * armoniche stanno tutte sotto Nyquist, e questa funzione restituisce quel livello con
 * il peso del crossfade verso il successivo già dentro la parte frazionaria. Chi la usa
 * legge la parte intera come livello da suonare e la parte frazionaria come peso del
 * livello sotto (più scuro):
 *
 *     lo = (int) livello;  mix = livello - lo;  campione = (1 - mix) * S(lo) + mix * S(lo + 1)
 *
 * Il risultato è sempre `>= t`, cioè non si legge mai un livello più brillante di quello
 * sicuro: l'aliasing non può peggiorare. Ed è continuo nella frequenza, quindi il numero
 * di armoniche che si sentono segue la legge 1/f senza scalini a ogni ottava — prima,
 * scegliendo `ceil(t)` secco, la brillantezza restava congelata per un'ottava e poi
 * crollava dell'11-18% di colpo (misurato sul centroide spettrale, nota per nota).
 *
 * Limitato a [0, MipTable::kMaxLevel]: sotto zero il livello 0 è già sovrabbondante,
 * sopra il massimo non c'è niente di più scuro da mescolare.
 */
float levelForFrequency (float frequencyHz, double sampleRate, int frameSize) noexcept;

/**
 * Oscillatore wavetable con tripla interpolazione: **Lagrange di grado 3** fra i campioni
 * dentro il frame, lineare fra i due frame adiacenti alla posizione, lineare fra i due
 * livelli adiacenti della piramide. La seconda produce il morph: senza, muovere Position
 * dà scatti. La terza rende continua la brillantezza lungo la tastiera: senza, il timbro
 * resta congelato per un'ottava e poi crolla di colpo (vedi levelForFrequency).
 *
 * La prima è quella che decide il pavimento di aliasing dello strumento, e per questo non
 * è lineare: fra due campioni della tavola la lineare sbaglia come la derivata seconda,
 * Lagrange-3 come la quarta (vedi `sampleAt` nel .cpp per la formula e la scelta del
 * grado). Le altre due interpolano fra segnali che cambiano a tasso di controllo, dove il
 * grado non conta.
 *
 * Costo: 16 letture di tavola per campione (2 frame × 2 livelli × 4 tap), nessuna
 * allocazione e nessuna chiamata a libm — log2 vive in setFrequencyHz/setTable. Il wrap
 * dentro il frame è una maschera, quindi nessun ramo condizionale per campione.
 */
class WavetableOscillator
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /**
     * Riparte da una fase scelta invece che da zero, 0..1 sul ciclo. Serve all'unison: N copie
     * che partono tutte a fase zero sono lo stesso identico segnale sommato N volte — N volte
     * piu' forte e senza un battimento finche' il detune non le separa — mentre distribuite
     * sul ciclo si sommano subito come sorgenti distinte.
     *
     * Il valore viene avvolto invece che limitato: i/N con i = N e' zero, non "l'ultimo punto
     * prima del giro", e un chiamante che sbaglia di un giro deve ottenere la stessa fase, non
     * un estremo.
     */
    void resetToPhase (float normalisedPhase) noexcept;

    /** Tavola attiva; nullptr significa silenzio, non crash. */
    void setTable (const MipTable* table) noexcept;

    void setFrequencyHz (float hz) noexcept;

    /** Posizione nel morph, 0..1 sull'intero set di frame. */
    void setFramePosition (float normalised) noexcept;

    /**
     * Warp = **sync**, 0..1: la fase letta dal frame corre `1 + 3·warp` volte per periodo della
     * nota, cioe' da uno a quattro cicli, con il wrap a ogni periodo — lo stesso mapping che
     * WaveDisplay disegna (`sampleWave` in WebUI/src/synth/curves.ts). Il livello mipmap si
     * sceglie sulla frequenza **efficace** `f · (1 + 3·warp)`: cio' che si legge resta
     * band-limited, e l'unico alias e' quello del wrap, che e' il suono del sync.
     *
     * Zero e' l'identita' esatta: `phase · 1.0` e `x - floor(x)` con x in [0, 1) restituiscono
     * lo stesso double, quindi l'uscita e' bit per bit quella di prima. Costo: una
     * moltiplicazione e un floor per campione, nessuna chiamata a libm (floor e' un'istruzione).
     */
    void setWarp (float amount01) noexcept;

    float getSample() noexcept;

private:
    void updateLevel() noexcept;

    double sampleRate_ { 44100.0 };
    double phase_ { 0.0 };
    double phaseIncrement_ { 0.0 };
    float frequencyHz_ { 0.0f };

    const MipTable* table_ { nullptr };
    // Coppia di livelli adiacenti e peso del crossfade: calcolati in updateLevel(), cioè
    // fuori dal loop audio. getSample() non deve chiamare log2 per campione.
    int levelLo_ { 0 };
    int levelHi_ { 0 };
    float levelMix_ { 0.0f };
    int frameLo_ { 0 };
    int frameHi_ { 0 };
    float frameMix_ { 0.0f };

    /** 1 + 3·warp: quante volte la fase letta corre per periodo. 1.0 esatto senza warp. */
    double warpRate_ { 1.0 };
};
} // namespace dsp
