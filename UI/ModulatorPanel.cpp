#include "ModulatorPanel.h"

// ── Value mappings for knobs (normalized 0..1 ↔ real units) ──────────────────
static float hzFromNorm (float n) { return 0.02f * std::pow(2000.0f, n); }          // 0.02..40 Hz
static float normFromHz (float h) { return std::log(juce::jmax(0.02f, h) / 0.02f) / std::log(2000.0f); }
static float secFromNorm(float n) { return 0.001f * std::pow(10000.0f, n); }        // 1 ms..10 s
static float normFromSec(float s) { return std::log(juce::jmax(0.001f, s) / 0.001f) / std::log(10000.0f); }

static juce::String hzText (float n) { return juce::String(hzFromNorm(n), 2) + " Hz"; }
static juce::String secText(float n) {
    const float s = secFromNorm(n);
    return s < 1.0f ? juce::String((int)std::round(s * 1000.0f)) + " ms"
                    : juce::String(s, 2) + " s";
}
static juce::String bipText(float n) { return juce::String(n * 2.0f - 1.0f, 2); }

// ── SourceHandle ─────────────────────────────────────────────────────────────

void ModulatorPanel::SourceHandle::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(2.0f, 1.0f);
    const bool isSelected = panel.selected == sourceId;
    const bool isLfo      = ModSrc::isLfo(sourceId);

    const bool active = isLfo
        ? panel.matrix.lfoParams[(size_t)sourceId].active.load()
        : panel.matrix.envParams[(size_t)(sourceId - ModSrc::EnvFirst)].active.load();

    g.setColour(isSelected ? juce::Colour(0xff2a2a4a) : juce::Colour(0xff1c1c30));
    g.fillRoundedRectangle(r, 4.0f);
    if (isSelected) {
        g.setColour(juce::Colour(0xff00c8ff));
        g.drawRoundedRectangle(r, 4.0f, 1.2f);
    }

    // Active dot
    g.setColour(active ? juce::Colours::limegreen : juce::Colours::white.withAlpha(0.2f));
    g.fillEllipse(r.getX() + 4.0f, r.getCentreY() - 2.5f, 5.0f, 5.0f);

    // Name
    g.setColour(juce::Colours::white.withAlpha(isSelected ? 0.95f : 0.7f));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(ModMatrix::sourceName(sourceId),
               (int)r.getX() + 12, (int)r.getY(), (int)r.getWidth() - 14, (int)r.getHeight() - 5,
               juce::Justification::centredLeft, false);

    // Live value meter along the bottom
    const float raw = panel.matrix.uiSourceValue[(size_t)sourceId].load(std::memory_order_relaxed);
    const float uni = isLfo ? raw * 0.5f + 0.5f : raw;
    auto meter = r.removeFromBottom(3.0f).reduced(3.0f, 0.0f);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRect(meter);
    g.setColour(juce::Colour(isLfo ? 0xff00c8ff : 0xffff9040).withAlpha(active ? 0.9f : 0.25f));
    g.fillRect(meter.removeFromLeft(meter.getWidth() * juce::jlimit(0.0f, 1.0f, uni)));
}

void ModulatorPanel::SourceHandle::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging && e.getDistanceFromDragStart() > 5) {
        dragging = true;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            container->startDragging(ModSlider::dragDescriptionFor(sourceId), this);
    }
}

// ── WaveView ─────────────────────────────────────────────────────────────────

