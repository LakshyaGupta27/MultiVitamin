#include "XLfo.h"
#include <cmath>

// Deterministic integer hash → bipolar float. Keying random shapes off the
// cycle index (instead of an RNG stream) keeps them phase-coherent with the
// transport and lets the UI draw the exact waveform.
float XLfo::hashNoise(int64_t cycle, int seed) noexcept {
    uint32_t h = (uint32_t)(cycle * 2654435761LL) ^ (uint32_t)(seed * 40503 + 12345);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float)(h & 0xffffffu) / (float)0x7fffffu - 1.0f;
}

float XLfo::posToPhase(int playMode, double pos) noexcept {
    switch (playMode) {
        case XLfoParams::OneShot:
            return (float)juce::jlimit(0.0, 1.0, pos);
        case XLfoParams::PingPong: {
            double t = std::fmod(pos, 2.0); if (t < 0.0) t += 2.0;
            return (float)(t < 1.0 ? t : 2.0 - t);
        }
        case XLfoParams::Reverse: {
            double f = pos - std::floor(pos);
            return (float)(1.0 - f);
        }
        case XLfoParams::Loop:
        default: {
            double f = pos - std::floor(pos);
            return (float)f;
        }
    }
}

float XLfo::evalShape(int shape, float phase01, float pulseWidth,
                      const ModCurve* curve, double cyclePos, int seed) noexcept {
    const float ph = phase01 - std::floor(phase01);
    switch (shape) {
        case XLfoParams::Sine:
            return std::sin(ph * juce::MathConstants<float>::twoPi);
        case XLfoParams::Triangle:
            return ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f;
        case XLfoParams::SawUp:
            return ph * 2.0f - 1.0f;
        case XLfoParams::SawDown:
            return 1.0f - ph * 2.0f;
        case XLfoParams::Square:
            return ph < 0.5f ? 1.0f : -1.0f;
        case XLfoParams::Pulse:
            return ph < pulseWidth ? 1.0f : -1.0f;
        case XLfoParams::SampleHold: {
            const auto cycle = (int64_t)std::floor(cyclePos);
            return hashNoise(cycle, seed);
        }
        case XLfoParams::SmoothRandom: {
            const auto  cycle = (int64_t)std::floor(cyclePos);
            const float f     = (float)(cyclePos - std::floor(cyclePos));
            const float a     = hashNoise(cycle, seed);
            const float b     = hashNoise(cycle + 1, seed);
            const float s     = f * f * (3.0f - 2.0f * f);  // smoothstep
            return a + (b - a) * s;
        }
        case XLfoParams::Custom:
            return curve != nullptr ? curve->sample(ph) * 2.0f - 1.0f : 0.0f;
        default:
            return 0.0f;
    }
}

float XLfo::evalAt(int shape, int playMode, double pos,
                   float phaseOffset, float pulseWidth,
                   const ModCurve* curve, int seed) noexcept {
    // Random shapes track raw position (offset shifts the cycle boundary);
    // the mode-mapped phase drives the deterministic shapes.
    const double offsetPos = pos + (double)phaseOffset;
    const float  phase     = posToPhase(playMode, pos) + phaseOffset;
    return evalShape(shape, phase, pulseWidth, curve, offsetPos, seed);
}

void XLfo::renderBlock(XLfoParams& p, const TransportInfo& transport,
                       const ModTriggerList& triggers, const XLfoMod& mod,
                       int numSamples, float* out, int lfoIndex) {
    const int   shape    = p.shape.load(std::memory_order_relaxed);
    const int   playMode = p.playMode.load(std::memory_order_relaxed);
    const int   trigMode = p.trigMode.load(std::memory_order_relaxed);
    const float fadeSec  = p.fadeInSec.load(std::memory_order_relaxed);

    // Meta-modulated parameters (base + offset from the matrix).
    const float phaseOff = p.phaseOffset.load(std::memory_order_relaxed) + mod.phase;
    const float pw       = juce::jlimit(0.02f, 0.98f,
                             p.pulseWidth.load(std::memory_order_relaxed) + mod.width);

    // Rate: free Hz or synced to host tempo, then meta-modulated by ±3 octaves.
    double rateHz;
    const auto& divs = syncDivisions();
    const int   divIdx = juce::jlimit(0, (int)divs.size() - 1,
                                      p.syncDiv.load(std::memory_order_relaxed));
    if (p.tempoSync.load(std::memory_order_relaxed)) {
        const double cycleSec = divs[(size_t)divIdx].beats * 60.0
                              / juce::jmax(1.0, transport.bpm);
        rateHz = 1.0 / juce::jmax(1e-4, cycleSec);
    } else {
        rateHz = (double)p.rateHz.load(std::memory_order_relaxed);
    }
    rateHz *= std::pow(2.0, (double)juce::jlimit(-1.0f, 1.0f, mod.rate) * 3.0);
    rateHz  = juce::jlimit(0.0001, 200.0, rateHz);

    const float amount = juce::jlimit(0.0f, 1.0f,
        p.amount.load(std::memory_order_relaxed) + mod.amount);

    // (Re)trigger handling ----------------------------------------------------
    if (!started) { started = true; fadeGain = fadeSec > 0.0f ? 0.0f : 1.0f; }

    if (trigMode == XLfoParams::Transport) {
        if (transport.justStarted) {
            pos = 0.0;
            fadeGain = fadeSec > 0.0f ? 0.0f : 1.0f;
        }
        if (transport.isPlaying && p.tempoSync.load(std::memory_order_relaxed)) {
            // Phase-lock to the song position while playing.
            pos = transport.ppqPosition / divs[(size_t)divIdx].beats;
        }
    }

    const double inc      = rateHz / sampleRate;
    const float  fadeInc  = fadeSec > 1e-4f ? (float)(1.0 / (sampleRate * fadeSec)) : 1.0f;
    const float  smMs     = p.smoothMs.load(std::memory_order_relaxed);
    const float  smCoeff  = smMs > 0.01f
        ? (float)std::exp(-1.0 / (sampleRate * (double)smMs * 0.001)) : 0.0f;

    int nextTrig = 0;
    for (int i = 0; i < numSamples; ++i) {
        // Sample-accurate MIDI retrigger
        while (trigMode == XLfoParams::Midi
               && nextTrig < triggers.numEvents
               && triggers.events[(size_t)nextTrig].sampleOffset <= i) {
            if (triggers.events[(size_t)nextTrig].isNoteOn) {
                pos = 0.0;
                fadeGain = fadeSec > 0.0f ? 0.0f : 1.0f;
            }
            ++nextTrig;
        }

        float v = evalAt(shape, playMode, pos, phaseOff, pw, &p.curve, lfoIndex);

        fadeGain = juce::jmin(1.0f, fadeGain + fadeInc);
        v *= amount * fadeGain;

        // Output smoothing (one-pole)
        smooth = v + smCoeff * (smooth - v);
        out[i] = smooth;

        if (playMode != XLfoParams::OneShot || pos < 1.0)
            pos += inc;
    }

    // Publish live state for the UI
    p.uiPhase   .store(posToPhase(playMode, pos), std::memory_order_relaxed);
    p.uiValue   .store(smooth,        std::memory_order_relaxed);
    p.uiEffRateHz     .store((float)rateHz, std::memory_order_relaxed);
    p.uiEffAmount      .store(amount,       std::memory_order_relaxed);
    p.uiEffPhaseOffset .store(phaseOff - std::floor(phaseOff), std::memory_order_relaxed);
    p.uiEffPulseWidth  .store(pw,           std::memory_order_relaxed);
}
