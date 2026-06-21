#include "DelayFx.h"

// Hardcoded tape character: regen filter frequency, resonance, vibrato depth
static constexpr double C_FREQ = 0.3;
static constexpr double D_RES  = 0.5;
static constexpr double E_VIB  = 0.3;

// Transposed direct-form II bandpass biquad; stores coeffs into f[0..6]
static void computeBandpassCoeffs(double freqParam, double resParam, double* f)
{
    f[0] = (std::pow(freqParam, 3) * 0.4) + 0.0001;
    f[1] = std::pow(resParam, 2) + 0.01;
    const double K    = std::tan(juce::MathConstants<double>::pi * f[0]);
    const double norm = 1.0 / (1.0 + K / f[1] + K * K);
    f[2] =  K / f[1] * norm;
    f[4] = -f[2];
    f[5] =  2.0 * (K * K - 1.0) * norm;
    f[6] =  (1.0 - K / f[1] + K * K) * norm;
}

void DelayFx::prepare(double sr, int /*maxBlock*/, int /*numCh*/)
{
    sampleRate = sr;

    computeBandpassCoeffs(C_FREQ, D_RES, regenFilterL);
    computeBandpassCoeffs(C_FREQ, D_RES, regenFilterR);

    // Output filter: same frequency, resonance scaled by golden ratio
    const double outRes = (std::pow(D_RES, 2) + 0.01) * 1.6180339887498948;
    outFilterL[0] = outFilterR[0] = regenFilterL[0];
    outFilterL[1] = outFilterR[1] = outRes;
    const double K    = std::tan(juce::MathConstants<double>::pi * outFilterL[0]);
    const double norm = 1.0 / (1.0 + K / outFilterL[1] + K * K);
    outFilterL[2] = outFilterR[2] =  K / outFilterL[1] * norm;
    outFilterL[4] = outFilterR[4] = -outFilterL[2];
    outFilterL[5] = outFilterR[5] =  2.0 * (K * K - 1.0) * norm;
    outFilterL[6] = outFilterR[6] =  (1.0 - K / outFilterL[1] + K * K) * norm;

    reset();
}