void ModulatorPanel::WaveView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff10101e));
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRect(getLocalBounds());

    const auto r = getLocalBounds().toFloat().reduced(6.0f);
    const int   shape    = params.shape.load();
    const int   playMode = params.playMode.load();
    // Effective (post-meta-modulation) values so LFO→LFO routing is visible.
    const float phOff    = params.uiEffPhaseOffset.load();
    const float pw       = params.uiEffPulseWidth.load();
    const float amt      = params.uiEffAmount.load();

    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawHorizontalLine((int)r.getCentreY(), r.getX(), r.getRight());

    auto toY = [&r](float v) { return r.getCentreY() - v * r.getHeight() * 0.48f; };

    // Waveform (exactly what the audio thread evaluates)
    juce::Path p;
    const int steps = juce::jmax(64, (int)r.getWidth());
    for (int i = 0; i <= steps; ++i) {
        const double pos = (double)i / (double)steps;
        const float  v   = XLfo::evalAt(shape, playMode, pos, phOff, pw, &params.curve, lfoIndex) * amt;
        const juce::Point<float> pt(r.getX() + (float)i / (float)steps * r.getWidth(), toY(v));
        if (i == 0) p.startNewSubPath(pt); else p.lineTo(pt);
    }
    g.setColour(juce::Colour(0xff00c8ff));
    g.strokePath(p, juce::PathStrokeType(1.8f));

    // Playhead + live dot
    const float ph = params.uiPhase.load();
    const float x  = r.getX() + juce::jlimit(0.0f, 1.0f, ph) * r.getWidth();
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawLine(x, r.getY(), x, r.getBottom(), 1.0f);

    g.setColour(juce::Colours::white);
    g.fillEllipse(x - 3.0f, toY(params.uiValue.load()) - 3.0f, 6.0f, 6.0f);
}

// ── XLfoEditor ───────────────────────────────────────────────────────────────

ModulatorPanel::XLfoEditor::XLfoEditor(ModMatrix& m, int idx)
    : matrix(m), params(m.lfoParams[(size_t)idx]), index(idx)
{
    activeToggle.onStateChange = [this] { params.active.store(activeToggle.getToggleState()); };
    addAndMakeVisible(activeToggle);

    const char* shapes[] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square",
                             "Pulse", "S&H", "Smooth Rnd", "Custom" };
    for (int i = 0; i < XLfoParams::NumShapes; ++i) shapeBox.addItem(shapes[i], i + 1);
    shapeBox.onChange = [this] {
        params.shape.store(shapeBox.getSelectedId() - 1);
        updateShapeView();
    };
    addAndMakeVisible(shapeBox);

    const char* plays[] = { "Loop", "One-Shot", "Ping-Pong", "Reverse" };
    for (int i = 0; i < XLfoParams::NumPlayModes; ++i) playBox.addItem(plays[i], i + 1);
    playBox.onChange = [this] { params.playMode.store(playBox.getSelectedId() - 1); };
    addAndMakeVisible(playBox);

    const char* trigs[] = { "Free", "Transport", "MIDI" };
    for (int i = 0; i < XLfoParams::NumTrigModes; ++i) trigBox.addItem(trigs[i], i + 1);
    trigBox.onChange = [this] { params.trigMode.store(trigBox.getSelectedId() - 1); };
    addAndMakeVisible(trigBox);

    syncToggle.onStateChange = [this] { params.tempoSync.store(syncToggle.getToggleState()); };
    addAndMakeVisible(syncToggle);

    const auto& divs = syncDivisions();
    for (int i = 0; i < (int)divs.size(); ++i) divBox.addItem(divs[(size_t)i].name, i + 1);
    divBox.onChange = [this] { params.syncDiv.store(divBox.getSelectedId() - 1); };
    addAndMakeVisible(divBox);

    // Rate & Amount are themselves modulation destinations (drag a modulator
    // onto them for LFO→LFO modulation).
    rateKnob = std::make_unique<ModSlider>(matrix, ModDest::lfoRate(index), "Rate");
    rateKnob->textFromValue = hzText;
    rateKnob->onValueChange = [this](float v) { params.rateHz.store(hzFromNorm(v)); };
    rateKnob->modValueFn    = [this] { return normFromHz(params.uiEffRateHz.load()); };
    addAndMakeVisible(*rateKnob);

    amountKnob = std::make_unique<ModSlider>(matrix, ModDest::lfoAmt(index), "Amount");
    amountKnob->onValueChange = [this](float v) { params.amount.store(v); };
    amountKnob->modValueFn    = [this] { return params.uiEffAmount.load(); };
    addAndMakeVisible(*amountKnob);

    // Phase and Width are modulation destinations too (drop another XLFO on them
    // to modulate this one's parameters).
    phaseKnob = std::make_unique<ModSlider>(matrix, ModDest::lfoPhase(index), "Phase");
    phaseKnob->onValueChange = [this](float v) { params.phaseOffset.store(v); };
    phaseKnob->modValueFn    = [this] { return params.uiEffPhaseOffset.load(); };
    addAndMakeVisible(*phaseKnob);

    pwKnob = std::make_unique<ModSlider>(matrix, ModDest::lfoPw(index), "Width");
    pwKnob->onValueChange = [this](float v) { params.pulseWidth.store(0.05f + 0.9f * v); };
    pwKnob->textFromValue = [](float v) { return juce::String((int)((0.05f + 0.9f * v) * 100)) + "%"; };
    pwKnob->modValueFn    = [this] { return (params.uiEffPulseWidth.load() - 0.05f) / 0.9f; };
    addAndMakeVisible(*pwKnob);

    auto plainKnob = [this](std::unique_ptr<ModSlider>& knob, const char* name,
                            std::function<void(float)> setter,
                            std::function<juce::String(float)> text) {
        knob = std::make_unique<ModSlider>(matrix, -1, name);
        knob->onValueChange = std::move(setter);
        if (text) knob->textFromValue = std::move(text);
        addAndMakeVisible(*knob);
    };
    plainKnob(fadeKnob,   "Fade",   [this](float v) { params.fadeInSec.store(10.0f * v * v); },
              [](float v) { return juce::String(10.0f * v * v, 2) + " s"; });
    plainKnob(smoothKnob, "Smooth", [this](float v) { params.smoothMs.store(500.0f * v * v); },
              [](float v) { return juce::String((int)(500.0f * v * v)) + " ms"; });

    waveView = std::make_unique<WaveView>(params, index);
    addChildComponent(*waveView);

    curveEditor = std::make_unique<CurveEditor>(params.curve);
    curveEditor->setBipolarDisplay(true);
    curveEditor->playheadFn = [this] { return params.uiPhase.load(); };
    addChildComponent(*curveEditor);

    syncFromState();
}

