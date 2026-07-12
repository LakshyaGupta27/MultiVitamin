#include "EnvelopeGen.h"

void EnvelopeGen::renderBlock(EnvParams& p, const float* monoInput,
                              int numSamples, float* out) {
    const float aSec = juce::jmax(1e-4f, p.attackSec .load(std::memory_order_relaxed));
    const float rSec = juce::jmax(1e-4f, p.releaseSec.load(std::memory_order_relaxed));
    const float gain = p.followerGain.load(std::memory_order_relaxed);
    const bool  peak = p.followerPeak.load(std::memory_order_relaxed);

    const float aCoeff = (float)std::exp(-1.0 / (sampleRate * (double)aSec));
    const float rCoeff = (float)std::exp(-1.0 / (sampleRate * (double)rSec));

    for (int i = 0; i < numSamples; ++i) {
        const float x      = monoInput != nullptr ? monoInput[i] : 0.0f;
        const float target = peak ? std::abs(x) : x * x;
        const float coeff  = target > followLevel ? aCoeff : rCoeff;
        followLevel = target + coeff * (followLevel - target);

        float env = peak ? followLevel : std::sqrt(juce::jmax(0.0f, followLevel));
        level = juce::jlimit(0.0f, 1.0f, env * gain);
        out[i] = level;
    }

    p.uiValue.store(level, std::memory_order_relaxed);
}
