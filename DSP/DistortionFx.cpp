#include "DistortionFx.h"
#include <cmath>

namespace {
    constexpr double kCutoffHz = 12000.0;  // fixed lowpass frequency between stages
    constexpr double kIirParam = 0.05;     // fixed highpass / DC-blocker strength
    constexpr double kOutPad   = 0.5;      // fixed output attenuation

    constexpr double kReso[7] = {
        4.46570214, 1.51387132, 0.93979296, 0.70710678,
        0.59051105, 0.52972649, 0.50316379
    };
}

// ── Biquad ───────────────────────────────────────────────────────────────────

void DistortionFx::Biquad::compute(double freq, double reso) {
    const double K    = std::tan(juce::MathConstants<double>::pi * freq);
    const double norm = 1.0 / (1.0 + K / reso + K * K);
    a0 = K * K * norm;
    a1 = 2.0 * a0;
    a2 = a0;
    b1 = 2.0 * (K * K - 1.0) * norm;
    b2 = (1.0 - K / reso + K * K) * norm;
}

double DistortionFx::Biquad::process(double x, int ch) {
    const double out = x * a0 + sL1[ch];
    sL1[ch] = x * a1 - out * b1 + sL2[ch];
    sL2[ch] = x * a2 - out * b2;
    return out;
}

void DistortionFx::Biquad::reset() {
    for (int i = 0; i < 2; ++i) sL1[i] = sL2[i] = 0.0;
}

// ── DistortionFx ─────────────────────────────────────────────────────────────

void DistortionFx::prepare(double sr, int /*maxBlock*/, int /*numCh*/) {
    sampleRate = sr;
    reset();
}

void DistortionFx::reset() {
    stageA.reset(); stageB.reset(); stageC.reset(); stageD.reset();
    stageE.reset(); stageF.reset(); stageG.reset();
    for (int ch = 0; ch < 2; ++ch) {
        iirSample[ch] = 0.0;
        fpd[ch] = 0x12345678u + (uint32_t)ch;
    }
}

void DistortionFx::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    const double drive = param1.load(std::memory_order_relaxed);
    const double wet   = param2.load(std::memory_order_relaxed);

    const double overallscale = sampleRate / 44100.0;
    const double inTrim = ((1.0 - std::pow(1.0 - drive, 2.0)) * 1.0) + 1.0;

    double cutoff = kCutoffHz / sampleRate;
    if (cutoff > 0.49)   cutoff = 0.49;
    if (cutoff < 0.0001) cutoff = 0.0001;

    double iirAmount = std::pow(kIirParam, 3.0) * 0.5;
    if (iirAmount < 1e-8) iirAmount = 1e-8;
    iirAmount /= overallscale;

    Biquad* stages[7] = { &stageA, &stageB, &stageC, &stageD, &stageE, &stageF, &stageG };
    for (int s = 0; s < 7; ++s)
        stages[s]->compute(cutoff, kReso[s]);

    const int numCh = juce::jmin(buffer.getNumChannels(), 2);

    for (int ch = 0; ch < numCh; ++ch) {
        float* data = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i) {
            double x = data[i];
            const double dry = x;

            // Denormal protection
            if (std::fabs(x) < 1.18e-23) x = (double)fpd[ch] * 1.18e-17;

            // Gentle IIR highpass (DC blocker)
            if (std::fabs(iirSample[ch]) < 1.18e-33) iirSample[ch] = 0.0;
            iirSample[ch] = iirSample[ch] * (1.0 - iirAmount) + x * iirAmount;
            x -= iirSample[ch];

            if (x >  1.0) x =  1.0;
            if (x < -1.0) x = -1.0;

            // 7 stages: biquad lowpass → soft clip → inTrim gain
            for (int s = 0; s < 7; ++s) {
                x = stages[s]->process(x, ch);
                x = (x - x * std::fabs(x) * 0.5) * inTrim;
            }

            // 32-bit floating point TPDF dither (Airwindows)
            int expon;
            std::frexp((float)x, &expon);
            fpd[ch] ^= fpd[ch] << 13;
            fpd[ch] ^= fpd[ch] >> 17;
            fpd[ch] ^= fpd[ch] << 5;
            x += ((double)fpd[ch] - (double)0x7fffffffu) * 5.5e-36 * std::pow(2.0, expon + 62);

            data[i] = (float)(x * wet * kOutPad + dry * (1.0 - wet));
        }
    }
}
