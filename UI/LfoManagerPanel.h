#pragma once
#include <JuceHeader.h>
#include "DspEngine.h"
#include <vector>

class LfoManagerPanel : public juce::Component, private juce::Timer {
public:
    explicit LfoManagerPanel(DspEngine& engine);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    struct LfoRow : public juce::Component,
                    public juce::Slider::Listener {
        explicit LfoRow(DspEngine::LfoState& state, int index);
        void resized() override;
        void syncFromState();
        void updateDisplay();

        // Slider::Listener — track drag so the timer doesn't fight user input
        void sliderDragStarted(juce::Slider* s) override;
        void sliderDragEnded  (juce::Slider* s) override;
        void sliderValueChanged(juce::Slider*) override {}

        juce::Label       indexLabel;
        juce::ComboBox    shapeBox;
        juce::Slider      rateSlider;
        juce::Slider      depthSlider;
        juce::ComboBox    targetBox;
        juce::ToggleButton activeToggle { "On" };

        DspEngine::LfoState& lfoState;
        int lfoIndex;

        bool rateBeingDragged  = false;
        bool depthBeingDragged = false;
    };

    static constexpr int MAX_ROWS = DspEngine::MAX_LFOS;
    std::vector<std::unique_ptr<LfoRow>> rows;
    DspEngine& dspEngine;
};
