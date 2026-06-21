#pragma once
#include <JuceHeader.h>
#include <unordered_set>
#include "DspEngine.h"
#include "FxNode.h"

class ControlPanel : public juce::Component,
                     private juce::Timer,
                     private juce::Slider::Listener {
public:
    explicit ControlPanel(DspEngine& engine);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void setupSection(int i, FxNode::Type type, const char* name,
                      const char* p1Name, const char* p2Name);
    void randomizeParams();

    void timerCallback() override;
    void sliderDragStarted(juce::Slider* s) override { draggedSliders.insert(s); }
    void sliderDragEnded  (juce::Slider* s) override { draggedSliders.erase(s); }
    void sliderValueChanged(juce::Slider*)  override {}

    DspEngine& dspEngine;
    std::unordered_set<juce::Slider*> draggedSliders;

    struct EffectSection {
        juce::Label  sectionLabel, p1Label, p2Label;
        juce::Slider p1Slider, p2Slider;
        FxNode::Type type = FxNode::Type::Delay;
    };

    static constexpr int NUM_EFFECTS = 5;
    std::array<EffectSection, NUM_EFFECTS> sections;

    juce::Label      dryWetLabel  { {}, "Dry / Wet" };
    juce::Slider     dryWetSlider;
    juce::TextButton randomizeBtn { "Randomize" };
};
