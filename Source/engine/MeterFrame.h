#pragma once
#include <juce_core/juce_core.h>

#include <atomic>

namespace engine
{
/** Scritto dal thread audio con store(relaxed), letto dal timer dell'editor.
    Solo atomics: nessun lock, nessuna dipendenza GUI. */
struct MeterFrame
{
    std::atomic<float> inPeak  { 0.0f };   // picco pre-master del blocco
    std::atomic<float> outPeak { 0.0f };   // picco post-master

    /**
     * I cinque livelli delle sorgenti del mod matrix — gli stessi nomi di engine::ModSource.
     *
     * Sono cio' che l'anello disegnato attorno a un knob modulato mostra: liveValue() in
     * WebUI/src/synth/mod.ts somma `depth x livello` al valore di base, quindi senza questi
     * numeri l'anello ha la profondita' giusta e il movimento sbagliato — su una route
     * env -> cutoff resterebbe fermo mentre il filtro si apre.
     *
     * Due regimi diversi, e la differenza sta nel lettore (bridge::MeterChannel, 30 Hz):
     *
     *  - `env`, `env2`, `vel` sono **picchi**, come inPeak/outPeak: il thread audio scrive
     *    store(max(load, valore)) e il lettore azzera con exchange(0). Un frame dura 33 ms e
     *    un attacco puo' durarne 5: pubblicare l'istantaneo vorrebbe dire che l'anello non
     *    vede quasi mai il punto piu' alto raggiunto da cio' che modula. Sono unipolari 0..1,
     *    quindi il massimo e' definito e ha il significato che serve.
     *
     *  - `lfo` e `mw` sono **istantanei**, e il lettore li legge con load() senza azzerarli.
     *    L'LFO e' bipolare -1..1: il massimo di |x| perderebbe il segno e quello con segno
     *    sarebbe il massimo del ramo positivo, cioe' un anello che non scende mai. Il mod
     *    wheel non e' un transitorio ma una **posizione**, mossa da una mano: 30 Hz la
     *    inseguono senza perderne niente, mentre azzerarla a ogni lettura la farebbe cadere
     *    a zero non appena il thread audio smette di girare, e la rotella e' ancora dov'e'.
     */
    std::atomic<float> lfo  { 0.0f };      // -1..1, istantaneo
    std::atomic<float> env  { 0.0f };      // 0..1, picco dall'ultima lettura
    std::atomic<float> env2 { 0.0f };      // 0..1, picco dall'ultima lettura
    std::atomic<float> vel  { 0.0f };      // 0..1, picco dall'ultima lettura
    std::atomic<float> mw   { 0.0f };      // 0..1, istantaneo

    std::atomic<int>   arpStep { 0 };      // 0..15, zero finché l'arp non esiste

    /** Un bit per nota MIDI: 0..63 in `notesLo`, 64..127 in `notesHi`. Istantanei come `lfo` e
        `mw` — il lettore fa load(), non exchange(0) — perche' una nota tenuta e' uno stato. */
    std::atomic<juce::uint64> notesLo { 0 };
    std::atomic<juce::uint64> notesHi { 0 };
};

/**
 * Alza `slot` fino a `value` se `value` e' piu' alto, lasciandolo dov'e' altrimenti.
 *
 * Non e' una compare-and-swap, ed e' corretto per la stessa ragione per cui lo era gia' per i
 * picchi audio: il thread audio e' l'**unico** scrittore, e l'unico lettore (il timer
 * dell'editor) fa solo exchange(0). Il peggio che puo' succedere e' che un exchange caschi fra
 * la load e la store qui sotto, e allora un picco resta nel frame un giro di timer in piu': 33
 * ms di ritardo su un'indicazione, non un dato perso ne' una lettura strappata.
 */
inline void storePeak (std::atomic<float>& slot, float value) noexcept
{
    const auto current = slot.load (std::memory_order_relaxed);

    if (value > current)
        slot.store (value, std::memory_order_relaxed);
}
} // namespace engine
