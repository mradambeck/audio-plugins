#include <juce_core/juce_core.h>

#include <cstdio>

// Headless entry point for Strike's UnitTest suite (the four DSP-seam classes - Excitation,
// Loop Filter, Delay Tuning/StringLine - plus the composed Voice) - a small standalone console
// app rather than a hook inside the plugin itself, so it can be run on its own without building/
// loading a full AU/VST3/Standalone. Matches every other plugin's TestRunner.cpp in this catalog.
int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.setPassesAreLogged(true);
    runner.runAllTests();

    int totalPasses = 0;
    int totalFailures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* result = runner.getResult(i);
        totalPasses += result->passes;
        totalFailures += result->failures;

        for (const auto& message : result->messages)
            std::fprintf(stderr, "[%s / %s] %s\n",
                         result->unitTestName.toRawUTF8(), result->subcategoryName.toRawUTF8(), message.toRawUTF8());
    }

    std::printf("\n%d passed, %d failed\n", totalPasses, totalFailures);

    return totalFailures == 0 ? 0 : 1;
}