void ModulatorPanel::XLfoEditor::syncFromState() {
    activeToggle.setToggleState(params.active.load(), juce::dontSendNotification);
    shapeBox .setSelectedId(params.shape.load() + 1,    juce::dontSendNotification);
    playBox  .setSelectedId(params.playMode.load() + 1, juce::dontSendNotification);
    trigBox  .setSelectedId(params.trigMode.load() + 1, juce::dontSendNotification);
    syncToggle.setToggleState(params.tempoSync.load(),  juce::dontSendNotification);
    divBox   .setSelectedId(params.syncDiv.load() + 1,  juce::dontSendNotification);

    rateKnob  ->setValue(normFromHz(params.rateHz.load()), juce::dontSendNotification);
    amountKnob->setValue(params.amount.load(),             juce::dontSendNotification);
    phaseKnob ->setValue(params.phaseOffset.load(),        juce::dontSendNotification);
    fadeKnob  ->setValue(std::sqrt(params.fadeInSec.load() / 10.0f),  juce::dontSendNotification);
    smoothKnob->setValue(std::sqrt(params.smoothMs.load() / 500.0f),  juce::dontSendNotification);
    pwKnob    ->setValue((params.pulseWidth.load() - 0.05f) / 0.9f,   juce::dontSendNotification);
    updateShapeView();
}

void ModulatorPanel::XLfoEditor::updateShapeView() {
    const bool custom = params.shape.load() == XLfoParams::Custom;
    waveView   ->setVisible(!custom);
    curveEditor->setVisible(custom);
}

