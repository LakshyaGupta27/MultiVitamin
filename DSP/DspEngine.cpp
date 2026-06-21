#include "DspEngine.h"

DspEngine::DspEngine() {
    for (int i = 0; i < NUM_NODES; ++i) {
        nodes[i] = FxNode::create(FxNode::Type::Delay);
        baseWeights[i].store(1.0f / NUM_NODES);
        effectiveWeights[i].store(1.0f / NUM_NODES);
    }
    // Give each node a different default type
    nodes[1]->param1.store(0.5f); // will be set properly after prepare
    static const FxNode::Type defaults[NUM_NODES] = {
        FxNode::Type::Delay, FxNode::Type::Reverb, FxNode::Type::Filter,
        FxNode::Type::Granular, FxNode::Type::Distortion
    };
    for (int i = 0; i < NUM_NODES; ++i)
        nodes[i] = FxNode::create(defaults[i]);
}

void DspEngine::prepare(double sr, int maxBlock, int numCh) {
    sampleRate  = sr;
    blockSize   = maxBlock;
    numChannels = numCh;

    dryBuffer.setSize(numCh, maxBlock);

    gainComp.fill(1.0f);
    gainSmoothCoeff = (float)std::exp(-1.0 / (sr * 0.2));

    juce::SpinLock::ScopedLockType lock(nodeLock);
    for (int n = 0; n < NUM_NODES; ++n) {
        nodeBuffers[n].setSize(numCh, maxBlock);
        nodes[n]->prepare(sr, maxBlock, numCh);
    }
    for (auto& ls : lfoStates)
        ls.lfo.sampleRate = sr;

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
    if (paramIndex == 0) nodes[nodeIndex]->param1.store(value);
    else                 nodes[nodeIndex]->param2.store(value);
}

float DspEngine::getNodeParam(int nodeIndex, int paramIndex) const {
    jassert(nodeIndex >= 0 && nodeIndex < NUM_NODES);
    return paramIndex == 0 ? nodes[nodeIndex]->param1.load()
                           : nodes[nodeIndex]->param2.load();
}

void DspEngine::applyLfos(std::array<float, NUM_NODES>& weights, int numSamples) {
    // Phase 1 — load base params from atomics; apply inter-LFO modulation
    // from the PREVIOUS block's outputs (one-block latency, non-recursive).
    for (auto& ls : lfoStates) {
        ls.lfo.shape = (Lfo::Shape)ls.shape.load(std::memory_order_relaxed);
        ls.lfo.rate  = ls.rate.load(std::memory_order_relaxed);
        ls.lfo.depth = ls.depth.load(std::memory_order_relaxed);
    }
    for (int i = 0; i < MAX_LFOS; ++i) {
        auto& src = lfoStates[i];
        if (!src.active.load(std::memory_order_relaxed)) continue;
        const int tType = src.targetType.load(std::memory_order_relaxed);
        const int tNode = src.targetNode.load(std::memory_order_relaxed);
        if (tType == 1 && tNode >= 0 && tNode < MAX_LFOS) {
            // Modulate target LFO's rate with this LFO's previous-block output
            auto& tgt = lfoStates[tNode];
            tgt.lfo.rate = juce::jlimit(0.01f, 20.0f, tgt.lfo.rate + src.lfo.prevOutput);
        } else if (tType == 2 && tNode >= 0 && tNode < MAX_LFOS) {
            auto& tgt = lfoStates[tNode];
            tgt.lfo.depth = juce::jlimit(0.0f, 1.0f, tgt.lfo.depth + src.lfo.prevOutput);
        }
    }

    // Store effective rate/depth so the UI can display modulated values.
    for (auto& ls : lfoStates) {
        ls.effectiveRate .store(ls.lfo.rate,  std::memory_order_relaxed);
        ls.effectiveDepth.store(ls.lfo.depth, std::memory_order_relaxed);
    }

    // Phase 2 — tick every active LFO; store output for next block's Phase 1.
    for (auto& ls : lfoStates) {
        if (!ls.active.load(std::memory_order_relaxed)) {
            ls.lfo.prevOutput = 0.0f;
            continue;
        }
        ls.lfo.prevOutput = ls.lfo.process(numSamples);
    }

    // Phase 3 — apply LFO→node modulations using this block's fresh outputs.
    for (int i = 0; i < MAX_LFOS; ++i) {
        auto& ls = lfoStates[i];
        if (!ls.active.load(std::memory_order_relaxed)) continue;
        if (ls.targetType.load(std::memory_order_relaxed) != 0) continue;

        const float mod   = ls.lfo.prevOutput;
        const int   node  = ls.targetNode.load(std::memory_order_relaxed);
        const int   param = ls.targetParam.load(std::memory_order_relaxed);
        if (node < 0 || node >= NUM_NODES) continue;

        if (param == 0) {
            weights[node] = juce::jlimit(0.0f, 1.0f, weights[node] + mod);
        } else if (param == 1) {
            auto& p = nodes[node]->param1;
            p.store(juce::jlimit(0.0f, 1.0f, p.load() + mod));
        } else {
            auto& p = nodes[node]->param2;
            p.store(juce::jlimit(0.0f, 1.0f, p.load() + mod));
        }
    }
}

void DspEngine::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    if (!prepared) { buffer.clear(); return; }

    juce::SpinLock::ScopedTryLockType tryLock(nodeLock);
    if (!tryLock.isLocked()) { buffer.clear(); return; }

    // Compute effective weights (base + LFO)
    std::array<float, NUM_NODES> ew;
    for (int i = 0; i < NUM_NODES; ++i)
        ew[i] = juce::jmax(0.0f, baseWeights[i].load(std::memory_order_relaxed));

    applyLfos(ew, numSamples);

    float total = 0.0f;
    for (float w : ew) total += w;
    if (total < 1e-6f) { buffer.clear(); return; }
    for (float& w : ew) w /= total;
    for (int i = 0; i < NUM_NODES; ++i)
        effectiveWeights[i].store(ew[i]);

    // Save dry copy
    for (int ch = 0; ch < numChannels && ch < buffer.getNumChannels(); ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // Process each node and accumulate wet output
    juce::AudioBuffer<float> wetOut(numChannels, numSamples);
    wetOut.clear();

    for (int n = 0; n < NUM_NODES; ++n) {
        if (ew[n] < 1e-4f) continue;
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

        for (int ch = 0; ch < numChannels && ch < nodeBuffers[n].getNumChannels(); ++ch)
            nodeBuffers[n].applyGain(ch, 0, numSamples, gainComp[n]);

        for (int ch = 0; ch < numChannels && ch < nodeBuffers[n].getNumChannels(); ++ch)
            wetOut.addFrom(ch, 0, nodeBuffers[n], ch, 0, numSamples, ew[n]);
    }

    // Blend dry/wet
    const float wet = globalDryWet.load();
    const float dry = 1.0f - wet;
    for (int ch = 0; ch < numChannels && ch < buffer.getNumChannels(); ++ch) {
        auto* out    = buffer.getWritePointer(ch);
        const auto* dryPtr = dryBuffer.getReadPointer(ch);
        const auto* wetPtr = wetOut.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            out[i] = dry * dryPtr[i] + wet * wetPtr[i];
    }
}
