#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      pentagon(p.dspEngine),
      controlPanel(p.dspEngine),
      modulatorPanel(p.dspEngine.modMatrix)
{
    addAndMakeVisible(pentagon);
    addAndMakeVisible(controlPanel);
    addAndMakeVisible(modulatorPanel);

    setSize(980, 820);
    setResizable(true, true);
    setResizeLimits(800, 660, 1500, 1200);
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0d0d1a));
}

void PluginEditor::resized() {
    auto bounds = getLocalBounds();
    const int modHeight = 290;
    const int sideWidth = 210;

    auto sideArea = bounds.removeFromRight(sideWidth);
    auto modArea  = bounds.removeFromBottom(modHeight);

    controlPanel  .setBounds(sideArea);
    modulatorPanel.setBounds(modArea);
    pentagon      .setBounds(bounds);
}
