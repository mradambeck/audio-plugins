#pragma once

// GradientDelayBuffer used to be a hand-rolled circular buffer defined here. It's now the shared
// wildjag::dsp::CircularDelayBuffer (../../common/dsp/CircularDelayBuffer.h) - same class, same
// behavior, promoted to common/ so other plugins (e.g. shields-reverb's tank lines, and
// intruder-gated-reverb) don't each reinvent it. This alias keeps every existing call site
// (GradientPitchShiftEngine, GradientDelayBufferTests) working unchanged.
#include "../../common/dsp/CircularDelayBuffer.h"

using GradientDelayBuffer = wildjag::dsp::CircularDelayBuffer;