void DelayFx::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    const double A         = 1.0 - (double)param1.load(std::memory_order_relaxed); // invert: higher time = slower tape = longer delay
    const double baseSpeed = (std::pow(A, 4) * 25.0) + 1.0;
    const double feedback  = std::pow((double)param2.load(std::memory_order_relaxed), 2);
    const double vibSpeed  = std::pow(E_VIB, 5) * baseSpeed * (regenFilterL[0] * 0.09 + 0.025);

    int cycleEnd = juce::jlimit(1, 4, (int)std::floor(sampleRate / 44100.0));
    if (cycle > cycleEnd - 1) cycle = cycleEnd - 1;

    auto* chL = buffer.getWritePointer(0);
    auto* chR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        double sL = chL[i];
        double sR = chR ? (double)chR[i] : sL;

        // Denormal prevention
        if (std::fabs(sL) < 1.18e-23) sL = fpdL * 1.18e-17;
        if (std::fabs(sR) < 1.18e-23) sR = fpdR * 1.18e-17;

        if (++cycle == cycleEnd)
        {
            // Input-driven wow/flutter: signal energy modulates sweep phase
            const double speedL = baseSpeed + vibSpeed * (std::sin(sweepL) + 1.0);
            const double speedR = baseSpeed + vibSpeed * (std::sin(sweepR) + 1.0);
            sweepL += 0.05 * sL * sL; if (sweepL > 6.283185307179586) sweepL -= 6.283185307179586;
            sweepR += 0.05 * sR * sR; if (sweepR > 6.283185307179586) sweepR -= 6.283185307179586;

            // --- Left channel ---
            int pos = (int)std::floor(delayL);

            // Feed signal through regen bandpass into delay buffer
            double newSample = sL + dL[pos] * feedback;
            double temp = newSample * regenFilterL[2] + regenFilterL[7];
            regenFilterL[7] = -(temp * regenFilterL[5]) + regenFilterL[8];
            regenFilterL[8] =   newSample * regenFilterL[4] - temp * regenFilterL[6];
            newSample = temp;

            // Advance tape head and linearly interpolate written samples
            delayL -= speedL; if (delayL < 0) delayL += 88200.0;
            const double incL = (newSample - prevSampleL) / speedL;
            dL[pos] = prevSampleL;
            while (pos != (int)std::floor(delayL)) {
                dL[pos] = prevSampleL;
                prevSampleL += incL;
                if (--pos < 0) pos += 88200;
            }
            prevSampleL = newSample;

            // Read from new head position through output bandpass
            pos = (int)std::floor(delayL); sL = dL[pos];
            temp = sL * outFilterL[2] + outFilterL[7];
            outFilterL[7] = -(temp * outFilterL[5]) + outFilterL[8];
            outFilterL[8] =   sL * outFilterL[4] - temp * outFilterL[6];
            sL = temp;

            // --- Right channel ---
            pos = (int)std::floor(delayR);

            newSample = sR + dR[pos] * feedback;
            temp = newSample * regenFilterR[2] + regenFilterR[7];
            regenFilterR[7] = -(temp * regenFilterR[5]) + regenFilterR[8];
            regenFilterR[8] =   newSample * regenFilterR[4] - temp * regenFilterR[6];
            newSample = temp;

            delayR -= speedR; if (delayR < 0) delayR += 88200.0;
            const double incR = (newSample - prevSampleR) / speedR;
            dR[pos] = prevSampleR;
            while (pos != (int)std::floor(delayR)) {
                dR[pos] = prevSampleR;
                prevSampleR += incR;
                if (--pos < 0) pos += 88200;
            }
            prevSampleR = newSample;

            pos = (int)std::floor(delayR); sR = dR[pos];
            temp = sR * outFilterR[2] + outFilterR[7];
            outFilterR[7] = -(temp * outFilterR[5]) + outFilterR[8];
            outFilterR[8] =   sR * outFilterR[4] - temp * outFilterR[6];
            sR = temp;

            // Build interpolated output references for oversampled cycles
            if (cycleEnd == 4) {
                lastRefL[0] = lastRefL[4];
                lastRefL[2] = (lastRefL[0] + sL) * 0.5;
                lastRefL[1] = (lastRefL[0] + lastRefL[2]) * 0.5;
                lastRefL[3] = (lastRefL[2] + sL) * 0.5;
                lastRefL[4] = sL;
                lastRefR[0] = lastRefR[4];
                lastRefR[2] = (lastRefR[0] + sR) * 0.5;
                lastRefR[1] = (lastRefR[0] + lastRefR[2]) * 0.5;
                lastRefR[3] = (lastRefR[2] + sR) * 0.5;
                lastRefR[4] = sR;
            } else if (cycleEnd == 3) {
                lastRefL[0] = lastRefL[3];
                lastRefL[2] = (lastRefL[0] * 2.0 + sL) / 3.0;
                lastRefL[1] = (lastRefL[0] + sL * 2.0) / 3.0;
                lastRefL[3] = sL;
                lastRefR[0] = lastRefR[3];
                lastRefR[2] = (lastRefR[0] * 2.0 + sR) / 3.0;
                lastRefR[1] = (lastRefR[0] + sR * 2.0) / 3.0;
                lastRefR[3] = sR;
            } else if (cycleEnd == 2) {
                lastRefL[0] = lastRefL[2];
                lastRefL[1] = (lastRefL[0] + sL) * 0.5;
                lastRefL[2] = sL;
                lastRefR[0] = lastRefR[2];
                lastRefR[1] = (lastRefR[0] + sR) * 0.5;
                lastRefR[2] = sR;
            } else {
                lastRefL[0] = sL;
                lastRefR[0] = sR;
            }
            cycle = 0;
            sL = lastRefL[0];
            sR = lastRefR[0];
        }
        else
        {
            sL = lastRefL[cycle];
            sR = lastRefR[cycle];
        }

        // Multi-pole averaging (anti-imaging for oversampled rates, falls through intentionally)
        switch (cycleEnd)
        {
            case 4:
                lastRefL[8] = sL; sL = (sL + lastRefL[7]) * 0.5; lastRefL[7] = lastRefL[8];
                lastRefR[8] = sR; sR = (sR + lastRefR[7]) * 0.5; lastRefR[7] = lastRefR[8];
                [[fallthrough]];
            case 3:
                lastRefL[8] = sL; sL = (sL + lastRefL[6]) * 0.5; lastRefL[6] = lastRefL[8];
                lastRefR[8] = sR; sR = (sR + lastRefR[6]) * 0.5; lastRefR[6] = lastRefR[8];
                [[fallthrough]];
            case 2:
                lastRefL[8] = sL; sL = (sL + lastRefL[5]) * 0.5; lastRefL[5] = lastRefL[8];
                lastRefR[8] = sR; sR = (sR + lastRefR[5]) * 0.5; lastRefR[5] = lastRefR[8];
                [[fallthrough]];
            case 1:
                break;
        }

        chL[i] = (float)sL;
        if (chR) chR[i] = (float)sR;

        fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
        fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
    }
}

void DelayFx::reset()
{
    std::fill(dL, dL + DELAY_BUF_SIZE, 0.0);
    std::fill(dR, dR + DELAY_BUF_SIZE, 0.0);
    delayL = delayR = 0.0;
    prevSampleL = prevSampleR = 0.0;
    sweepL = 0.0; sweepR = 3.141592653589793;
    std::fill(lastRefL, lastRefL + 9, 0.0);
    std::fill(lastRefR, lastRefR + 9, 0.0);
    // Clear filter state only; coefficients computed in prepare() are preserved
    regenFilterL[7] = regenFilterL[8] = 0.0;
    regenFilterR[7] = regenFilterR[8] = 0.0;
    outFilterL[7]   = outFilterL[8]   = 0.0;
    outFilterR[7]   = outFilterR[8]   = 0.0;
    cycle = 0;
    fpdL = 1; fpdR = 1;
}
