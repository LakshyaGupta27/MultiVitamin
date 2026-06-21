#pragma once
#include "FxNode.h"
#include <cstdint>

class DistortionFx : public FxNode {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;
    Type getType() const override { return Type::Distortion; }
    juce::String getParamName(int idx) const override { return idx == 0 ? "Drive" : "Mix"; }

private:
    struct Biquad {
        double a0 = 0, a1 = 0, a2 = 0, b1 = 0, b2 = 0;
        double sL1[2] {};
        double sL2[2] {};
        void compute(double freq, double reso);
        double process(double x, int ch);
        void reset();
    };

    Biquad stageA, stageB, stageC, stageD, stageE, stageF, stageG;
    double   iirSample[2] {};
    uint32_t fpd[2] { 0x12345678u, 0x87654321u };
    double   sampleRate = 44100.0;
};
