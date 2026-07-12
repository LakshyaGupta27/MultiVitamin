#pragma once
#include <JuceHeader.h>
#include "../Modulation/ModCurve.h"

// ── CurveEditor ──────────────────────────────────────────────────────────────
// Interactive editor for a ModCurve:
//   • drag points to move them
//   • drag the diamond between two points to bend the segment (Bezier-style)
//   • double-click empty space to add a point, double-click a point to remove
//   • live playhead + value dot driven by playheadFn (return < 0 to hide)
class CurveEditor : public juce::Component, private juce::Timer {
public:
    explicit CurveEditor(ModCurve& curve);

    void setBipolarDisplay(bool b) { bipolarDisplay = b; repaint(); }

    std::function<float()> playheadFn;   // normalized position, or -1 to hide
    std::function<void()>  onChange;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { dragPoint = dragSegment = -1; }

private:
    void timerCallback() override;

    juce::Point<float> toScreen(float x, float y) const;
    float toNormX(float px) const;
    float toNormY(float py) const;
    juce::Point<float> segmentHandle(int seg) const;   // curvature handle position
    int hitPoint(juce::Point<float> p) const;          // -1 if none
    int hitSegmentHandle(juce::Point<float> p) const;  // -1 if none
    void changed() { if (onChange) onChange(); repaint(); }

    ModCurve& curve;
    bool  bipolarDisplay = false;
    int   dragPoint   = -1;
    int   dragSegment = -1;
    float dragStartCurve = 0.0f;
    float lastPlayhead   = -1.0f;
};
