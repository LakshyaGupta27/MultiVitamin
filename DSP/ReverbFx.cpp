#include "ReverbFx.h"

void ReverbFx::prepare(double /*sr*/, int /*maxBlock*/, int numCh) {
    numChannels = numCh;
    reverb.reset();
}

void ReverbFx::process(juce::AudioBuffer<float>& buffer, int numSamples) {
    juce::Reverb::Parameters p;
    p.roomSize  = param1.load(std::memory_order_relaxed);
    p.damping   = param2.load(std::memory_order_relaxed);
    p.wetLevel  = 1.0f;
    p.dryLevel  = 0.0f;
    p.width     = 1.0f;
    p.freezeMode = 0.0f;
    reverb.setParameters(p);

    if (buffer.getNumChannels() >= 2)
        reverb.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);
    else if (buffer.getNumChannels() == 1)
        reverb.processMono(buffer.getWritePointer(0), numSamples);
}

void ReverbFx::reset() {
    reverb.reset();
}
