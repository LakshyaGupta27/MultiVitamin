#include "FilterFx.h"

void FilterFx::prepare(double sr, int maxBlock, int numCh) {
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sr;
    spec.maximumBlockSize = (uint32_t)maxBlock;
    spec.numChannels      = (uint32_t)numCh;
    filter.prepare(spec);
    filter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
}

void FilterFx::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    // param1: cutoff 0-1 -> 20Hz to 20kHz (exponential)
    const float cutoff01 = param1.load(std::memory_order_relaxed);
    const float res      = param2.load(std::memory_order_relaxed);
    const float freq = 20.0f * std::pow(1000.0f, cutoff01);
    filter.setCutoffFrequency(juce::jlimit(20.0f, 20000.0f, freq));
    filter.setResonance(juce::jlimit(0.1f, 10.0f, 0.1f + res * 9.9f));

    juce::dsp::AudioBlock<float> block(buffer);
    auto sub = block.getSubBlock(0, (size_t)numSamples);
    juce::dsp::ProcessContextReplacing<float> ctx(sub);
    filter.process(ctx);
}

void FilterFx::reset() {
    filter.reset();
}
