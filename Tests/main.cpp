#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <iostream>

int main (int argc, char** argv)
{
    // Un MessageManager per il thread principale: il processore (Timer, AsyncUpdater,
    // JUCE_ASSERT_MESSAGE_THREAD) e l'APVTS lo danno per scontato. Senza, ogni suite che li
    // tocca stampava una jassert da juce_Timer.cpp e proseguiva.
    juce::MessageManager::getInstance();

    int failures = 0;

    {
        juce::UnitTestRunner runner;
        runner.setAssertOnFailure (false);
        // `XerumTests dsp` lancia solo quella categoria: il ciclo rosso/verde su una suite non
        // deve pagare i due minuti dell'intera batteria.
        if (argc > 1)
            runner.runTestsInCategory (argv[1]);
        else
            runner.runAllTests();

        for (int i = 0; i < runner.getNumResults(); ++i)
            if (const auto* result = runner.getResult (i))
                failures += result->failures;
    }

    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TEST FAILURES") << std::endl;
    return failures == 0 ? 0 : 1;
}
