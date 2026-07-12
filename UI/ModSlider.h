#pragma once
#include <JuceHeader.h>
#include "../Modulation/ModMatrix.h"

// ── ModSlider ────────────────────────────────────────────────────────────────
// A rotary control bound to a modulation destination. This is the whole
// "implicit matrix" surface:
//   • drop a modulator's drag handle on it → routing created
//   • animated ring shows modulation range + the live modulated value
//   • right-click → ModRoutingEditor call-out (amount / polarity / remove)
//   • drag vertically to set the base value; double-click resets
//
// Values are normalized 0..1 internally; textFromValue formats the readout.
class ModSlider : public juce::Component,
                  public juce::DragAndDropTarget,
                  private juce::Timer {
public:
    ModSlider(ModMatrix& matrix, int destId, const juce::String& label);

    void  setValue(float newValue, juce::NotificationType notify);
    float getValue() const { return value; }

    void setLabel(const juce::String& text) { labelText = text; repaint(); }
    void setDefaultValue(float v)           { defaultValue = v; }

    std::function<void(float)>         onValueChange;
    std::function<juce::String(float)> textFromValue;
    // Optional override for the live modulated value (normalized). Defaults to
    // base value + the matrix's audio-rate offset for this destination.
    std::function<float()>             modValueFn;

    // Component
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails&) override { dragOver = true;  repaint(); }
    void itemDragExit (const SourceDetails&) override { dragOver = false; repaint(); }

    // Drag payload helpers shared by every drop target
    static juce::String dragDescriptionFor(int sourceId) { return "modsrc:" + juce::String(sourceId); }
    static int sourceIdFromDescription(const juce::var& desc);   // -1 if not a mod drag

private:
    void timerCallback() override;
    float currentModValue() const;

    ModMatrix&   matrix;
    int          dest;
    juce::String labelText;

    float value        = 0.5f;
    float defaultValue = 0.5f;
    float dragStartValue = 0.5f;
    bool  dragOver     = false;
    float lastPainted  = -1.0f;
};
