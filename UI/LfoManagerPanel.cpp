#include "LfoManagerPanel.h"

// ── LfoRow ───────────────────────────────────────────────────────────────────

static constexpr int N = DspEngine::NUM_NODES;
static constexpr int M = DspEngine::MAX_LFOS;

// Encodes the combo-box selection to the three target atomics.
static void decodeTarget(int sel0based,
                         DspEngine::LfoState& ls)
{
    if (sel0based < N) {                         // Node 0-4 Mix
        ls.targetType .store(0);
        ls.targetNode .store(sel0based);
        ls.targetParam.store(0);
    } else if (sel0based < N * 2) {              // Node 0-4 Param1
        ls.targetType .store(0);
        ls.targetNode .store(sel0based - N);
        ls.targetParam.store(1);
    } else if (sel0based < N * 3) {              // Node 0-4 Param2
        ls.targetType .store(0);
        ls.targetNode .store(sel0based - N * 2);
        ls.targetParam.store(2);
    } else if (sel0based < N * 3 + M) {          // LFO 1-8 Rate
        ls.targetType .store(1);
        ls.targetNode .store(sel0based - N * 3);
    } else {                                     // LFO 1-8 Depth
        ls.targetType .store(2);
        ls.targetNode .store(sel0based - N * 3 - M);
    }
}

// Encodes the three target atomics back to a 1-based combo-box ID.
static int encodeTarget(const DspEngine::LfoState& ls) {
    const int tType  = ls.targetType .load();
    const int tNode  = ls.targetNode .load();
    const int tParam = ls.targetParam.load();
    if (tType == 0) return tNode + tParam * N + 1;
    if (tType == 1) return N * 3 + tNode + 1;
    return N * 3 + M + tNode + 1;
}

LfoManagerPanel::LfoRow::LfoRow(DspEngine::LfoState& state, int index)
    : lfoState(state), lfoIndex(index)
{
    indexLabel.setText("LFO " + juce::String(index + 1), juce::dontSendNotification);
    indexLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(indexLabel);

    shapeBox.addItem("Sine",     1);
    shapeBox.addItem("Tri",      2);
    shapeBox.addItem("Saw",      3);
    shapeBox.addItem("Sqr",      4);
    shapeBox.setSelectedId(1, juce::dontSendNotification);
    shapeBox.onChange = [this] {
        lfoState.shape.store(shapeBox.getSelectedId() - 1);
    };
    addAndMakeVisible(shapeBox);

    rateSlider.setRange(0.01, 20.0, 0.01);
    rateSlider.setValue(0.5);
    rateSlider.setTextValueSuffix(" Hz");
    rateSlider.setSliderStyle(juce::Slider::LinearVertical);
    rateSlider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 60, 14);
    rateSlider.onValueChange = [this] {
        lfoState.rate.store((float)rateSlider.getValue());
    };
    rateSlider.addListener(this);
    addAndMakeVisible(rateSlider);

    depthSlider.setRange(0.0, 1.0, 0.01);
    depthSlider.setValue(0.3);
    depthSlider.setSliderStyle(juce::Slider::LinearVertical);
    depthSlider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 60, 14);
    depthSlider.onValueChange = [this] {
        lfoState.depth.store((float)depthSlider.getValue());
    };
    depthSlider.addListener(this);
    addAndMakeVisible(depthSlider);

    // Build target combo ──────────────────────────────────────────────────
    int id = 1;
    // Node targets
    for (int n = 0; n < N; ++n)
        targetBox.addItem("Nd" + juce::String(n) + " Mix", id++);
    for (int n = 0; n < N; ++n)
        targetBox.addItem("Nd" + juce::String(n) + " P1",  id++);
    for (int n = 0; n < N; ++n)
        targetBox.addItem("Nd" + juce::String(n) + " P2",  id++);
    // LFO targets
    targetBox.addSeparator();
    for (int i = 0; i < M; ++i)
        targetBox.addItem("LFO" + juce::String(i + 1) + " Rate",  id++);
    for (int i = 0; i < M; ++i)
        targetBox.addItem("LFO" + juce::String(i + 1) + " Depth", id++);

    targetBox.setSelectedId(1, juce::dontSendNotification);
    targetBox.onChange = [this] {
        decodeTarget(targetBox.getSelectedId() - 1, lfoState);
    };
    addAndMakeVisible(targetBox);

    activeToggle.setToggleState(false, juce::dontSendNotification);
    activeToggle.onStateChange = [this] {
        lfoState.active.store(activeToggle.getToggleState());
    };
    addAndMakeVisible(activeToggle);
}

void LfoManagerPanel::LfoRow::resized() {
    auto r = getLocalBounds().reduced(2);
    const int comboH  = 22;
    const int headerH = 18;

    auto header = r.removeFromTop(headerH);
    indexLabel  .setBounds(header.removeFromLeft(header.getWidth() / 2));
    activeToggle.setBounds(header);

    shapeBox .setBounds(r.removeFromTop(comboH));
    targetBox.setBounds(r.removeFromBottom(comboH));

    depthSlider.setBounds(r.removeFromRight(r.getWidth() / 2));
    rateSlider .setBounds(r);
}

void LfoManagerPanel::LfoRow::syncFromState() {
    shapeBox    .setSelectedId(lfoState.shape.load() + 1, juce::dontSendNotification);
    rateSlider  .setValue(lfoState.rate.load(),           juce::dontSendNotification);
    depthSlider .setValue(lfoState.depth.load(),          juce::dontSendNotification);
    activeToggle.setToggleState(lfoState.active.load(),   juce::dontSendNotification);
    targetBox   .setSelectedId(encodeTarget(lfoState),    juce::dontSendNotification);
}

void LfoManagerPanel::LfoRow::updateDisplay() {
    // Only reflect the audio thread's modulated value when not being dragged.
    if (!rateBeingDragged)
        rateSlider .setValue(lfoState.effectiveRate .load(), juce::dontSendNotification);
    if (!depthBeingDragged)
        depthSlider.setValue(lfoState.effectiveDepth.load(), juce::dontSendNotification);
}

void LfoManagerPanel::LfoRow::sliderDragStarted(juce::Slider* s) {
    if (s == &rateSlider)  rateBeingDragged  = true;
    if (s == &depthSlider) depthBeingDragged = true;
}

void LfoManagerPanel::LfoRow::sliderDragEnded(juce::Slider* s) {
    if (s == &rateSlider)  rateBeingDragged  = false;
    if (s == &depthSlider) depthBeingDragged = false;
}

// ── LfoManagerPanel ──────────────────────────────────────────────────────────

LfoManagerPanel::LfoManagerPanel(DspEngine& engine) : dspEngine(engine) {
    for (int i = 0; i < MAX_ROWS; ++i) {
        rows.push_back(std::make_unique<LfoRow>(dspEngine.lfoStates[i], i));
        addAndMakeVisible(*rows.back());
        rows.back()->syncFromState();
    }
    startTimerHz(30);
}

void LfoManagerPanel::timerCallback() {
    for (auto& row : rows)
        row->updateDisplay();
}

void LfoManagerPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff12121f));

    // dividers between columns
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    const int colW = getWidth() / (int)rows.size();
    for (int i = 1; i < (int)rows.size(); ++i)
        g.drawVerticalLine(i * colW, 0.0f, (float)getHeight());
}

void LfoManagerPanel::resized() {
    const int colW = getWidth() / (int)rows.size();
    int x = 0;
    for (auto& row : rows) {
        row->setBounds(x, 0, colW, getHeight());
        x += colW;
    }
}
