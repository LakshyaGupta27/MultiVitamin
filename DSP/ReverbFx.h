#pragma once
#include "FxNode.h"

class ReverbFx : public FxNode {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;
    Type getType() const override { return Type::Reverb; }
    juce::String getParamName(int idx) const override { return idx == 0 ? "Room" : "Damp"; }

private:
    juce::Reverb reverb;
    int numChannels = 2;
};
