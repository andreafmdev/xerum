#include <juce_core/juce_core.h>

#include <iostream>

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
            failures += result->failures;

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TEST FAILURES") << std::endl;
    return failures == 0 ? 0 : 1;
}
