#include "ModCurve.h"

void ModCurve::setPreset(Preset p) {
    switch (p) {
        case Preset::RampUp:
            numPoints = 2;
            points[0] = { 0.0f, 0.0f, 0.0f };
            points[1] = { 1.0f, 1.0f, 0.0f };
            break;
        case Preset::RampDown:
            numPoints = 2;
            points[0] = { 0.0f, 1.0f, 0.0f };
            points[1] = { 1.0f, 0.0f, 0.0f };
            break;
        case Preset::Triangle:
            numPoints = 3;
            points[0] = { 0.0f, 0.0f, 0.0f };
            points[1] = { 0.5f, 1.0f, 0.0f };
            points[2] = { 1.0f, 0.0f, 0.0f };
            break;
        case Preset::Adsr:
            numPoints = 4;
            points[0] = { 0.0f,  0.0f, -0.4f };
            points[1] = { 0.15f, 1.0f,  0.4f };
            points[2] = { 0.4f,  0.6f,  0.0f };
            points[3] = { 1.0f,  0.0f,  0.0f };
            break;
    }
    bake();
}

void ModCurve::setPoint(int i, float x, float y) {
    if (i < 0 || i >= numPoints) return;
    // Endpoints are pinned to the domain edges; interior points stay ordered.
    if (i == 0)                  x = 0.0f;
    else if (i == numPoints - 1) x = 1.0f;
    else x = juce::jlimit(points[(size_t)i - 1].x + 0.001f,
                          points[(size_t)i + 1].x - 0.001f, x);
    points[(size_t)i].x = x;
    points[(size_t)i].y = juce::jlimit(0.0f, 1.0f, y);
    bake();
}

void ModCurve::setCurvature(int i, float c) {
    if (i < 0 || i >= numPoints - 1) return;
    points[(size_t)i].curve = juce::jlimit(-1.0f, 1.0f, c);
    bake();
}

int ModCurve::addPoint(float x, float y) {
    if (numPoints >= MaxPoints) return -1;
    x = juce::jlimit(0.001f, 0.999f, x);

    int insertAt = numPoints;
    for (int i = 0; i < numPoints; ++i)
        if (points[(size_t)i].x > x) { insertAt = i; break; }

    for (int i = numPoints; i > insertAt; --i)
        points[(size_t)i] = points[(size_t)i - 1];

    points[(size_t)insertAt] = { x, juce::jlimit(0.0f, 1.0f, y), 0.0f };
    ++numPoints;
    bake();
    return insertAt;
}

void ModCurve::removePoint(int i) {
    if (i <= 0 || i >= numPoints - 1) return;  // keep endpoints
    for (int j = i; j < numPoints - 1; ++j)
        points[(size_t)j] = points[(size_t)j + 1];
    --numPoints;
    bake();
}

void ModCurve::bake() {
    const int which = 1 - activeTable.load(std::memory_order_relaxed);
    auto& lut = tables[(size_t)which];

    int seg = 0;
    for (int i = 0; i < LutSize; ++i) {
        const float x = (float)i / (float)(LutSize - 1);
        while (seg < numPoints - 2 && x > points[(size_t)seg + 1].x) ++seg;

        const auto& a = points[(size_t)seg];
        const auto& b = points[(size_t)juce::jmin(seg + 1, numPoints - 1)];
        const float span = juce::jmax(1e-6f, b.x - a.x);
        const float t    = shapeSegment((x - a.x) / span, a.curve);
        lut[(size_t)i]   = a.y + (b.y - a.y) * t;
    }

    activeTable.store(which, std::memory_order_release);
}

void ModCurve::toXml(juce::XmlElement& parent, const char* tag) const {
    auto* el = parent.createNewChildElement(tag);
    for (int i = 0; i < numPoints; ++i) {
        auto* pt = el->createNewChildElement("Pt");
        pt->setAttribute("x", (double)points[(size_t)i].x);
        pt->setAttribute("y", (double)points[(size_t)i].y);
        pt->setAttribute("c", (double)points[(size_t)i].curve);
    }
}

void ModCurve::fromXml(const juce::XmlElement& parent, const char* tag) {
    auto* el = parent.getChildByName(tag);
    if (el == nullptr) return;

    int n = 0;
    for (auto* pt : el->getChildWithTagNameIterator("Pt")) {
        if (n >= MaxPoints) break;
        points[(size_t)n] = { (float)pt->getDoubleAttribute("x"),
                              (float)pt->getDoubleAttribute("y"),
                              (float)pt->getDoubleAttribute("c") };
        ++n;
    }
    if (n >= 2) {
        numPoints = n;
        points[0].x = 0.0f;
        points[(size_t)numPoints - 1].x = 1.0f;
        bake();
    }
}
