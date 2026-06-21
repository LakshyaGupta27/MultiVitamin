#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      pentagon(p.dspEngine),
      controlPanel(p.dspEngine),
      lfoPanel(p.dspEngine)
{
    addAndMakeVisible(pentagon);
    addAndMakeVisible(controlPanel);
    addAndMakeVisible(lfoPanel);

    setSize(920, 760);
    setResizable(true, true);
    setResizeLimits(700, 550, 1400, 1100);
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0d0d1a));
}

void PluginEditor::resized() {
    auto bounds = getLocalBounds();
    const int lfoHeight = 200;
    const int sideWidth = 200;

    auto sideArea = bounds.removeFromRight(sideWidth);
    auto lfoArea  = bounds.removeFromBottom(lfoHeight);

    controlPanel.setBounds(sideArea);
    lfoPanel    .setBounds(lfoArea);
    pentagon    .setBounds(bounds);
}
