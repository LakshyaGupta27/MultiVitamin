#pragma once
#include "FxNode.h"
#include <array>

class GranularFx : public FxNode {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;
    Type getType() const override { return Type::Granular; }
    juce::String getParamName(int idx) const override { return idx == 0 ? "Grain Size" : "Scatter"; }

private:
    static constexpr int MAX_GRAINS = 4;

    struct Grain {
        int readPos = 0;
        int length  = 0;
        int age     = 0;
        bool active = false;
    };

    juce::AudioBuffer<float> ringBuffer;
    int writePos = 0;
    int samplesSinceLastGrain = 0;
    std::array<Grain, MAX_GRAINS> grains;
    double sampleRate = 44100.0;
    int numChannels   = 2;
    juce::Random random;
};
