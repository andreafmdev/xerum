#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace engine
{
inline constexpr bool isMidiNote (int note) noexcept { return note >= 0 && note <= 127; }

/** Il bit di una nota dentro la parola che la contiene (0..63 o 64..127). */
inline constexpr juce::uint64 noteBit (int note) noexcept { return juce::uint64 (1) << (note % 64); }

/**
 * Un bit per nota MIDI: 0..63 in `lo`, 64..127 in `hi`.
 *
 * Due parole di bit e non una lista: l'insieme e' senza ordine per costruzione e una maschera
 * costa un confronto per nota invece di una scansione. La usano VoiceManager (i note-off
 * differiti dal pedale) e SynthEngine (i tasti che il pedale tiene accesi sul keybed): sono due
 * insiemi diversi con la stessa forma, e la forma sta qui una volta sola. Niente atomico: chi ha
 * bisogno di pubblicare un mask a un altro thread lo fa parola per parola (vedi
 * SynthEngine::getActiveNotesLo/Hi).
 *
 * Una nota fuori da 0..127 non ha un bit: `test` risponde false e `set` non fa niente, ed e'
 * anche cio' che tiene lo spostamento definito.
 */
struct NoteMask
{
    juce::uint64 lo { 0 };
    juce::uint64 hi { 0 };

    bool test (int note) const noexcept
    {
        return isMidiNote (note) && (wordFor (note) & noteBit (note)) != 0;
    }

    void set (int note, bool on) noexcept
    {
        if (! isMidiNote (note))
            return;

        auto& word = wordFor (note);
        const auto bit = noteBit (note);
        word = on ? (word | bit) : (word & ~bit);
    }

    void clear() noexcept { lo = 0; hi = 0; }
    bool empty() const noexcept { return lo == 0 && hi == 0; }

private:
    juce::uint64& wordFor (int note) noexcept { return note < 64 ? lo : hi; }
    juce::uint64 wordFor (int note) const noexcept { return note < 64 ? lo : hi; }
};

/**
 * Le quattro parole a 32 bit con cui un mask viaggia come JSON: un uint64 non entra esatto
 * nella mantissa di un double. Il lettore le ricompone in WebUI/src/juce/backend.ts
 * (noteMaskOf), nello stesso ordine: n0/n1 da `lo`, n2/n3 da `hi`, parola bassa per prima.
 */
inline std::array<juce::uint32, 4> splitNoteMask (juce::uint64 lo, juce::uint64 hi) noexcept
{
    return { (juce::uint32) (lo & 0xffffffffu), (juce::uint32) (lo >> 32),
             (juce::uint32) (hi & 0xffffffffu), (juce::uint32) (hi >> 32) };
}
} // namespace engine
