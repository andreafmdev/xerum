#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <iostream>

int main()
{
    // Un MessageManager per il thread principale: il processore (Timer, AsyncUpdater,
    // JUCE_ASSERT_MESSAGE_THREAD) e l'APVTS lo danno per scontato. Senza, ogni suite che li
    // tocca stampava una jassert da juce_Timer.cpp e proseguiva.
    juce::MessageManager::getInstance();

    int failures = 0;

    {
        juce::UnitTestRunner runner;
        runner.setAssertOnFailure (false);
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
