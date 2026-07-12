#include "DspEngine.h"

DspEngine::DspEngine() {
    static const FxNode::Type defaults[NUM_NODES] = {
        FxNode::Type::Delay, FxNode::Type::Reverb, FxNode::Type::Filter,
        FxNode::Type::Granular, FxNode::Type::Distortion
    };
    for (int i = 0; i < NUM_NODES; ++i) {
        nodes[i] = FxNode::create(defaults[i]);
        baseWeights[i].store(1.0f / NUM_NODES);
        effectiveWeights[i].store(1.0f / NUM_NODES);
        paramBase[i][0].store(0.5f);
        paramBase[i][1].store(0.5f);
    }
}

void DspEngine::prepare(double sr, int maxBlock, int numCh) {
    sampleRate  = sr;
    blockSize   = maxBlock;
    numChannels = numCh;

    dryBuffer.setSize(numCh, maxBlock);

    gainComp.fill(1.0f);
    gainSmoothCoeff = (float)std::exp(-1.0 / (sr * 0.2));

    modMatrix.prepare(sr, maxBlock);

    juce::SpinLock::ScopedLockType lock(nodeLock);
    for (int n = 0; n < NUM_NODES; ++n) {
        nodeBuffers[n].setSize(numCh, maxBlock);
        nodes[n]->prepare(sr, maxBlock, numCh);
    }

    prepared = true;
}

void DspEngine::releaseResources() {
    prepared = false;
}

void DspEngine::setBaseWeights(const std::array<float, NUM_NODES>& w) {
    for (int i = 0; i < NUM_NODES; ++i)
        baseWeights[i].store(w[i]);
}

void DspEngine::setNodeType(int nodeIndex, FxNode::Type type) {
    jassert(nodeIndex >= 0 && nodeIndex < NUM_NODES);
    auto node = FxNode::create(type);
    if (prepared) node->prepare(sampleRate, blockSize, numChannels);

    juce::SpinLock::ScopedLockType lock(nodeLock);
    nodes[nodeIndex] = std::move(node);
}

FxNode::Type DspEngine::getNodeType(int nodeIndex) const {
    return nodes[nodeIndex]->getType();
}

void DspEngine::setNodeParam(int nodeIndex, int paramIndex, float value) {
    jassert(nodeIndex >= 0 && nodeIndex < NUM_NODES);
    paramBase[nodeIndex][paramIndex == 0 ? 0 : 1].store(value);
}

float DspEngine::getNodeParam(int nodeIndex, int paramIndex) const {
    jassert(nodeIndex >= 0 && nodeIndex < NUM_NODES);
    return paramBase[nodeIndex][paramIndex == 0 ? 0 : 1].load();
}

