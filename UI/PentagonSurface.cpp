#include "PentagonSurface.h"
#include "ModSlider.h"
#include "ModRoutingEditor.h"
#include <cmath>
#include <limits>

static const FxNode::Type NODE_DEFAULTS[DspEngine::NUM_NODES] = {
    FxNode::Type::Granular, FxNode::Type::Distortion, FxNode::Type::Delay,
    FxNode::Type::Reverb, FxNode::Type::Filter,
};

PentagonSurface::PentagonSurface(DspEngine& engine) : dspEngine(engine) {
    for (int i = 0; i < N; ++i)
        nodeLabels[i] = FxNode::typeName(dspEngine.getNodeType(i));
    setSize(600, 600);
    startTimerHz(60);
}

// ── Geometry ─────────────────────────────────────────────────────────────────

std::array<juce::Point<float>, DspEngine::NUM_NODES> PentagonSurface::getVertices() const {
    const float cx = getWidth()  * 0.5f;
    const float cy = getHeight() * 0.5f;
    const float r  = std::min(cx, cy) * 0.72f;
    std::array<juce::Point<float>, N> v;
    for (int i = 0; i < N; ++i) {
        const float angle = -juce::MathConstants<float>::halfPi
                          + juce::MathConstants<float>::twoPi * i / N;
        v[i] = { cx + r * std::cos(angle), cy + r * std::sin(angle) };
    }
    return v;
}

std::array<float, DspEngine::NUM_NODES> PentagonSurface::computeWeights(juce::Point<float> p) const {
    auto v = getVertices();
    std::array<float, N> w;
    float total = 0.f;
    for (int i = 0; i < N; ++i) {
        w[i] = 1.0f / (p.getDistanceSquaredFrom(v[i]) + 1.0f);
        total += w[i];
    }
    for (float& wi : w) wi /= total;
    return w;
}

// ── Pentagon containment ──────────────────────────────────────────────────────

// Vertices are in clockwise screen order; interior points give cross > 0 for each edge.
bool PentagonSurface::isInsidePentagon(juce::Point<float> p) const {
    auto v = getVertices();
    for (int i = 0; i < N; ++i) {
        const auto& a = v[i];
        const auto& b = v[(i + 1) % N];
        const float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (cross < 0.0f) return false;
    }
    return true;
}

juce::Point<float> PentagonSurface::nearestOnSegment(juce::Point<float> p,
                                                       juce::Point<float> a,
                                                       juce::Point<float> b) {
    const float abx = b.x - a.x, aby = b.y - a.y;
    const float apx = p.x - a.x, apy = p.y - a.y;
    const float denom = abx * abx + aby * aby;
    if (denom < 1e-8f) return a;
    const float t = juce::jlimit(0.0f, 1.0f, (apx * abx + apy * aby) / denom);
    return { a.x + t * abx, a.y + t * aby };
}

juce::Point<float> PentagonSurface::clampToPolygon(juce::Point<float> p) const {
    if (isInsidePentagon(p)) return p;
    auto v = getVertices();
    float minDist = std::numeric_limits<float>::max();
    juce::Point<float> nearest = p;
    for (int i = 0; i < N; ++i) {
        auto pt = nearestOnSegment(p, v[i], v[(i + 1) % N]);
        const float d = p.getDistanceSquaredFrom(pt);
        if (d < minDist) { minDist = d; nearest = pt; }
    }
    return nearest;
}

// ── Effective cursor (LFO-driven) ─────────────────────────────────────────────

// Weighted centroid of effectiveWeights — always inside the pentagon
// because it is a convex combination of the five vertices.
juce::Point<float> PentagonSurface::effectiveCursorPos() const {
    auto v = getVertices();
    float x = 0.f, y = 0.f;
    for (int i = 0; i < N; ++i) {
        const float w = dspEngine.effectiveWeights[i].load(std::memory_order_relaxed);
        x += w * v[i].x;
        y += w * v[i].y;
    }
    return { x, y };
}

// ── Input ─────────────────────────────────────────────────────────────────────

void PentagonSurface::updateWeights() {
    dspEngine.cursorRelX.store(cursor.x / (float)getWidth());
    dspEngine.cursorRelY.store(cursor.y / (float)getHeight());
    const auto w = computeWeights(cursor);
    dspEngine.setBaseWeights(w);
    if (onWeightsChanged) onWeightsChanged();
}

