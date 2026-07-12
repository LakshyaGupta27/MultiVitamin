#pragma once
#include "FxNode.h"
#include "../Modulation/ModMatrix.h"
#include <array>
#include <atomic>
#include <memory>

class DspEngine {
public:
    static constexpr int NUM_NODES = 5;

    DspEngine();

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void process(juce::AudioBuffer<float>& buffer, int numSamples,
                 const TransportInfo& transport, const ModTriggerList& triggers);
    void releaseResources();

    // Called from UI thread — sets base mix weights from pentagon cursor
    void setBaseWeights(const std::array<float, NUM_NODES>& w);

    // Replace a node's FX type (UI thread). Handles prepare internally.
    void setNodeType(int nodeIndex, FxNode::Type type);
    FxNode::Type getNodeType(int nodeIndex) const;

    // Per-node effective weights after modulation (for UI visualization)
    std::array<std::atomic<float>, NUM_NODES> effectiveWeights;

    // Base (unmodulated) node params — what the UI edits and presets store.
    // Modulation offsets from the matrix are applied on top per block.
    void setNodeParam(int nodeIndex, int paramIndex, float value);
    float getNodeParam(int nodeIndex, int paramIndex) const;

    // Global dry/wet base value (modulatable via ModDest::DryWet)
    std::atomic<float> globalDryWet { 0.7f };

    // Cursor position as fraction of surface bounds — persisted in state
    std::atomic<float> cursorRelX { 0.5f };
    std::atomic<float> cursorRelY { 0.5f };

    // The modulation system: sources, routings, destination offsets.
    ModMatrix modMatrix;

private:
    std::array<std::unique_ptr<FxNode>, NUM_NODES> nodes;
    std::array<std::atomic<float>, NUM_NODES> baseWeights;
    std::array<std::array<std::atomic<float>, 2>, NUM_NODES> paramBase;

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
