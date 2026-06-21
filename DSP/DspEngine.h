#pragma once
#include "FxNode.h"
#include "Lfo.h"
#include <array>
#include <atomic>
#include <memory>

class DspEngine {
public:
    static constexpr int NUM_NODES = 5;
    static constexpr int MAX_LFOS  = 8;

    struct LfoState {
        std::atomic<int>   shape       { 0 };    // Lfo::Shape as int
        std::atomic<float> rate        { 0.5f };
        std::atomic<float> depth       { 0.3f };
        std::atomic<int>   targetType  { 0 };    // 0=node, 1=lfo_rate, 2=lfo_depth
        std::atomic<int>   targetNode  { 0 };    // node 0-4  OR  lfo index 0-7
        std::atomic<int>   targetParam { 0 };    // 0=mix, 1=param1, 2=param2 (only for targetType==0)
        std::atomic<bool>  active       { false };
        // Written by audio thread each block; read by UI for live display.
        std::atomic<float> effectiveRate  { 0.5f };
        std::atomic<float> effectiveDepth { 0.3f };
        Lfo lfo; // phase + prevOutput are audio-thread only
    };

    DspEngine();

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void process(juce::AudioBuffer<float>& buffer, int numSamples);
    void releaseResources();

    // Called from UI thread — sets base mix weights from pentagon cursor
    void setBaseWeights(const std::array<float, NUM_NODES>& w);

    // Replace a node's FX type (UI thread). Handles prepare internally.
    void setNodeType(int nodeIndex, FxNode::Type type);
    FxNode::Type getNodeType(int nodeIndex) const;

    // Per-node effective weights after LFO modulation (for UI visualization)
    std::array<std::atomic<float>, NUM_NODES> effectiveWeights;

    void setNodeParam(int nodeIndex, int paramIndex, float value);
    float getNodeParam(int nodeIndex, int paramIndex) const;

    // Global dry/wet
    std::atomic<float> globalDryWet { 0.7f };

    // Cursor position as fraction of surface bounds — persisted in state
    std::atomic<float> cursorRelX { 0.5f };
    std::atomic<float> cursorRelY { 0.5f };

    // LFO states — modified from UI, read from audio thread
    std::array<LfoState, MAX_LFOS> lfoStates;

private:
    void applyLfos(std::array<float, NUM_NODES>& weights, int numSamples);

    std::array<std::unique_ptr<FxNode>, NUM_NODES> nodes;
    std::array<std::atomic<float>, NUM_NODES> baseWeights;

    juce::AudioBuffer<float> dryBuffer;
    std::array<juce::AudioBuffer<float>, NUM_NODES> nodeBuffers;

    double sampleRate  = 44100.0;
    int    blockSize   = 512;
    int    numChannels = 2;
    bool   prepared    = false;

    std::array<float, NUM_NODES> gainComp {};
    float gainSmoothCoeff = 0.0f;

    juce::SpinLock nodeLock;
};
