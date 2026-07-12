#include "CurveEditor.h"

static constexpr float PAD       = 8.0f;
static constexpr float POINT_R   = 5.0f;
static constexpr float HANDLE_R  = 4.0f;
static constexpr float HIT_R     = 9.0f;

CurveEditor::CurveEditor(ModCurve& c) : curve(c) {
    startTimerHz(30);
}

void CurveEditor::timerCallback() {
    if (!playheadFn) return;
    const float ph = playheadFn();
    if (std::abs(ph - lastPlayhead) > 0.002f) repaint();
}

juce::Point<float> CurveEditor::toScreen(float x, float y) const {
    return { PAD + x * ((float)getWidth() - 2 * PAD),
             PAD + (1.0f - y) * ((float)getHeight() - 2 * PAD) };
}
float CurveEditor::toNormX(float px) const {
    return juce::jlimit(0.0f, 1.0f, (px - PAD) / juce::jmax(1.0f, (float)getWidth() - 2 * PAD));
}
float CurveEditor::toNormY(float py) const {
    return juce::jlimit(0.0f, 1.0f, 1.0f - (py - PAD) / juce::jmax(1.0f, (float)getHeight() - 2 * PAD));
}

juce::Point<float> CurveEditor::segmentHandle(int seg) const {
    const auto a = curve.getPoint(seg);
    const auto b = curve.getPoint(seg + 1);
    const float mx = (a.x + b.x) * 0.5f;
    const float t  = ModCurve::shapeSegment(0.5f, a.curve);
    const float my = a.y + (b.y - a.y) * t;
    return toScreen(mx, my);
}

int CurveEditor::hitPoint(juce::Point<float> p) const {
    for (int i = 0; i < curve.getNumPoints(); ++i) {
        const auto pt = curve.getPoint(i);
        if (toScreen(pt.x, pt.y).getDistanceFrom(p) < HIT_R) return i;
    }
    return -1;
}

int CurveEditor::hitSegmentHandle(juce::Point<float> p) const {
    for (int i = 0; i < curve.getNumPoints() - 1; ++i)
        if (segmentHandle(i).getDistanceFrom(p) < HIT_R) return i;
    return -1;
}

void CurveEditor::mouseDown(const juce::MouseEvent& e) {
    dragPoint = hitPoint(e.position);
    if (dragPoint >= 0) return;
    dragSegment = hitSegmentHandle(e.position);
    if (dragSegment >= 0)
        dragStartCurve = curve.getPoint(dragSegment).curve;
}

void CurveEditor::mouseDrag(const juce::MouseEvent& e) {
    if (dragPoint >= 0) {
        curve.setPoint(dragPoint, toNormX(e.position.x), toNormY(e.position.y));
        changed();
    } else if (dragSegment >= 0) {
        // Vertical drag bends the segment; direction follows the segment slope
        // so pulling the handle toward a point always eases toward it.
        const auto a = curve.getPoint(dragSegment);
        const auto b = curve.getPoint(dragSegment + 1);
        const float dir = b.y >= a.y ? -1.0f : 1.0f;
        curve.setCurvature(dragSegment,
                           dragStartCurve + dir * (float)e.getDistanceFromDragStartY() / 60.0f);
        changed();
    }
}

void CurveEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    const int p = hitPoint(e.position);
    if (p >= 0) curve.removePoint(p);
    else        curve.addPoint(toNormX(e.position.x), toNormY(e.position.y));
    changed();
}

void CurveEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff10101e));
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRect(getLocalBounds());

    // Grid
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for (int i = 1; i < 4; ++i) {
        const auto v = toScreen((float)i / 4.0f, 0.0f);
        const auto h = toScreen(0.0f, (float)i / 4.0f);
        g.drawVerticalLine  ((int)v.x, PAD, (float)getHeight() - PAD);
        g.drawHorizontalLine((int)h.y, PAD, (float)getWidth()  - PAD);
    }
    if (bipolarDisplay) {
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        const auto mid = toScreen(0.0f, 0.5f);
        g.drawHorizontalLine((int)mid.y, PAD, (float)getWidth() - PAD);
    }

    // Curve (sampled from the same LUT the audio thread plays)
    juce::Path path;
    const int steps = juce::jmax(32, getWidth());
    for (int i = 0; i <= steps; ++i) {
        const float x = (float)i / (float)steps;
        const auto  p = toScreen(x, curve.sample(x));
        if (i == 0) path.startNewSubPath(p); else path.lineTo(p);
    }
    g.setColour(juce::Colour(0xff00c8ff));
    g.strokePath(path, juce::PathStrokeType(2.0f));

    // Fill under curve
    juce::Path fill(path);
    fill.lineTo(toScreen(1.0f, bipolarDisplay ? 0.5f : 0.0f));
    fill.lineTo(toScreen(0.0f, bipolarDisplay ? 0.5f : 0.0f));
    fill.closeSubPath();
    g.setColour(juce::Colour(0xff00c8ff).withAlpha(0.10f));
    g.fillPath(fill);

    // Segment curvature handles
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    for (int i = 0; i < curve.getNumPoints() - 1; ++i) {
        const auto h = segmentHandle(i);
        juce::Path diamond;
        diamond.addPolygon(h, 4, HANDLE_R, juce::MathConstants<float>::pi * 0.25f);
        g.fillPath(diamond);
    }

    // Points
    for (int i = 0; i < curve.getNumPoints(); ++i) {
        const auto pt = curve.getPoint(i);
        const auto p  = toScreen(pt.x, pt.y);
        g.setColour(juce::Colour(0xffff9040));
        g.fillEllipse(p.x - POINT_R, p.y - POINT_R, POINT_R * 2, POINT_R * 2);
        g.setColour(juce::Colours::white);
        g.drawEllipse(p.x - POINT_R, p.y - POINT_R, POINT_R * 2, POINT_R * 2, 1.0f);
    }

    // Playhead
    if (playheadFn) {
        const float ph = playheadFn();
        lastPlayhead = ph;
        if (ph >= 0.0f && ph <= 1.0f) {
            const float y = curve.sample(ph);
            const auto top = toScreen(ph, 1.0f);
            const auto bot = toScreen(ph, 0.0f);
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.drawLine(top.x, top.y, bot.x, bot.y, 1.0f);
            const auto dot = toScreen(ph, y);
            g.setColour(juce::Colours::white);
            g.fillEllipse(dot.x - 3.5f, dot.y - 3.5f, 7.0f, 7.0f);
        }
    }
}
