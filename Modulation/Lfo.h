#pragma once

class Lfo {
public:
    enum class Shape { Sine, Triangle, Saw, Square };

    // Call once per audio block; advances phase by numSamples; returns modulation value [-depth, +depth]
    float process(int numSamples);
    void  reset() noexcept { phase = 0.0f; prevOutput = 0.0f; }

    Shape  shape      = Shape::Sine;
    float  rate       = 0.5f;    // Hz, [0.01, 20]
    float  depth      = 0.5f;    // [0, 1]
    double sampleRate = 44100.0;

    // Written each block by the audio thread; read by other LFOs the following block
    // for LFO→LFO modulation (one-block latency keeps the chain non-recursive).
    float prevOutput = 0.0f;

private:
    float phase = 0.0f; // [0, 2π), audio-thread only
};