void ModulatorPanel::XLfoEditor::resized() {
    auto r = getLocalBounds().reduced(6);

    auto row = r.removeFromTop(24);
    activeToggle.setBounds(row.removeFromLeft(44));
    shapeBox    .setBounds(row.removeFromLeft(96).reduced(2, 0));
    playBox     .setBounds(row.removeFromLeft(90).reduced(2, 0));
    trigBox     .setBounds(row.removeFromLeft(88).reduced(2, 0));
    syncToggle  .setBounds(row.removeFromLeft(54));
    divBox      .setBounds(row.removeFromLeft(70).reduced(2, 0));

    r.removeFromTop(4);
    auto knobs = r.removeFromLeft(250);
    const int kw = knobs.getWidth() / 4, kh = knobs.getHeight() / 2;
    ModSlider* grid[8] = { rateKnob.get(), amountKnob.get(), phaseKnob.get(), pwKnob.get(),
                           fadeKnob.get(), smoothKnob.get(), nullptr, nullptr };
    for (int i = 0; i < 8; ++i)
        if (grid[i] != nullptr)
            grid[i]->setBounds(knobs.getX() + (i % 4) * kw, knobs.getY() + (i / 4) * kh, kw, kh);

    auto view = r.reduced(6, 0);
    waveView   ->setBounds(view);
    curveEditor->setBounds(view);
}

// ── FollowerView ─────────────────────────────────────────────────────────────
// Scrolls the follower's output value across the panel — a live picture of the
// loudness contour it has extracted from the input.

void ModulatorPanel::FollowerView::timerCallback() {
    history[(size_t)writePos] = juce::jlimit(0.0f, 1.0f, params.uiValue.load());
    writePos = (writePos + 1) % HistorySize;
    repaint();
}

void ModulatorPanel::FollowerView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff10101e));
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRect(getLocalBounds());

    const auto r = getLocalBounds().toFloat().reduced(6.0f);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    for (int i = 1; i < 4; ++i) {
        const float y = r.getY() + r.getHeight() * (float)i / 4.0f;
        g.drawHorizontalLine((int)y, r.getX(), r.getRight());
    }

    juce::Path p;
    for (int i = 0; i < HistorySize; ++i) {
        const int idx = (writePos + i) % HistorySize;
        const float x = r.getX() + r.getWidth() * (float)i / (float)(HistorySize - 1);
        const float y = r.getBottom() - history[(size_t)idx] * r.getHeight();
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    juce::Path fill(p);
    fill.lineTo(r.getRight(), r.getBottom());
    fill.lineTo(r.getX(),     r.getBottom());
    fill.closeSubPath();
    g.setColour(juce::Colour(0xffff9040).withAlpha(0.12f));
    g.fillPath(fill);
    g.setColour(juce::Colour(0xffff9040));
    g.strokePath(p, juce::PathStrokeType(1.8f));

    // Current value dot at the leading edge
    const float cur = juce::jlimit(0.0f, 1.0f, params.uiValue.load());
    g.fillEllipse(r.getRight() - 3.0f, r.getBottom() - cur * r.getHeight() - 3.0f, 6.0f, 6.0f);

    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("input loudness \xe2\x86\x92 control signal", r.toNearestInt(),
               juce::Justification::topLeft, false);
}

// ── EnvEditor ────────────────────────────────────────────────────────────────

ModulatorPanel::EnvEditor::EnvEditor(ModMatrix& m, int idx)
    : matrix(m), params(m.envParams[(size_t)idx]), index(idx)
{
    activeToggle.onStateChange = [this] { params.active.store(activeToggle.getToggleState()); };
    addAndMakeVisible(activeToggle);

    peakToggle.setClickingTogglesState(true);
    peakToggle.onClick = [this] { params.followerPeak.store(peakToggle.getToggleState()); };
    addAndMakeVisible(peakToggle);

    auto knob = [this](std::unique_ptr<ModSlider>& k, const char* name,
                       std::function<void(float)> setter,
                       std::function<juce::String(float)> text) {
        k = std::make_unique<ModSlider>(matrix, -1, name);
        k->onValueChange = std::move(setter);
        if (text) k->textFromValue = std::move(text);
        addAndMakeVisible(*k);
    };

    knob(attackKnob, "Attack", [this](float v) { params.attackSec.store(secFromNorm(v)); }, secText);
    knob(releaseKnob, "Release", [this](float v) { params.releaseSec.store(secFromNorm(v)); }, secText);
    knob(gainKnob, "Gain", [this](float v) { params.followerGain.store(std::pow(64.0f, v)); },
         [](float v) { return juce::String(std::pow(64.0f, v), 1) + "x"; });

    followerView = std::make_unique<FollowerView>(params);
    addAndMakeVisible(*followerView);

    syncFromState();
}

