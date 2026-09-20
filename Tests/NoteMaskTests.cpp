#include "engine/NoteMask.h"

#include <juce_core/juce_core.h>

/** La maschera a 128 bit condivisa da VoiceManager e SynthEngine, e il suo packing per il JSON. */
struct NoteMaskTests final : public juce::UnitTest
{
    NoteMaskTests() : juce::UnitTest ("NoteMask", "engine") {}

    void runTest() override
    {
        beginTest ("set/test/clear su entrambe le parole");
        {
            engine::NoteMask m;
            expect (m.empty());
            m.set (0, true);
            m.set (63, true);
            m.set (64, true);
            m.set (127, true);
            expect (m.test (0) && m.test (63) && m.test (64) && m.test (127));
            expect (! m.test (1) && ! m.test (65));
            expectEquals ((juce::int64) m.lo, (juce::int64) ((juce::uint64 (1) << 63) | 1));
            expectEquals ((juce::int64) m.hi, (juce::int64) ((juce::uint64 (1) << 63) | 1));

            m.set (63, false);
            expect (! m.test (63) && m.test (0));
            m.clear();
            expect (m.empty());
        }

        beginTest ("una nota fuori da 0..127 non ha un bit");
        {
            engine::NoteMask m;
            m.set (-1, true);
            m.set (128, true);
            expect (m.empty());
            expect (! m.test (-1) && ! m.test (128));
        }

        beginTest ("splitNoteMask: quattro parole a 32 bit, bassa per prima, nello stesso ordine di noteMaskOf in backend.ts");
        {
            const auto words = engine::splitNoteMask (0x0000000100000002ull, 0x8000000000000004ull);
            expectEquals ((juce::int64) words[0], (juce::int64) 2);
            expectEquals ((juce::int64) words[1], (juce::int64) 1);
            expectEquals ((juce::int64) words[2], (juce::int64) 4);
            expectEquals ((juce::int64) words[3], (juce::int64) 0x80000000u);
        }
    }
};

static NoteMaskTests noteMaskTests;
