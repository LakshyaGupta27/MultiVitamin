#pragma once
#include <JuceHeader.h>
#include "../Modulation/ModMatrix.h"
#include "ModSlider.h"
#include "CurveEditor.h"

// ── ModulatorPanel ───────────────────────────────────────────────────────────
// Bottom strip of the editor. Left: one drag handle per modulation source
// (XLFO 1-8, ENV 1-4) with a live value meter — click to select, drag onto any
// mod-aware knob (including knobs inside this panel) to create a routing.
// Right: the selected source's editor with live waveform / envelope display.
class ModulatorPanel : public juce::Component, private juce::Timer {
public:
    explicit ModulatorPanel(ModMatrix& matrix);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void select(int sourceId);

    // ── Source drag handle ───────────────────────────────────────────────────
    struct SourceHandle : juce::Component {
        SourceHandle(ModulatorPanel& p, int id) : panel(p), sourceId(id) {}
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent&) override { panel.select(sourceId); }
        void mouseDrag(const juce::MouseEvent& e) override;
        ModulatorPanel& panel;
        int  sourceId;
        bool dragging = false;
    };

    // ── XLFO waveform display (non-custom shapes) ────────────────────────────
    struct WaveView : juce::Component, private juce::Timer {
        WaveView(XLfoParams& p, int index) : params(p), lfoIndex(index) { startTimerHz(30); }
        void paint(juce::Graphics& g) override;
        void timerCallback() override { repaint(); }
        XLfoParams& params;
        int lfoIndex;
    };

    // ── Per-XLFO editor ──────────────────────────────────────────────────────
    struct XLfoEditor : juce::Component {
        XLfoEditor(ModMatrix& m, int index);
        void resized() override;
        void syncFromState();
        void updateShapeView();

        ModMatrix& matrix;
        XLfoParams& params;
        int index;

        juce::ToggleButton activeToggle { "On" };
        juce::ComboBox shapeBox, playBox, trigBox, divBox;
        juce::ToggleButton syncToggle { "Sync" };
        std::unique_ptr<ModSlider> rateKnob, amountKnob,
                                   phaseKnob, fadeKnob, smoothKnob, pwKnob;
        std::unique_ptr<WaveView>    waveView;
        std::unique_ptr<CurveEditor> curveEditor;
    };

    // ── Envelope-follower contour display (scrolling output history) ─────────
    struct FollowerView : juce::Component, private juce::Timer {
        explicit FollowerView(EnvParams& p) : params(p) { history.fill(0.0f); startTimerHz(30); }
        void paint(juce::Graphics& g) override;
        void timerCallback() override;
        EnvParams& params;
        static constexpr int HistorySize = 160;
        std::array<float, HistorySize> history {};
        int writePos = 0;
    };

    // ── Per-envelope (follower) editor ───────────────────────────────────────
    struct EnvEditor : juce::Component {
        EnvEditor(ModMatrix& m, int index);
        void resized() override;
        void syncFromState();

        ModMatrix& matrix;
        EnvParams& params;
        int index;

        juce::ToggleButton activeToggle { "On" };
        juce::ToggleButton peakToggle   { "Peak" };
        std::unique_ptr<ModSlider> attackKnob, releaseKnob, gainKnob;
        std::unique_ptr<FollowerView> followerView;
    };

    ModMatrix& matrix;
    std::array<std::unique_ptr<SourceHandle>, ModConst::NUM_SOURCES> handles;
    std::array<std::unique_ptr<XLfoEditor>,   ModConst::NUM_XLFOS>   lfoEditors;
    std::array<std::unique_ptr<EnvEditor>,    ModConst::NUM_ENVS>    envEditors;
    int selected = 0;
};
