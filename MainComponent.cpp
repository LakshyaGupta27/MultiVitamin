#include "MainComponent.h"

MainComponent::MainComponent()
    : pentagon(dspEngine),
      controlPanel(dspEngine, deviceManager),
      lfoPanel(dspEngine)
{
    addAndMakeVisible(pentagon);
    addAndMakeVisible(controlPanel);
    addAndMakeVisible(lfoPanel);

    setSize(920, 760);
    setAudioChannels(2, 2);
}

MainComponent::~MainComponent() {
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    dspEngine.prepare(sampleRate, samplesPerBlockExpected, 2);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    if (info.buffer->getNumChannels() == 0) {
        info.clearActiveBufferRegion();
        return;
    }
    juce::AudioBuffer<float> block(
        info.buffer->getArrayOfWritePointers(),
        info.buffer->getNumChannels(),
        info.startSample,
        info.numSamples);
    dspEngine.process(block, info.numSamples);
}

void MainComponent::releaseResources() {
    dspEngine.releaseResources();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0d0d1a));
}

void MainComponent::resized() {
    auto bounds = getLocalBounds();
    const int lfoHeight  = 240;
    const int sideWidth  = 200;

    auto lfoArea  = bounds.removeFromBottom(lfoHeight);
    auto sideArea = bounds.removeFromRight(sideWidth);

    lfoPanel    .setBounds(lfoArea);
    controlPanel.setBounds(sideArea);
    pentagon    .setBounds(bounds);
}
