#pragma once
#include "FxNode.h"

class DelayFx : public FxNode {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;
    void reset() override;
    Type getType() const override { return Type::Delay; }
    juce::String getParamName(int idx) const override { return idx == 0 ? "Time" : "Feedback"; }

private:
    static constexpr int DELAY_BUF_SIZE = 88200;

    double sampleRate = 44100.0;

    double dL[DELAY_BUF_SIZE] = {};
    double dR[DELAY_BUF_SIZE] = {};
    double delayL = 0.0, delayR = 0.0;
    double prevSampleL = 0.0, prevSampleR = 0.0;
    double sweepL = 0.0, sweepR = 3.141592653589793;
    double lastRefL[9] = {}, lastRefR[9] = {};

    // indices 0-1: freq/res params, 2/4/5/6: biquad coeffs, 7-8: state
    double regenFilterL[9] = {}, regenFilterR[9] = {};
    double outFilterL[9]   = {}, outFilterR[9]   = {};

    int      cycle = 0;
    uint32_t fpdL  = 1, fpdR = 1;
};
