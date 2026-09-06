#pragma once

// Maps a zone's output destination index to where its voice actually gets summed. v1 declares a
// single main stereo bus (see PluginProcessor's isBusesLayoutSupported), so every destination
// collapses onto it - adding real aux outs later is a change to THIS mapping and to the
// constructor's bus declaration, not to any voice code, which only ever knows about destination
// indices (see concrete-sampler-plugin-plan.md's Architecture #1, "architect for aux outs").
//
// The plan's transparency requirement ("with every zone on destination 0, output must null
// against a build that renders voices directly into the main buffer") is satisfied by this class
// having no behavior beyond returning 0 for every destination today - see
// ConcreteBusRouterTests.cpp, which checks exactly that across a range of destination values.
class ConcreteBusRouter
{
public:
    // Returns the channel offset into the AudioBuffer<float> passed to processBlock() that a
    // voice rendering to `destination` should write into. v1: every destination maps to the main
    // bus (channel offset 0).
    int getChannelOffsetForDestination(int /*destination*/) const noexcept
    {
        return 0;
    }
};
