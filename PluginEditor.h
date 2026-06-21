#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PentagonSurface.h"
#include "LfoManagerPanel.h"
#include "ControlPanel.h"

class PluginEditor : public juce::AudioProcessorEditor {
public:
    explicit PluginEditor(PluginProcessor& p);
    ~PluginEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& processor;

    PentagonSurface pentagon;
    ControlPanel    controlPanel;
    LfoManagerPanel lfoPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
