#pragma once
#include <JuceHeader.h>
#include "DspEngine.h"
#include "FxNode.h"
#include "ModSlider.h"

// Right-hand panel: one section per pentagon node (the FX type can change at
// runtime), each with two mod-aware knobs, plus global dry/wet. Every knob is
// a drag-and-drop modulation target with an animated modulation ring.
class ControlPanel : public juce::Component, private juce::Timer {
public:
    explicit ControlPanel(DspEngine& engine);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void randomizeParams();
    void refreshSection(int i);

    DspEngine& dspEngine;

    struct NodeSection {
        juce::Label sectionLabel;
        std::unique_ptr<ModSlider> p1Knob, p2Knob;
        FxNode::Type shownType = FxNode::Type::Delay;
    };

    std::array<NodeSection, DspEngine::NUM_NODES> sections;

    std::unique_ptr<ModSlider> dryWetKnob;
    juce::TextButton randomizeBtn { "Randomize" };
};