void DspEngine::process(juce::AudioBuffer<float>& buffer, int numSamples,
                        const TransportInfo& transport, const ModTriggerList& triggers) {
    if (!prepared) { buffer.clear(); return; }

    juce::SpinLock::ScopedTryLockType tryLock(nodeLock);
    if (!tryLock.isLocked()) { buffer.clear(); return; }

    // ── Run the modulation system for this block ─────────────────────────────
    // buffer still holds the dry input here — envelope followers track it.
    modMatrix.processBlock(buffer, transport, triggers, numSamples);

    // Node params: base + matrix offset, applied per block (FX read them once).
    for (int n = 0; n < NUM_NODES; ++n) {
        for (int pi = 0; pi < 2; ++pi) {
            const int dest = pi == 0 ? ModDest::nodeP1(n) : ModDest::nodeP2(n);
            float v = paramBase[n][pi].load(std::memory_order_relaxed);
            if (const float* off = modMatrix.getDestOffsets(dest, 0))
                v += off[0];
            auto& target = pi == 0 ? nodes[n]->param1 : nodes[n]->param2;
            target.store(juce::jlimit(0.0f, 1.0f, v));
        }
    }

    // ── Decide which nodes run this block ────────────────────────────────────
    std::array<float, NUM_NODES> base;
    std::array<const float*, NUM_NODES> wOffL {}, wOffR {};
    std::array<bool, NUM_NODES> runNode {};
    bool anyRuns = false;

    for (int n = 0; n < NUM_NODES; ++n) {
        base[n]  = juce::jmax(0.0f, baseWeights[n].load(std::memory_order_relaxed));
        wOffL[n] = modMatrix.getDestOffsets(ModDest::nodeMix(n), 0);
        wOffR[n] = modMatrix.getDestOffsets(ModDest::nodeMix(n), 1);
        runNode[n] = base[n] > 1e-4f || wOffL[n] != nullptr;
        anyRuns |= runNode[n];
    }
    if (!anyRuns) { buffer.clear(); return; }

    // Save dry copy
    for (int ch = 0; ch < numChannels && ch < buffer.getNumChannels(); ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // ── Process nodes (with per-block RMS gain compensation) ─────────────────
    for (int n = 0; n < NUM_NODES; ++n) {
        if (!runNode[n]) continue;
        for (int ch = 0; ch < numChannels && ch < buffer.getNumChannels(); ++ch)
            nodeBuffers[n].copyFrom(ch, 0, buffer, ch, 0, numSamples);

        float inputRms = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            inputRms += nodeBuffers[n].getRMSLevel(ch, 0, numSamples);
        inputRms /= numChannels;

        nodes[n]->process(nodeBuffers[n], numSamples);

        if (inputRms > 1e-6f) {
            float outputRms = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                outputRms += nodeBuffers[n].getRMSLevel(ch, 0, numSamples);
            outputRms /= numChannels;

            if (outputRms > 1e-6f) {
                const float target = juce::jlimit(0.25f, 4.0f, inputRms / outputRms);
                gainComp[n] = gainComp[n] * gainSmoothCoeff + target * (1.0f - gainSmoothCoeff);
            }
        }
        nodeBuffers[n].applyGain(0, numSamples, gainComp[n]);
    }

    // ── Sample-accurate mix: weights and dry/wet vary per sample ─────────────
    const float dryWetBase = globalDryWet.load(std::memory_order_relaxed);
    const float* dwOff[2] = { modMatrix.getDestOffsets(ModDest::DryWet, 0),
                              modMatrix.getDestOffsets(ModDest::DryWet, 1) };

    for (int ch = 0; ch < numChannels && ch < buffer.getNumChannels(); ++ch) {
        auto*       out    = buffer.getWritePointer(ch);
        const auto* dryPtr = dryBuffer.getReadPointer(ch);
        const auto  offs   = ch == 0 ? wOffL : wOffR;

        for (int i = 0; i < numSamples; ++i) {
            // Per-sample normalized node weights
            float w[NUM_NODES];
            float total = 0.0f;
            for (int n = 0; n < NUM_NODES; ++n) {
                float v = base[n];
                if (offs[n] != nullptr) v += offs[n][i];
                w[n] = juce::jmax(0.0f, v);
                total += w[n];
            }

            float wetSample = 0.0f;
            if (total > 1e-6f)
                for (int n = 0; n < NUM_NODES; ++n)
                    if (runNode[n] && w[n] > 0.0f)
                        wetSample += nodeBuffers[n].getReadPointer(ch)[i] * (w[n] / total);

            float wet = dryWetBase;
            if (dwOff[ch] != nullptr) wet += dwOff[juce::jmin(ch, 1)][i];
            wet = juce::jlimit(0.0f, 1.0f, wet);

            out[i] = (1.0f - wet) * dryPtr[i] + wet * wetSample;
        }
    }

    // Publish effective weights (last sample, L) for the pentagon display
    {
        float w[NUM_NODES]; float total = 0.0f;
        const int last = numSamples - 1;
        for (int n = 0; n < NUM_NODES; ++n) {
            float v = base[n];
            if (wOffL[n] != nullptr) v += wOffL[n][last];
            w[n] = juce::jmax(0.0f, v); total += w[n];
        }
        for (int n = 0; n < NUM_NODES; ++n)
            effectiveWeights[n].store(total > 1e-6f ? w[n] / total : 0.0f,
                                      std::memory_order_relaxed);
    }
}