void ModulatorPanel::EnvEditor::syncFromState() {
    activeToggle.setToggleState(params.active.load(), juce::dontSendNotification);
    peakToggle.setToggleState(params.followerPeak.load(), juce::dontSendNotification);
    attackKnob->setValue(normFromSec(params.attackSec.load()), juce::dontSendNotification);
    releaseKnob->setValue(normFromSec(params.releaseSec.load()), juce::dontSendNotification);
    gainKnob->setValue(std::log(juce::jmax(1.0f, params.followerGain.load())) / std::log(64.0f),
                       juce::dontSendNotification);
}

void ModulatorPanel::EnvEditor::resized() {
    auto r = getLocalBounds().reduced(6);

    auto row = r.removeFromTop(24);
    activeToggle.setBounds(row.removeFromLeft(44));
    peakToggle.setBounds(row.removeFromLeft(54));

    r.removeFromTop(4);
    auto knobs = r.removeFromLeft(200);
    const int kw = knobs.getWidth() / 3, kh = knobs.getHeight();
    attackKnob->setBounds(knobs.getX(), knobs.getY(), kw, kh);
    releaseKnob->setBounds(knobs.getX() + kw, knobs.getY(), kw, kh);
    gainKnob->setBounds(knobs.getX() + 2 * kw, knobs.getY(), kw, kh);

    auto view = r.reduced(6, 0);
    followerView->setBounds(view);
}

// ── ModulatorPanel ───────────────────────────────────────────────────────────

ModulatorPanel::ModulatorPanel(ModMatrix& m) : matrix(m) {
    for (int i = 0; i < ModConst::NUM_SOURCES; ++i) {
        handles[(size_t)i] = std::make_unique<SourceHandle>(*this, i);
        addAndMakeVisible(*handles[(size_t)i]);
    }
    for (int i = 0; i < ModConst::NUM_XLFOS; ++i) {
        lfoEditors[(size_t)i] = std::make_unique<XLfoEditor>(matrix, i);
        addChildComponent(*lfoEditors[(size_t)i]);
    }
    for (int i = 0; i < ModConst::NUM_ENVS; ++i) {
        envEditors[(size_t)i] = std::make_unique<EnvEditor>(matrix, i);
        addChildComponent(*envEditors[(size_t)i]);
    }
    select(0);
    startTimerHz(20);
}

void ModulatorPanel::select(int sourceId) {
    selected = sourceId;
    for (int i = 0; i < ModConst::NUM_XLFOS; ++i)
        lfoEditors[(size_t)i]->setVisible(selected == i);
    for (int i = 0; i < ModConst::NUM_ENVS; ++i)
        envEditors[(size_t)i]->setVisible(selected == ModSrc::EnvFirst + i);

    if (ModSrc::isLfo(selected)) lfoEditors[(size_t)selected]->syncFromState();
    else                         envEditors[(size_t)(selected - ModSrc::EnvFirst)]->syncFromState();
    repaint();
}

void ModulatorPanel::timerCallback() {
    for (auto& h : handles) h->repaint();   // live meters + active dots
}

void ModulatorPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff12121f));
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());

    g.setColour(juce::Colours::grey.withAlpha(0.6f));
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("Drag a modulator onto any knob to route it  |  Right-click a knob to edit its modulations",
               104, getHeight() - 14, getWidth() - 110, 12,
               juce::Justification::centredLeft, false);
}

void ModulatorPanel::resized() {
    auto r = getLocalBounds();
    auto left = r.removeFromLeft(100).reduced(2);
    const int hh = left.getHeight() / ModConst::NUM_SOURCES;
    for (int i = 0; i < ModConst::NUM_SOURCES; ++i)
        handles[(size_t)i]->setBounds(left.removeFromTop(hh));

    r.removeFromBottom(14);   // hint text
    for (auto& e : lfoEditors) e->setBounds(r);
    for (auto& e : envEditors) e->setBounds(r);
}
