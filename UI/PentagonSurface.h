#pragma once
#include <JuceHeader.h>
#include "DspEngine.h"
#include <array>
#include <functional>

class PentagonSurface : public juce::Component, private juce::Timer {
public:
    explicit PentagonSurface(DspEngine& engine);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    std::function<void()> onWeightsChanged;

private:
    static constexpr int N = DspEngine::NUM_NODES;

    void timerCallback() override { repaint(); }

    std::array<juce::Point<float>, N> getVertices() const;
    std::array<float, N>              computeWeights(juce::Point<float> p) const;

    // Effective (LFO-driven) cursor = weighted centroid of effectiveWeights
    juce::Point<float> effectiveCursorPos() const;

    // Pentagon containment and clamping
    bool isInsidePentagon(juce::Point<float> p) const;
    juce::Point<float> clampToPolygon(juce::Point<float> p) const;
    static juce::Point<float> nearestOnSegment(juce::Point<float> p,
                                                juce::Point<float> a,
                                                juce::Point<float> b);

    void updateWeights();
    void showNodeMenu(int nodeIndex);

    juce::Point<float> cursor;  // base anchor set by mouse
    DspEngine& dspEngine;
    std::array<juce::String, N> nodeLabels;
};
