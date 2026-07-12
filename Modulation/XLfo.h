#pragma once
#include <JuceHeader.h>
#include "ModTypes.h"
#include "ModCurve.h"
#include <atomic>

// ── XLFO ─────────────────────────────────────────────────────────────────────
// FabFilter-style multi-shape LFO. Parameters live in XLfoParams (plain
// atomics, written by the UI / preset code, read by the audio thread); the
// XLfo engine below is audio-thread-only state.
//
// Rate, Amount, Phase and Pulse Width are all modulation destinations, so any
// modulator (including another XLFO) can drive them — see ModDest::lfo*().
//
// Every shape — including the random ones, which use hash noise keyed by the
// cycle index — is a pure function of position, so the editor can render the
// exact waveform the audio thread produces.

struct XLfoParams {
    enum Shape    { Sine, Triangle, SawUp, SawDown, Square, Pulse,
                    SampleHold, SmoothRandom, Custom, NumShapes };
    enum PlayMode { Loop, OneShot, PingPong, Reverse, NumPlayModes };
    enum TrigMode { Free, Transport, Midi, NumTrigModes };

    std::atomic<bool>  active      { false };
    std::atomic<int>   shape       { Sine };
    std::atomic<bool>  tempoSync   { false };
    std::atomic<float> rateHz      { 1.0f };              // free mode, 0.02..40
    std::atomic<int>   syncDiv     { DEFAULT_SYNC_DIV };  // synced mode
    std::atomic<int>   playMode    { Loop };
    std::atomic<int>   trigMode    { Free };
    std::atomic<float> phaseOffset { 0.0f };              // 0..1
    std::atomic<float> fadeInSec   { 0.0f };              // 0..10 s after (re)trigger
    std::atomic<float> smoothMs    { 5.0f };              // output lowpass, 0..500 ms
    std::atomic<float> pulseWidth  { 0.5f };              // Pulse shape, 0.05..0.95
    std::atomic<float> amount      { 1.0f };              // output level, 0..1 (meta-modulatable)

    ModCurve curve;   // Custom shape

    // Live feedback for the UI (written by the audio thread each block)
    std::atomic<float> uiPhase  { 0.0f };
    std::atomic<float> uiValue  { 0.0f };
    // Effective (post-meta-modulation) values for display
    std::atomic<float> uiEffRateHz     { 1.0f };
    std::atomic<float> uiEffAmount      { 1.0f };
    std::atomic<float> uiEffPhaseOffset { 0.0f };
    std::atomic<float> uiEffPulseWidth  { 0.5f };
};

// Per-block meta-modulation offsets applied to an XLFO's parameters.
struct XLfoMod {
    float rate  = 0.0f;   // -1..1 → ±3 octaves on rate
    float amount = 0.0f;  // added to amount
    float phase = 0.0f;   // added to phase offset
    float width = 0.0f;   // added to pulse width
};

class XLfo {
public:
    void prepare(double sr) { sampleRate = sr; reset(); }
    void reset() {
        pos = 0.0; fadeGain = 1.0f; smooth = 0.0f; started = false;
    }

    void renderBlock(XLfoParams& p, const TransportInfo& transport,
                     const ModTriggerList& triggers, const XLfoMod& mod,
                     int numSamples, float* out, int lfoIndex);

    // Pure waveform evaluation, shared with the UI. pos is unbounded playback
    // position in cycles; returns bipolar [-1, 1].
    static float evalAt(int shape, int playMode, double pos,
                        float phaseOffset, float pulseWidth,
                        const ModCurve* curve, int seed) noexcept;

private:
    static float evalShape(int shape, float phase01, float pulseWidth,
                           const ModCurve* curve, double cyclePos, int seed) noexcept;
    static float hashNoise(int64_t cycle, int seed) noexcept;   // deterministic, [-1,1]
    static float posToPhase(int playMode, double pos) noexcept; // mode mapping

    double sampleRate = 44100.0;
    double pos        = 0.0;    // unbounded position in cycles
    float  fadeGain   = 1.0f;
    float  smooth     = 0.0f;
    bool   started    = false;  // for one-shot Free mode: begins on first render
};
