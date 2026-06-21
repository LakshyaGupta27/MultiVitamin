#pragma once
#include <JuceHeader.h>
#include "DspEngine.h"
#include "PentagonSurface.h"
#include "LfoManagerPanel.h"
#include "ControlPanel.h"

class MainComponent : public juce::AudioAppComponent {
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    DspEngine dspEngine;

    PentagonSurface pentagon;
    ControlPanel    controlPanel;
    LfoManagerPanel lfoPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
