#include "ControlPanel.h"

ControlPanel::ControlPanel(DspEngine& engine) : dspEngine(engine) {
    for (int i = 0; i < DspEngine::NUM_NODES; ++i) {
        auto& s = sections[(size_t)i];

        s.sectionLabel.setJustificationType(juce::Justification::centred);
        s.sectionLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        addAndMakeVisible(s.sectionLabel);

        for (int p = 0; p < 2; ++p) {
            const int dest = p == 0 ? ModDest::nodeP1(i) : ModDest::nodeP2(i);
            auto knob = std::make_unique<ModSlider>(dspEngine.modMatrix, dest, "");
            knob->setValue(dspEngine.getNodeParam(i, p), juce::dontSendNotification);
            knob->onValueChange = [this, i, p](float v) { dspEngine.setNodeParam(i, p, v); };
            addAndMakeVisible(*knob);
            (p == 0 ? s.p1Knob : s.p2Knob) = std::move(knob);
        }

        refreshSection(i);
    }

    dryWetKnob = std::make_unique<ModSlider>(dspEngine.modMatrix, ModDest::DryWet, "Dry / Wet");
    dryWetKnob->setValue(dspEngine.globalDryWet.load(), juce::dontSendNotification);
    dryWetKnob->setDefaultValue(0.7f);
    dryWetKnob->onValueChange = [this](float v) { dspEngine.globalDryWet.store(v); };
    addAndMakeVisible(*dryWetKnob);

    randomizeBtn.onClick = [this] { randomizeParams(); };
    addAndMakeVisible(randomizeBtn);

    startTimerHz(10);
}

void ControlPanel::refreshSection(int i) {
    auto& s = sections[(size_t)i];
    const auto type = dspEngine.getNodeType(i);
    s.shownType = type;
    s.sectionLabel.setText("N" + juce::String(i + 1) + " \xc2\xb7 " + FxNode::typeName(type),
                           juce::dontSendNotification);

    // Param names come from the node itself so they follow the FX type.
    juce::String p1 = "P1", p2 = "P2";
    switch (type) {
        case FxNode::Type::Delay:      p1 = "Time";   p2 = "Feedback";  break;
        case FxNode::Type::Reverb:     p1 = "Room";   p2 = "Damp";      break;
        case FxNode::Type::Filter:     p1 = "Cutoff"; p2 = "Resonance"; break;
        case FxNode::Type::Granular:   p1 = "Size";   p2 = "Scatter";   break;
        case FxNode::Type::Distortion: p1 = "Drive";  p2 = "Mix";       break;
    }
    s.p1Knob->setLabel(p1);
    s.p2Knob->setLabel(p2);
}

void ControlPanel::randomizeParams() {
    juce::Random rng;
    for (int n = 0; n < DspEngine::NUM_NODES; ++n) {
        dspEngine.setNodeParam(n, 0, rng.nextFloat());
        dspEngine.setNodeParam(n, 1, rng.nextFloat());
        sections[(size_t)n].p1Knob->setValue(dspEngine.getNodeParam(n, 0), juce::dontSendNotification);
        sections[(size_t)n].p2Knob->setValue(dspEngine.getNodeParam(n, 1), juce::dontSendNotification);
    }
}

void ControlPanel::timerCallback() {
    for (int i = 0; i < DspEngine::NUM_NODES; ++i) {
        auto& s = sections[(size_t)i];
        if (dspEngine.getNodeType(i) != s.shownType)
            refreshSection(i);

        // Follow external changes (preset load) unless the user is dragging.
        if (!s.p1Knob->isMouseButtonDown())
            s.p1Knob->setValue(dspEngine.getNodeParam(i, 0), juce::dontSendNotification);
        if (!s.p2Knob->isMouseButtonDown())
            s.p2Knob->setValue(dspEngine.getNodeParam(i, 1), juce::dontSendNotification);
    }
    if (!dryWetKnob->isMouseButtonDown())
        dryWetKnob->setValue(dspEngine.globalDryWet.load(), juce::dontSendNotification);
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

    const int sectionH = (r.getHeight() - 110) / DspEngine::NUM_NODES;
    for (auto& s : sections) {
        auto sec = r.removeFromTop(sectionH);
        s.sectionLabel.setBounds(sec.removeFromTop(13));
        auto left  = sec.removeFromLeft(sec.getWidth() / 2).reduced(2, 0);
        auto right = sec.reduced(2, 0);
        s.p1Knob->setBounds(left);
        s.p2Knob->setBounds(right);
    }

    r.removeFromTop(4);
    dryWetKnob->setBounds(r.removeFromTop(78).withSizeKeepingCentre(84, 78));
    r.removeFromTop(4);
    randomizeBtn.setBounds(r.removeFromTop(24).reduced(4, 0));
}
