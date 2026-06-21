#include "ControlPanel.h"

ControlPanel::ControlPanel(DspEngine& engine) : dspEngine(engine) {
    setupSection(0, FxNode::Type::Delay,      "Delay",      "Time",      "Feedback");
    setupSection(1, FxNode::Type::Reverb,     "Reverb",     "Room",      "Damp");
    setupSection(2, FxNode::Type::Filter,     "Filter",     "Cutoff",    "Resonance");
    setupSection(3, FxNode::Type::Granular,   "Granular",   "Size",      "Scatter");
    setupSection(4, FxNode::Type::Distortion, "Distortion", "Drive",     "Mix");

    dryWetLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(dryWetLabel);

    dryWetSlider.setRange(0.0, 1.0, 0.01);
    dryWetSlider.setValue(dspEngine.globalDryWet.load(), juce::dontSendNotification);
    dryWetSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    dryWetSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
    dryWetSlider.onValueChange = [this] {
        dspEngine.globalDryWet.store((float)dryWetSlider.getValue());
    };
    addAndMakeVisible(dryWetSlider);

    randomizeBtn.onClick = [this] { randomizeParams(); };
    addAndMakeVisible(randomizeBtn);

    startTimerHz(60);
}

void ControlPanel::setupSection(int i, FxNode::Type type, const char* name,
                                 const char* p1Name, const char* p2Name) {
    auto& s = sections[i];
    s.type = type;

    s.sectionLabel.setText(name, juce::dontSendNotification);
    s.sectionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(s.sectionLabel);

    s.p1Label.setText(p1Name, juce::dontSendNotification);
    s.p1Label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(s.p1Label);

    s.p2Label.setText(p2Name, juce::dontSendNotification);
    s.p2Label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(s.p2Label);

    for (int p = 0; p < 2; ++p) {
        auto& slider = (p == 0 ? s.p1Slider : s.p2Slider);
        slider.setRange(0.0, 1.0, 0.01);
        slider.setValue(0.5, juce::dontSendNotification);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
        const int paramIdx = p;
        slider.onValueChange = [this, type, &slider, paramIdx] {
            for (int n = 0; n < DspEngine::NUM_NODES; ++n)
                if (dspEngine.getNodeType(n) == type)
                    dspEngine.setNodeParam(n, paramIdx, (float)slider.getValue());
        };
        slider.addListener(this);
        addAndMakeVisible(slider);
    }

    // Seed sliders from the first node of this type, if one exists
    for (int n = 0; n < DspEngine::NUM_NODES; ++n) {
        if (dspEngine.getNodeType(n) == type) {
            s.p1Slider.setValue(dspEngine.getNodeParam(n, 0), juce::dontSendNotification);
            s.p2Slider.setValue(dspEngine.getNodeParam(n, 1), juce::dontSendNotification);
            break;
        }
    }
}

void ControlPanel::randomizeParams() {
    juce::Random rng;
    for (int n = 0; n < DspEngine::NUM_NODES; ++n) {
        dspEngine.setNodeParam(n, 0, rng.nextFloat());
        dspEngine.setNodeParam(n, 1, rng.nextFloat());
    }
    for (auto& s : sections) {
        for (int n = 0; n < DspEngine::NUM_NODES; ++n) {
            if (dspEngine.getNodeType(n) == s.type) {
                s.p1Slider.setValue(dspEngine.getNodeParam(n, 0), juce::dontSendNotification);
                s.p2Slider.setValue(dspEngine.getNodeParam(n, 1), juce::dontSendNotification);
                break;
            }
        }
    }
}

void ControlPanel::timerCallback() {
    for (auto& s : sections) {
        for (int n = 0; n < DspEngine::NUM_NODES; ++n) {
            if (dspEngine.getNodeType(n) == s.type) {
                if (!draggedSliders.count(&s.p1Slider))
                    s.p1Slider.setValue(dspEngine.getNodeParam(n, 0), juce::dontSendNotification);
                if (!draggedSliders.count(&s.p2Slider))
                    s.p2Slider.setValue(dspEngine.getNodeParam(n, 1), juce::dontSendNotification);
                break;
            }
        }
    }
}

void ControlPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff16162a));
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawRect(getLocalBounds().toFloat(), 0.5f);
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("Controls", 0, 4, getWidth(), 14, juce::Justification::centred);
}

void ControlPanel::resized() {
    auto r = getLocalBounds().reduced(8);
    r.removeFromTop(18); // title

    for (auto& s : sections) {
        s.sectionLabel.setBounds(r.removeFromTop(13));
        auto row  = r.removeFromTop(76);
        auto left = row.removeFromLeft(row.getWidth() / 2).reduced(2, 0);
        auto right = row.reduced(2, 0);

        s.p1Label.setBounds(left .removeFromTop(12));
        s.p2Label.setBounds(right.removeFromTop(12));
        s.p1Slider.setBounds(left);
        s.p2Slider.setBounds(right);

        r.removeFromTop(6);
    }

    dryWetLabel .setBounds(r.removeFromTop(13));
    dryWetSlider.setBounds(r.removeFromTop(76).withSizeKeepingCentre(70, 76));

    r.removeFromTop(8);
    randomizeBtn.setBounds(r.removeFromTop(26).reduced(4, 0));
}
