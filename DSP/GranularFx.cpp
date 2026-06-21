#include "GranularFx.h"

void GranularFx::prepare(double sr, int /*maxBlock*/, int numCh) {
    sampleRate  = sr;
    numChannels = numCh;
    const int ringSize = (int)(sr * 2.0); // 2 seconds
    ringBuffer.setSize(numCh, ringSize);
    ringBuffer.clear();
    writePos = 0;
    samplesSinceLastGrain = 0;
    for (auto& g : grains) g = Grain{};
}

void GranularFx::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const float grainSize01 = param1.load(std::memory_order_relaxed);
    const float scatter     = param2.load(std::memory_order_relaxed);

    // grain size: 10ms to 500ms
    const int grainSamples = juce::jmax(1, (int)((10.0f + grainSize01 * 490.0f) * 0.001f * (float)sampleRate));
    const int grainPeriod  = juce::jmax(1, grainSamples / 2);
    const int ringSize     = ringBuffer.getNumSamples();
    const int numCh        = juce::jmin(buffer.getNumChannels(), numChannels);

    for (int i = 0; i < numSamples; ++i) {
        // Write input to ring buffer
        for (int ch = 0; ch < numCh; ++ch)
            ringBuffer.getWritePointer(ch)[writePos] = buffer.getReadPointer(ch)[i];

        // Spawn grain if due
        if (samplesSinceLastGrain >= grainPeriod) {
            samplesSinceLastGrain = 0;
            for (auto& g : grains) {
                if (!g.active) {
                    g.length  = grainSamples;
                    g.age     = 0;
                    g.active  = true;
                    const int maxScatter = (int)(scatter * grainSamples * 4.0f);
                    const int offset = grainSamples + (maxScatter > 0 ? random.nextInt(maxScatter) : 0);
                    g.readPos = (writePos + ringSize - offset) % ringSize;
                    break;
                }
            }
        }
        ++samplesSinceLastGrain;

        // Sum active grains
        float sums[8] = {};
        float totalEnv = 0.0f;
        for (auto& g : grains) {
            if (!g.active) continue;
            // Triangular envelope
            const float t = (float)g.age / (float)g.length;
            const float env = 1.0f - std::abs(2.0f * t - 1.0f);
            for (int ch = 0; ch < numCh; ++ch)
                sums[ch] += ringBuffer.getReadPointer(ch)[g.readPos] * env;
            totalEnv += env;
            g.readPos = (g.readPos + 1) % ringSize;
            if (++g.age >= g.length) g.active = false;
        }

        const float norm = totalEnv > 0.001f ? 1.0f / totalEnv : 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer(ch)[i] = sums[ch] * norm;

        writePos = (writePos + 1) % ringSize;
    }
}

void GranularFx::reset() {
    ringBuffer.clear();
    writePos = 0;
    samplesSinceLastGrain = 0;
    for (auto& g : grains) g = Grain{};
}
