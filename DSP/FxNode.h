#pragma once
#include <JuceHeader.h>
#include <memory>
#include <atomic>

class FxNode {
public:
    enum class Type { Delay, Reverb, Filter, Granular, Distortion };

    static std::unique_ptr<FxNode> create(Type t);

    virtual ~FxNode() = default;
    virtual void prepare(double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void process(juce::AudioBuffer<float>& buffer, int numSamples) = 0;
    virtual void reset() = 0;
    virtual Type getType() const = 0;
    virtual juce::String getParamName(int idx) const = 0;

    static const char* typeName(Type t) {
        switch (t) {
            case Type::Delay:       return "Delay";
            case Type::Reverb:      return "Reverb";
            case Type::Filter:      return "Filter";
            case Type::Granular:    return "Granular";
            case Type::Distortion:  return "Distortion";
        }
        return "?";
    }

    std::atomic<float> param1 { 0.5f };
    std::atomic<float> param2 { 0.5f };
};
