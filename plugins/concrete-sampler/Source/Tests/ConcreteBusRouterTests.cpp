#include "../ConcreteBusRouter.h"

#include <juce_core/juce_core.h>

// Direct check of concrete-sampler-plugin-plan.md's Architecture #1 transparency requirement:
// "with every zone on destination 0, output must null against a build that renders voices
// directly into the main buffer". The router has no state and no signal path of its own - it's a
// pure function from destination index to channel offset - so proving it returns exactly 0 for
// every destination v1 can produce (and for the destinations a future aux-out feature would add)
// is a direct proof of transparency, not an approximation of one.
class ConcreteBusRouterTests : public juce::UnitTest
{
public:
    ConcreteBusRouterTests() : juce::UnitTest("ConcreteBusRouter", "Concrete") {}

    void runTest() override
    {
        beginTest("Every destination collapses onto the main bus (channel offset 0) in v1");
        {
            ConcreteBusRouter router;
            for (int destination = 0; destination < 8; ++destination)
                expectEquals(router.getChannelOffsetForDestination(destination), 0,
                             "no destination should be routed anywhere but the main bus yet");
        }
    }
};

static ConcreteBusRouterTests concreteBusRouterTests;
