#pragma once
#include "FxNode.h"

class FilterFx : public FxNode {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;
    Type getType() const override { return Type::Filter; }
    juce::String getParamName(int idx) const override { return idx == 0 ? "Cutoff" : "Resonance"; }

private:
    juce::dsp::StateVariableTPTFilter<float> filter;
};
