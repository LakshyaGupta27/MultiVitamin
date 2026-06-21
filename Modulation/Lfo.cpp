#include "Lfo.h"
#include <JuceHeader.h>
#include <cmath>

float Lfo::process(int numSamples) {
    const float twoPi = juce::MathConstants<float>::twoPi;
    const float pi    = juce::MathConstants<float>::pi;

    float output = 0.0f;
    switch (shape) {
        case Shape::Sine:
            output = std::sin(phase);
            break;
        case Shape::Triangle:
            output = phase < pi ? (phase / pi) * 2.0f - 1.0f
                                : 3.0f - (phase / pi) * 2.0f;
            break;
        case Shape::Saw:
            output = phase / pi - 1.0f;
            break;
        case Shape::Square:
            output = phase < pi ? 1.0f : -1.0f;
            break;
    }

    phase += twoPi * rate * (float)numSamples / (float)sampleRate;
    while (phase >= twoPi) phase -= twoPi;

    return output * depth;
}
