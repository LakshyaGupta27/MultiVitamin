#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PentagonSurface.h"
#include "ModulatorPanel.h"
#include "ControlPanel.h"

// DragAndDropContainer: modulator handles are dragged from the ModulatorPanel
// onto any ModSlider / pentagon vertex anywhere in the editor.
class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::DragAndDropContainer {
public:
    explicit PluginEditor(PluginProcessor& p);
    ~PluginEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& processor;

    PentagonSurface pentagon;
    ControlPanel    controlPanel;
    ModulatorPanel  modulatorPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
