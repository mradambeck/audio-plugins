#include "../ConcreteSampleSet.h"

#include <juce_core/juce_core.h>

// v1 only ever builds a one-zone ConcreteSampleSet, but lookup() is written as a real key/
// velocity-range search specifically so multi-zone kits/multisamples are a data change, not a
// code change (concrete-sampler-plugin-plan.md's Architecture #1) - these tests exercise that
// generality now, even though nothing in the shipped plugin creates a multi-zone set yet.
class ConcreteSampleSetTests : public juce::UnitTest
{
public:
    ConcreteSampleSetTests() : juce::UnitTest("ConcreteSampleSet", "Concrete") {}

    void runTest() override
    {
        beginTest("A single zone spanning the whole keyboard matches any note/velocity (v1's shape)");
        {
            ConcreteSampleSet set;
            ConcreteSampleZone zone;
            zone.keyLo = 0; zone.keyHi = 127;
            zone.velLo = 0; zone.velHi = 127;
            set.zones.push_back(zone);

            expectEquals(set.lookup(0, 0), 0);
            expectEquals(set.lookup(60, 100), 0);
            expectEquals(set.lookup(127, 127), 0);
        }

        beginTest("An empty zone list matches nothing");
        {
            ConcreteSampleSet set;
            expectEquals(set.lookup(60, 100), -1);
        }

        beginTest("A kit's per-note zones only match their own key range (multi-zone shape)");
        {
            ConcreteSampleSet set;
            ConcreteSampleZone kick;
            kick.keyLo = 36; kick.keyHi = 36;
            ConcreteSampleZone snare;
            snare.keyLo = 38; snare.keyHi = 38;
            set.zones.push_back(kick);
            set.zones.push_back(snare);

            expectEquals(set.lookup(36, 100), 0, "note 36 should hit the kick zone");
            expectEquals(set.lookup(38, 100), 1, "note 38 should hit the snare zone");
            expectEquals(set.lookup(40, 100), -1, "an unmapped note should match nothing");
        }

        beginTest("A multisampled instrument's key-range zones only match within their own range");
        {
            ConcreteSampleSet set;
            ConcreteSampleZone low;
            low.keyLo = 0; low.keyHi = 59;
            ConcreteSampleZone high;
            high.keyLo = 60; high.keyHi = 127;
            set.zones.push_back(low);
            set.zones.push_back(high);

            expectEquals(set.lookup(59, 100), 0);
            expectEquals(set.lookup(60, 100), 1);
        }

        beginTest("Velocity range is checked alongside key range");
        {
            ConcreteSampleSet set;
            ConcreteSampleZone soft;
            soft.keyLo = 60; soft.keyHi = 60; soft.velLo = 0; soft.velHi = 63;
            ConcreteSampleZone hard;
            hard.keyLo = 60; hard.keyHi = 60; hard.velLo = 64; hard.velHi = 127;
            set.zones.push_back(soft);
            set.zones.push_back(hard);

            expectEquals(set.lookup(60, 10), 0, "low velocity should hit the soft layer");
            expectEquals(set.lookup(60, 100), 1, "high velocity should hit the hard layer");
        }

        beginTest("lookup() returns the FIRST matching zone when ranges overlap");
        {
            ConcreteSampleSet set;
            ConcreteSampleZone first;
            first.keyLo = 0; first.keyHi = 127;
            ConcreteSampleZone second;
            second.keyLo = 0; second.keyHi = 127;
            set.zones.push_back(first);
            set.zones.push_back(second);

            expectEquals(set.lookup(60, 100), 0, "an overlapping later zone should never be picked over an earlier match");
        }
    }
};

static ConcreteSampleSetTests concreteSampleSetTests;
