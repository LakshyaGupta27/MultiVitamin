#pragma once
#include <JuceHeader.h>
#include "ModTypes.h"
#include <atomic>

// ── Envelope Follower ───────────────────────────────────────────────────────
// An envelope FOLLOWER: tracks the loudness contour of the plugin's audio input
// (rectify + separate attack/release smoothing) and outputs a 0..1 control signal.
// No triggers, no gate logic — it just measures input level continuously.

struct EnvParams {
    std::atomic<bool>  active        { false };
    std::atomic<bool>  followerPeak  { true };   // peak |x| vs RMS (x²→√)
    std::atomic<float> attackSec     { 0.01f };  // fast attack smoothing
    std::atomic<float> releaseSec    { 0.3f };   // slow release smoothing
    std::atomic<float> followerGain  { 4.0f };   // input make-up gain, 1x..64x

    // Live feedback for the UI
    std::atomic<float> uiValue       { 0.0f };
};

class EnvelopeGen {
public:
    void prepare(double sr) { sampleRate = sr; reset(); }
    void reset() {
        followLevel = 0.0f; level = 0.0f;
    }

    // `monoInput` is the plugin input summed to mono.
    void renderBlock(EnvParams& p, const float* monoInput, int numSamples, float* out);

private:
    double sampleRate = 44100.0;
    float  followLevel = 0.0f;  // smoothed follower output (pre-gain)
    float  level = 0.0f;        // current output
};
