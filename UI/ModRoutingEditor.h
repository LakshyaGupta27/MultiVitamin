#pragma once
#include <JuceHeader.h>
#include "../Modulation/ModMatrix.h"

// ── ModRoutingEditor ─────────────────────────────────────────────────────────
// The matrix has no page of its own: right-clicking any modulated control
// opens this small call-out listing the routings that land on it, each with
// amount / polarity / invert / remove. Shown via ModRoutingEditor::show().
class ModRoutingEditor : public juce::Component {
public:
    ModRoutingEditor(ModMatrix& matrix, int destId, const juce::String& destName);

    // Opens the editor as a CallOutBox anchored to `anchor`. No-op when the
    // destination has no routings.
    static void show(ModMatrix& matrix, int destId, const juce::String& destName,
                     juce::Component* anchor);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Row {
        juce::Label      name;
        juce::Slider     amount;
        juce::TextButton bipolar { juce::CharPointer_UTF8("\xc2\xb1") };
        juce::TextButton invert  { "Inv" };
        juce::TextButton remove  { "X" };
        int routingIndex = -1;
    };

    void rebuild();

    ModMatrix&   matrix;
    int          dest;
    juce::String destName;
    juce::Label  title;
    std::vector<std::unique_ptr<Row>> rows;
};