void PentagonSurface::resized() {
    // Restore saved cursor position; defaults to center on first load.
    cursor = { dspEngine.cursorRelX.load() * (float)getWidth(),
               dspEngine.cursorRelY.load() * (float)getHeight() };
    updateWeights();
}

int PentagonSurface::vertexAt(juce::Point<float> p) const {
    auto v = getVertices();
    for (int i = 0; i < N; ++i)
        if (p.getDistanceFrom(v[i]) < 40.0f) return i;
    return -1;
}

void PentagonSurface::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isRightButtonDown()) {
        const int i = vertexAt(e.position);
        if (i >= 0) showNodeMenu(i);
        return;
    }
    cursor = clampToPolygon(e.position);
    updateWeights();
}

// ── Modulator drag-and-drop onto node circles ─────────────────────────────────

bool PentagonSurface::isInterestedInDragSource(const SourceDetails& details) {
    return ModSlider::sourceIdFromDescription(details.description) >= 0;
}

void PentagonSurface::itemDragMove(const SourceDetails& details) {
    const int v = vertexAt(details.localPosition.toFloat());
    if (v != dragOverVertex) { dragOverVertex = v; repaint(); }
}

void PentagonSurface::itemDropped(const SourceDetails& details) {
    const int src = ModSlider::sourceIdFromDescription(details.description);
    const int v   = vertexAt(details.localPosition.toFloat());
    if (src >= 0 && v >= 0)
        dspEngine.modMatrix.addRouting(src, ModDest::nodeMix(v), 0.3f);
    dragOverVertex = -1;
    repaint();
}

