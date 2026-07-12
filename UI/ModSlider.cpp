#include "ModSlider.h"
#include "ModRoutingEditor.h"

static constexpr float ANGLE_RANGE = 2.356194f;   // ±135° from 12 o'clock

ModSlider::ModSlider(ModMatrix& m, int destId, const juce::String& label)
    : matrix(m), dest(destId), labelText(label)
{
    startTimerHz(30);
}

int ModSlider::sourceIdFromDescription(const juce::var& desc) {
    const auto s = desc.toString();
    if (!s.startsWith("modsrc:")) return -1;
    const int id = s.fromFirstOccurrenceOf(":", false, false).getIntValue();
    return id >= 0 && id < ModConst::NUM_SOURCES ? id : -1;
}

void ModSlider::setValue(float newValue, juce::NotificationType notify) {
    newValue = juce::jlimit(0.0f, 1.0f, newValue);
    if (value == newValue) return;
    value = newValue;
    if (notify != juce::dontSendNotification && onValueChange) onValueChange(value);
    repaint();
}

float ModSlider::currentModValue() const {
    if (modValueFn) return juce::jlimit(0.0f, 1.0f, modValueFn());
    float v = value;
    if (ModDest::isAudioRate(dest))
        v += matrix.uiDestOffset[(size_t)dest].load(std::memory_order_relaxed);
    return juce::jlimit(0.0f, 1.0f, v);
}

void ModSlider::timerCallback() {
    // Animate only when something is actually modulating us.
    if (dest < 0 || !matrix.destHasRoutings(dest)) return;
    const float mv = currentModValue();
    if (std::abs(mv - lastPainted) > 0.002f) repaint();
}

void ModSlider::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isRightButtonDown()) {
        ModRoutingEditor::show(matrix, dest, labelText, this);
        return;
    }
    dragStartValue = value;
}

void ModSlider::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isRightButtonDown()) return;
    const float sensitivity = e.mods.isShiftDown() ? 900.0f : 220.0f;
    setValue(dragStartValue - (float)e.getDistanceFromDragStartY() / sensitivity,
             juce::sendNotification);
}

void ModSlider::mouseDoubleClick(const juce::MouseEvent&) {
    setValue(defaultValue, juce::sendNotification);
}

bool ModSlider::isInterestedInDragSource(const SourceDetails& details) {
    // dest < 0 → plain (non-modulatable) knob
    return dest >= 0 && sourceIdFromDescription(details.description) >= 0;
}

void ModSlider::itemDropped(const SourceDetails& details) {
    dragOver = false;
    const int src = sourceIdFromDescription(details.description);
    if (src >= 0 && dest >= 0) {
        matrix.addRouting(src, dest, 0.3f);
        repaint();
    }
}

void ModSlider::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    const float labelH = 13.0f;
    const float textH  = 12.0f;
    auto knobArea = bounds.withTrimmedBottom(labelH + textH);

    const float size = juce::jmin(knobArea.getWidth(), knobArea.getHeight());
    const auto  c    = knobArea.getCentre();
    const float rOut = size * 0.5f - 2.0f;            // modulation ring radius
    const float rKnob = rOut - 5.0f;

    const bool  hasMod = matrix.destHasRoutings(dest);
    const float modVal = currentModValue();
    lastPainted = modVal;

    auto angleFor = [](float v) { return -ANGLE_RANGE + v * 2.0f * ANGLE_RANGE; };

    // Ring track
    {
        juce::Path track;
        track.addCentredArc(c.x, c.y, rOut, rOut, 0.0f, -ANGLE_RANGE, ANGLE_RANGE, true);
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.strokePath(track, juce::PathStrokeType(2.5f));
    }

    // Modulation range arc + live value dot
    if (hasMod) {
        const float range = matrix.destModRange(dest);
        const float lo = juce::jlimit(0.0f, 1.0f, value - range);
        const float hi = juce::jlimit(0.0f, 1.0f, value + range);
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, rOut, rOut, 0.0f, angleFor(lo), angleFor(hi), true);
        g.setColour(juce::Colour(0xff00c8ff).withAlpha(0.35f));
        g.strokePath(arc, juce::PathStrokeType(2.5f));

        const float a = angleFor(modVal);
        const juce::Point<float> dot(c.x + rOut * std::sin(a), c.y - rOut * std::cos(a));
        g.setColour(juce::Colour(0xff00c8ff));
        g.fillEllipse(dot.x - 3.0f, dot.y - 3.0f, 6.0f, 6.0f);
    }

    // Knob body
    g.setColour(juce::Colour(0xff262640));
    g.fillEllipse(c.x - rKnob, c.y - rKnob, rKnob * 2.0f, rKnob * 2.0f);
    g.setColour(dragOver ? juce::Colour(0xff00c8ff)
                         : juce::Colours::white.withAlpha(0.25f));
    g.drawEllipse(c.x - rKnob, c.y - rKnob, rKnob * 2.0f, rKnob * 2.0f,
                  dragOver ? 2.0f : 1.0f);

    // Value arc (base value)
    {
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, rKnob - 2.0f, rKnob - 2.0f, 0.0f,
                          -ANGLE_RANGE, angleFor(value), true);
        g.setColour(juce::Colour(0xffff9040));
        g.strokePath(arc, juce::PathStrokeType(2.0f));
    }

    // Pointer
    {
        const float a = angleFor(value);
        g.setColour(juce::Colours::white);
        g.drawLine(c.x + (rKnob * 0.35f) * std::sin(a), c.y - (rKnob * 0.35f) * std::cos(a),
                   c.x + (rKnob * 0.85f) * std::sin(a), c.y - (rKnob * 0.85f) * std::cos(a),
                   2.0f);
    }

    // Readout + label
    auto footer = getLocalBounds();
    auto labelRect = footer.removeFromBottom((int)labelH);
    auto textRect  = footer.removeFromBottom((int)textH);

    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(juce::FontOptions(10.0f));
    const auto text = textFromValue ? textFromValue(value)
                                    : juce::String((int)std::round(value * 100.0f)) + "%";
    g.drawText(text, textRect, juce::Justification::centred, false);

    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(labelText, labelRect, juce::Justification::centred, false);
}
