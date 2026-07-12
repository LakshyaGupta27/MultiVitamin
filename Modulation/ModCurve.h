#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

// ── ModCurve ─────────────────────────────────────────────────────────────────
// User-drawable curve: up to MaxPoints breakpoints, each segment shaped by a
// curvature value in [-1, 1] (0 = linear, >0 = slow start, <0 = fast start —
// Bezier-style tension dragged from the segment midpoint in the editor).
//
// Threading: the point model is owned by the message thread. Every edit
// re-bakes the curve into one of two lookup tables and atomically publishes
// the fresh one, so the audio thread samples the curve lock-free and never
// sees a half-edited state.
class ModCurve {
public:
    static constexpr int MaxPoints = 32;
    static constexpr int LutSize   = 1024;

    struct Point {
        float x = 0.0f;      // 0..1, kept sorted; first pinned to 0, last to 1
        float y = 0.0f;      // 0..1
        float curve = 0.0f;  // shape of the segment leaving this point, -1..1
    };

    ModCurve() { setPreset(Preset::Triangle); }

    // ── Message-thread editing API ───────────────────────────────────────────
    enum class Preset { RampUp, RampDown, Triangle, Adsr };
    void setPreset(Preset p);

    int   getNumPoints() const           { return numPoints; }
    Point getPoint(int i) const          { return points[(size_t)juce::jlimit(0, numPoints - 1, i)]; }

    void  setPoint(int i, float x, float y);   // clamps + keeps ordering
    void  setCurvature(int i, float c);        // segment i → i+1
    int   addPoint(float x, float y);          // returns new index, or -1 if full
    void  removePoint(int i);                  // endpoints can't be removed

    // ── Audio-thread sampling API (lock-free) ────────────────────────────────
    // phase in [0,1) → y in [0,1]
    float sample(float phase) const noexcept {
        const auto& lut = tables[(size_t)activeTable.load(std::memory_order_acquire)];
        const float pos = juce::jlimit(0.0f, 1.0f, phase) * (float)(LutSize - 1);
        const int   i0  = (int)pos;
        const int   i1  = juce::jmin(i0 + 1, LutSize - 1);
        const float f   = pos - (float)i0;
        return lut[(size_t)i0] + (lut[(size_t)i1] - lut[(size_t)i0]) * f;
    }

    // ── Serialization (message thread) ───────────────────────────────────────
    void toXml(juce::XmlElement& parent, const char* tag) const;
    void fromXml(const juce::XmlElement& parent, const char* tag);

    // Segment interpolation shared with the audio-thread envelope stages:
    // t in [0,1], curvature c in [-1,1] → shaped t.
    static float shapeSegment(float t, float c) noexcept {
        // c = 0 → linear; c > 0 → slow start (ease-in); c < 0 → fast start.
        const float k = std::pow(4.0f, juce::jlimit(-1.0f, 1.0f, c));
        return std::pow(juce::jlimit(0.0f, 1.0f, t), k);
    }

private:
    void bake();  // re-render inactive LUT from points, then publish it

    std::array<Point, MaxPoints> points;
    int numPoints = 0;

    std::array<std::array<float, LutSize>, 2> tables {};
    std::atomic<int> activeTable { 0 };
};
