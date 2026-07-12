#pragma once
#include <JuceHeader.h>
#include "ModTypes.h"
#include "XLfo.h"
#include "EnvelopeGen.h"
#include <array>
#include <atomic>

// ── ModMatrix ────────────────────────────────────────────────────────────────
// Owns every modulation source (8 XLFOs + 4 envelope generators) and a fixed
// pool of routings. There is no explicit "matrix page" in the UI — routings
// are created by dragging a modulator handle onto any mod-aware control
// (including other modulators' rate/amount knobs) and edited on the control
// itself. This class is the single authority either way.
//
// Threading: routing fields and modulator params are atomics written by the
// message thread; processBlock() runs on the audio thread with zero
// allocations and no locks. Audio-rate destinations get per-sample stereo
// offset buffers; meta destinations (LFO rate/amount) are applied per block
// from the previous block's source values, which keeps modulator→modulator
// chains non-recursive.
class ModMatrix {
public:
    struct Routing {
        std::atomic<bool>  active   { false };
        std::atomic<int>   source   { 0 };
        std::atomic<int>   dest     { 0 };
        std::atomic<float> amount   { 0.25f };  // -1..1
        std::atomic<bool>  bipolar  { true };   // map source bipolar vs unipolar
        std::atomic<bool>  invert   { false };
        std::atomic<float> smoothMs { 10.0f };  // per-routing offset smoothing
    };

    ModMatrix();

    // ── Audio thread ─────────────────────────────────────────────────────────
    void prepare(double sampleRate, int maxBlockSize);
    // `input` is the plugin's dry input for this block; envelope followers track
    // its loudness. Pass before the buffer is overwritten with output.
    void processBlock(const juce::AudioBuffer<float>& input,
                      const TransportInfo& transport,
                      const ModTriggerList& triggers, int numSamples);

    // Per-sample offset buffer for an audio-rate destination, or nullptr when
    // nothing routes there this block (caller then uses the base value alone).
    const float* getDestOffsets(int dest, int channel) const {
        jassert(ModDest::isAudioRate(dest));
        return destInUse[(size_t)dest] ? destBuffers[(size_t)dest].getReadPointer(channel)
                                       : nullptr;
    }

    // ── Message thread: routing management ───────────────────────────────────
    // Returns routing index, reusing an existing src→dest routing if present;
    // -1 if the pool is exhausted.
    int  addRouting(int source, int dest, float amount = 0.25f);
    void removeRouting(int index);
    int  findRouting(int source, int dest) const;             // -1 if none
    int  routingsForDest(int dest, std::array<int, ModConst::MAX_ROUTINGS>& out) const;
    bool destHasRoutings(int dest) const;
    float destModRange(int dest) const;   // sum of |amount|, for ring extents

    Routing& routing(int i)             { return routings[(size_t)i]; }
    const Routing& routing(int i) const { return routings[(size_t)i]; }

    // ── Sources ──────────────────────────────────────────────────────────────
    std::array<XLfoParams, ModConst::NUM_XLFOS> lfoParams;
    std::array<EnvParams,  ModConst::NUM_ENVS>  envParams;

    static juce::String sourceName(int src);
    static bool sourceIsBipolar(int src) { return ModSrc::isLfo(src); }

    // Live per-destination state for UI rings (written each audio block)
    std::array<std::atomic<float>, ModDest::NumAudioRate> uiDestOffset {};
    // Live source values, for drag handles / list indicators
    std::array<std::atomic<float>, ModConst::NUM_SOURCES> uiSourceValue {};

    // ── Serialization (message thread) ───────────────────────────────────────
    void toXml(juce::XmlElement& root) const;
    void fromXml(const juce::XmlElement& root);

private:
    std::array<Routing, ModConst::MAX_ROUTINGS> routings;

    std::array<XLfo,        ModConst::NUM_XLFOS> lfoEngines;
    std::array<EnvelopeGen, ModConst::NUM_ENVS>  envEngines;

    // Rendered source signals (stereo; XLFOs and envelopes duplicate L→R)
    std::array<juce::AudioBuffer<float>, ModConst::NUM_SOURCES>   srcBuffers;
    juce::AudioBuffer<float> monoInputBuf;   // input summed to mono, for followers
    // Accumulated offsets per audio-rate destination
    std::array<juce::AudioBuffer<float>, ModDest::NumAudioRate>   destBuffers;
    std::array<bool, ModDest::NumAudioRate>                       destInUse {};

    // Per-routing smoothing state (stereo)
    std::array<std::array<float, 2>, ModConst::MAX_ROUTINGS> smoothState {};

    // Block-end source values from the previous block, for meta modulation
    std::array<float, ModConst::NUM_SOURCES> prevSourceValue {};

    double sampleRate = 44100.0;
    int    maxBlock   = 0;
};
