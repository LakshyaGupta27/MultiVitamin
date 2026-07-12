#pragma once
#include <JuceHeader.h>
#include "DspEngine.h"
#include <array>
#include <functional>

class PentagonSurface : public juce::Component,
                        public juce::DragAndDropTarget,
                        private juce::Timer {
public:
    explicit PentagonSurface(DspEngine& engine);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    // Drop a modulator on a node circle → routing to that node's mix weight
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails&) override { dragOverVertex = -1; repaint(); }

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
    int  vertexAt(juce::Point<float> p) const;   // -1 if none within reach

    juce::Point<float> cursor;  // base anchor set by mouse
    DspEngine& dspEngine;
    std::array<juce::String, N> nodeLabels;
    int dragOverVertex = -1;    // vertex highlighted during a modulator drag
};