void PentagonSurface::mouseDrag(const juce::MouseEvent& e) {
    cursor = clampToPolygon(e.position);
    updateWeights();
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void PentagonSurface::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a2e));

    auto v = getVertices();
    const auto effCursor = effectiveCursorPos();  // LFO-driven position
    const auto baseWeights = computeWeights(cursor);

    // Pentagon outline
    juce::Path pentagon;
    pentagon.startNewSubPath(v[0]);
    for (int i = 1; i < N; ++i) pentagon.lineTo(v[i]);
    pentagon.closeSubPath();
    g.setColour(juce::Colour(0xff334466));
    g.strokePath(pentagon, juce::PathStrokeType(2.0f));

    // Spokes from effective cursor to vertices, width driven by effective weight
    for (int i = 0; i < N; ++i) {
        const float w = dspEngine.effectiveWeights[i].load(std::memory_order_relaxed);
        g.setColour(juce::Colour::fromHSV((float)i / N, 0.8f, 0.9f, 0.15f + w * 0.85f));
        g.drawLine(effCursor.x, effCursor.y, v[i].x, v[i].y, 1.5f + w * 4.5f);
    }

    // Node circles at vertices
    const float nodeR = 28.0f;
    for (int i = 0; i < N; ++i) {
        const float w = dspEngine.effectiveWeights[i].load(std::memory_order_relaxed);
        juce::Colour col = juce::Colour::fromHSV((float)i / N, 0.7f, 0.8f, 1.0f);
        g.setColour(col.withAlpha(0.25f + w * 0.75f));
        g.fillEllipse(v[i].x - nodeR, v[i].y - nodeR, nodeR * 2.f, nodeR * 2.f);
        g.setColour(col);
        g.drawEllipse(v[i].x - nodeR, v[i].y - nodeR, nodeR * 2.f, nodeR * 2.f, 1.5f);

        // Modulation ring: shows that this node's mix weight is a routing
        // target, plus the live offset as an arc (cw = positive, ccw = negative).
        if (dspEngine.modMatrix.destHasRoutings(ModDest::nodeMix(i))) {
            const float ringR = nodeR + 5.0f;
            g.setColour(juce::Colour(0xff00c8ff).withAlpha(0.30f));
            g.drawEllipse(v[i].x - ringR, v[i].y - ringR, ringR * 2.f, ringR * 2.f, 1.2f);

            const float off = dspEngine.modMatrix.uiDestOffset[(size_t)ModDest::nodeMix(i)]
                                  .load(std::memory_order_relaxed);
            if (std::abs(off) > 1e-3f) {
                juce::Path arc;
                arc.addCentredArc(v[i].x, v[i].y, ringR, ringR, 0.0f,
                                  0.0f, juce::jlimit(-1.0f, 1.0f, off)
                                            * juce::MathConstants<float>::twoPi, true);
                g.setColour(juce::Colour(0xff00c8ff));
                g.strokePath(arc, juce::PathStrokeType(2.2f));
            }
        }

        // Highlight while a modulator hovers over this vertex
        if (dragOverVertex == i) {
            const float hiR = nodeR + 9.0f;
            g.setColour(juce::Colour(0xff00c8ff));
            g.drawEllipse(v[i].x - hiR, v[i].y - hiR, hiR * 2.f, hiR * 2.f, 2.0f);
        }

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("[" + juce::String(i) + "] " + nodeLabels[i],
                   (int)(v[i].x - 40), (int)(v[i].y - 8), 80, 16,
                   juce::Justification::centred, false);
        g.setColour(juce::Colours::white.withAlpha(0.65f));
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(juce::String((int)(w * 100)) + "%",
                   (int)(v[i].x - 20), (int)(v[i].y + 10), 40, 12,
                   juce::Justification::centred, false);
    }

    // Ghost anchor (where user dragged)
    const float ghostR = 6.0f;
    g.setColour(juce::Colours::white.withAlpha(0.25f));
    g.fillEllipse(cursor.x - ghostR, cursor.y - ghostR, ghostR * 2.f, ghostR * 2.f);
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.drawEllipse(cursor.x - ghostR, cursor.y - ghostR, ghostR * 2.f, ghostR * 2.f, 1.0f);

    // Effective dot (LFO-animated, always inside pentagon)
    const float dotR = 13.0f;
    g.setColour(juce::Colours::cyan.withAlpha(0.9f));
    g.fillEllipse(effCursor.x - dotR, effCursor.y - dotR, dotR * 2.f, dotR * 2.f);
    g.setColour(juce::Colours::white);
    g.drawEllipse(effCursor.x - dotR, effCursor.y - dotR, dotR * 2.f, dotR * 2.f, 2.0f);

    // Hint
    g.setColour(juce::Colours::grey.withAlpha(0.6f));
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("Drag to mix  |  Right-click node for FX type & modulations  |  Drop a modulator on a node to modulate its mix",
               0, getHeight() - 16, getWidth(), 14,
               juce::Justification::centred, false);
}

// ── Node type menu ────────────────────────────────────────────────────────────

void PentagonSurface::showNodeMenu(int nodeIndex) {
    juce::PopupMenu menu;
    auto cur = dspEngine.getNodeType(nodeIndex);
    menu.addItem(1, "Granular",   true, cur == FxNode::Type::Granular);
    menu.addItem(2, "Distortion", true, cur == FxNode::Type::Distortion);
    menu.addItem(3, "Delay",      true, cur == FxNode::Type::Delay);
    menu.addItem(4, "Reverb",     true, cur == FxNode::Type::Reverb);
    menu.addItem(5, "Filter",     true, cur == FxNode::Type::Filter);

    if (dspEngine.modMatrix.destHasRoutings(ModDest::nodeMix(nodeIndex))) {
        menu.addSeparator();
        menu.addItem(100, "Edit Modulations...");
    }

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, nodeIndex](int result) {
        if (result == 0) return;
        if (result == 100) {
            ModRoutingEditor::show(dspEngine.modMatrix, ModDest::nodeMix(nodeIndex),
                                   "Node " + juce::String(nodeIndex + 1) + " Mix", this);
            return;
        }
        FxNode::Type t = FxNode::Type::Granular;
        switch (result) {
            case 2: t = FxNode::Type::Distortion; break;
            case 3: t = FxNode::Type::Delay;      break;
            case 4: t = FxNode::Type::Reverb;     break;
            case 5: t = FxNode::Type::Filter;     break;
            default: break;
        }
        dspEngine.setNodeType(nodeIndex, t);
        nodeLabels[nodeIndex] = FxNode::typeName(t);
        repaint();
    });
}
