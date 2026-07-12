#include "ModRoutingEditor.h"

static constexpr int ROW_H   = 30;
static constexpr int TITLE_H = 24;
static constexpr int WIDTH   = 380;

ModRoutingEditor::ModRoutingEditor(ModMatrix& m, int destId, const juce::String& dn)
    : matrix(m), dest(destId), destName(dn)
{
    title.setText("Modulations \xe2\x86\x92 " + destName, juce::dontSendNotification);
    title.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    title.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title);
    rebuild();
}

void ModRoutingEditor::show(ModMatrix& matrix, int destId, const juce::String& destName,
                            juce::Component* anchor) {
    if (!matrix.destHasRoutings(destId)) return;
    auto content = std::make_unique<ModRoutingEditor>(matrix, destId, destName);
    juce::CallOutBox::launchAsynchronously(std::move(content),
                                           anchor->getScreenBounds(), nullptr);
}

void ModRoutingEditor::rebuild() {
    rows.clear();

    std::array<int, ModConst::MAX_ROUTINGS> idx {};
    const int n = matrix.routingsForDest(dest, idx);

    for (int k = 0; k < n; ++k) {
        auto row = std::make_unique<Row>();
        row->routingIndex = idx[(size_t)k];
        auto& r = matrix.routing(row->routingIndex);

        row->name.setText(ModMatrix::sourceName(r.source.load()), juce::dontSendNotification);
        row->name.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(row->name);

        row->amount.setRange(-1.0, 1.0, 0.01);
        row->amount.setValue((double)r.amount.load(), juce::dontSendNotification);
        row->amount.setSliderStyle(juce::Slider::LinearHorizontal);
        row->amount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 16);
        row->amount.setDoubleClickReturnValue(true, 0.25);
        {
            auto* rt = &matrix.routing(row->routingIndex);
            auto* sl = &row->amount;
            row->amount.onValueChange = [rt, sl] { rt->amount.store((float)sl->getValue()); };
        }
        addAndMakeVisible(row->amount);

        auto setupToggle = [this, &row](juce::TextButton& b, std::atomic<bool>& flag) {
            b.setClickingTogglesState(true);
            b.setToggleState(flag.load(), juce::dontSendNotification);
            b.onClick = [&b, &flag] { flag.store(b.getToggleState()); };
            addAndMakeVisible(b);
        };
        setupToggle(row->bipolar, r.bipolar);
        setupToggle(row->invert,  r.invert);

        const int routingIndex = row->routingIndex;
        row->remove.onClick = [this, routingIndex] {
            matrix.removeRouting(routingIndex);
            rebuild();
            if (rows.empty())
                if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                    box->dismiss();
        };
        addAndMakeVisible(row->remove);

        rows.push_back(std::move(row));
    }

    setSize(WIDTH, TITLE_H + ROW_H * juce::jmax(1, (int)rows.size()) + 8);
    resized();
}

void ModRoutingEditor::resized() {
    auto r = getLocalBounds().reduced(4);
    title.setBounds(r.removeFromTop(TITLE_H));
    for (auto& row : rows) {
        auto line = r.removeFromTop(ROW_H).reduced(0, 3);
        row->name  .setBounds(line.removeFromLeft(64));
        row->remove.setBounds(line.removeFromRight(24));
        row->invert.setBounds(line.removeFromRight(36));
        row->bipolar.setBounds(line.removeFromRight(28));
        row->amount.setBounds(line.reduced(4, 0));
    }
}

void ModRoutingEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a2e));
}
