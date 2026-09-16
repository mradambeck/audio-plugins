#include <juce_core/juce_core.h>

#include <cstdio>

// Headless entry point for ConvBase's UnitTest suite - the shared convolution engine's tests, run
// through this harness plugin's synthetic IRs. A standalone console app rather than a hook inside
// the plugin, so CI can run it without building or loading an AU/VST3/Standalone. Matches every
// other plugin's <Name>Tests target in this catalog.
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
