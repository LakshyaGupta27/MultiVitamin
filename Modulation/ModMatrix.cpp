#include "ModMatrix.h"

ModMatrix::ModMatrix() {
    for (auto& v : uiDestOffset)  v.store(0.0f);
    for (auto& v : uiSourceValue) v.store(0.0f);
}

void ModMatrix::prepare(double sr, int maxBlockSize) {
    sampleRate = sr;
    maxBlock   = juce::jmax(16, maxBlockSize);

    for (auto& b : srcBuffers)  { b.setSize(2, maxBlock); b.clear(); }
    for (auto& b : destBuffers) { b.setSize(2, maxBlock); b.clear(); }
    monoInputBuf.setSize(1, maxBlock); monoInputBuf.clear();

    for (auto& e : lfoEngines) e.prepare(sr);
    for (auto& e : envEngines) e.prepare(sr);

    for (auto& s : smoothState) s = { 0.0f, 0.0f };
    prevSourceValue.fill(0.0f);
    destInUse.fill(false);
}

// Normalize a raw source sample into the routing's chosen polarity, apply
// inversion, and scale by amount.
static inline float transformSample(float raw, bool srcBipolar,
                                    bool routeBipolar, bool invert, float amount) noexcept {
    float v;
    if (routeBipolar) {
        v = srcBipolar ? raw : raw * 2.0f - 1.0f;
        if (invert) v = -v;
    } else {
        v = srcBipolar ? raw * 0.5f + 0.5f : raw;
        if (invert) v = 1.0f - v;
    }
    return v * amount;
}

void ModMatrix::processBlock(const juce::AudioBuffer<float>& input,
                             const TransportInfo& transport,
                             const ModTriggerList& triggers, int numSamples) {
    numSamples = juce::jmin(numSamples, maxBlock);
    if (numSamples <= 0) return;

    // Sum the input to mono for any envelope followers.
    float* mono = monoInputBuf.getWritePointer(0);
    const int inCh = input.getNumChannels();
    if (inCh > 0) {
        monoInputBuf.copyFrom(0, 0, input, 0, 0, numSamples);
        for (int ch = 1; ch < inCh; ++ch)
            monoInputBuf.addFrom(0, 0, input, ch, 0, numSamples);
        if (inCh > 1) monoInputBuf.applyGain(0, 0, numSamples, 1.0f / (float)inCh);
    } else {
        monoInputBuf.clear(0, 0, numSamples);
    }

    // ── Phase 1: meta modulation of XLFO params from the previous block ──────
    std::array<XLfoMod, ModConst::NUM_XLFOS> lfoMod {};

    for (const auto& r : routings) {
        if (!r.active.load(std::memory_order_relaxed)) continue;
        const int dest = r.dest.load(std::memory_order_relaxed);
        if (!ModDest::isLfoMeta(dest)) continue;

        const int src = r.source.load(std::memory_order_relaxed);
        const float v = transformSample(prevSourceValue[(size_t)src],
                                        sourceIsBipolar(src),
                                        r.bipolar.load(std::memory_order_relaxed),
                                        r.invert .load(std::memory_order_relaxed),
                                        r.amount .load(std::memory_order_relaxed));
        if      (dest >= ModDest::LfoPwFirst)    lfoMod[(size_t)(dest - ModDest::LfoPwFirst)]   .width  += v;
        else if (dest >= ModDest::LfoPhaseFirst) lfoMod[(size_t)(dest - ModDest::LfoPhaseFirst)].phase  += v;
        else if (dest >= ModDest::LfoAmtFirst)   lfoMod[(size_t)(dest - ModDest::LfoAmtFirst)]  .amount += v;
        else                                     lfoMod[(size_t)(dest - ModDest::LfoRateFirst)] .rate   += v;
    }
    for (auto& m : lfoMod) m.rate = juce::jlimit(-1.0f, 1.0f, m.rate);

    // ── Phase 2: render every active source ──────────────────────────────────
    for (int s = 0; s < ModConst::NUM_XLFOS; ++s) {
        auto& p = lfoParams[(size_t)s];
        if (!p.active.load(std::memory_order_relaxed)) { prevSourceValue[(size_t)s] = 0.0f; continue; }
        auto* dst = srcBuffers[(size_t)s].getWritePointer(0);
        lfoEngines[(size_t)s].renderBlock(p, transport, triggers, lfoMod[(size_t)s],
                                          numSamples, dst, s);
        srcBuffers[(size_t)s].copyFrom(1, 0, srcBuffers[(size_t)s], 0, 0, numSamples);
        prevSourceValue[(size_t)s] = dst[numSamples - 1];
        uiSourceValue [(size_t)s].store(prevSourceValue[(size_t)s], std::memory_order_relaxed);
    }
    for (int e = 0; e < ModConst::NUM_ENVS; ++e) {
        const int s = ModSrc::EnvFirst + e;
        auto& p = envParams[(size_t)e];
        if (!p.active.load(std::memory_order_relaxed)) { prevSourceValue[(size_t)s] = 0.0f; continue; }
        auto* dst = srcBuffers[(size_t)s].getWritePointer(0);
        envEngines[(size_t)e].renderBlock(p, mono, numSamples, dst);
        srcBuffers[(size_t)s].copyFrom(1, 0, srcBuffers[(size_t)s], 0, 0, numSamples);
        prevSourceValue[(size_t)s] = dst[numSamples - 1];
        uiSourceValue [(size_t)s].store(prevSourceValue[(size_t)s], std::memory_order_relaxed);
    }

    // ── Phase 3: accumulate routings into destination offset buffers ─────────
    std::array<bool, ModDest::NumAudioRate> used {};

    for (int ri = 0; ri < ModConst::MAX_ROUTINGS; ++ri) {
        const auto& r = routings[(size_t)ri];
        if (!r.active.load(std::memory_order_relaxed)) continue;

        const int dest = r.dest.load(std::memory_order_relaxed);
        if (!ModDest::isAudioRate(dest)) continue;

        const int  src        = r.source.load(std::memory_order_relaxed);
        const bool srcBipolar = sourceIsBipolar(src);
        const bool srcActive  = srcBipolar
            ? lfoParams[(size_t)src].active.load(std::memory_order_relaxed)
            : envParams[(size_t)(src - ModSrc::EnvFirst)].active.load(std::memory_order_relaxed);

        const bool  routeBip = r.bipolar.load(std::memory_order_relaxed);
        const bool  inv      = r.invert .load(std::memory_order_relaxed);
        const float amount   = r.amount .load(std::memory_order_relaxed);
        const float smMs     = r.smoothMs.load(std::memory_order_relaxed);
        const float coeff    = smMs > 0.01f
            ? (float)std::exp(-1.0 / (sampleRate * (double)smMs * 0.001)) : 0.0f;

        if (!used[(size_t)dest]) {
            destBuffers[(size_t)dest].clear(0, 0, numSamples);
            destBuffers[(size_t)dest].clear(1, 0, numSamples);
            used[(size_t)dest] = true;
        }

        for (int ch = 0; ch < 2; ++ch) {
            const float* in  = srcBuffers[(size_t)src].getReadPointer(ch);
            float*       out = destBuffers[(size_t)dest].getWritePointer(ch);
            float sm = smoothState[(size_t)ri][(size_t)ch];
            for (int i = 0; i < numSamples; ++i) {
                const float target = srcActive
                    ? transformSample(in[i], srcBipolar, routeBip, inv, amount) : 0.0f;
                sm = target + coeff * (sm - target);
                out[i] += sm;
            }
            smoothState[(size_t)ri][(size_t)ch] = sm;
        }
    }

    for (int d = 0; d < ModDest::NumAudioRate; ++d) {
        destInUse[(size_t)d] = used[(size_t)d];
        uiDestOffset[(size_t)d].store(
            used[(size_t)d] ? destBuffers[(size_t)d].getSample(0, numSamples - 1) : 0.0f,
            std::memory_order_relaxed);
    }
}

// ── Routing management (message thread) ──────────────────────────────────────

int ModMatrix::findRouting(int source, int dest) const {
    for (int i = 0; i < ModConst::MAX_ROUTINGS; ++i) {
        const auto& r = routings[(size_t)i];
        if (r.active.load() && r.source.load() == source && r.dest.load() == dest)
            return i;
    }
    return -1;
}

int ModMatrix::addRouting(int source, int dest, float amount) {
    const int existing = findRouting(source, dest);
    if (existing >= 0) return existing;

    for (int i = 0; i < ModConst::MAX_ROUTINGS; ++i) {
        auto& r = routings[(size_t)i];
        if (r.active.load()) continue;
        r.source  .store(source);
        r.dest    .store(dest);
        r.amount  .store(amount);
        r.bipolar .store(sourceIsBipolar(source));
        r.invert  .store(false);
        r.smoothMs.store(10.0f);
        smoothState[(size_t)i] = { 0.0f, 0.0f };
        r.active.store(true);   // publish last
        return i;
    }
    return -1;
}

void ModMatrix::removeRouting(int index) {
    if (index >= 0 && index < ModConst::MAX_ROUTINGS)
        routings[(size_t)index].active.store(false);
}

int ModMatrix::routingsForDest(int dest, std::array<int, ModConst::MAX_ROUTINGS>& out) const {
    int n = 0;
    for (int i = 0; i < ModConst::MAX_ROUTINGS; ++i)
        if (routings[(size_t)i].active.load() && routings[(size_t)i].dest.load() == dest)
            out[(size_t)n++] = i;
    return n;
}

bool ModMatrix::destHasRoutings(int dest) const {
    for (const auto& r : routings)
        if (r.active.load() && r.dest.load() == dest) return true;
    return false;
}

float ModMatrix::destModRange(int dest) const {
    float range = 0.0f;
    for (const auto& r : routings)
        if (r.active.load() && r.dest.load() == dest)
            range += std::abs(r.amount.load());
    return range;
}

juce::String ModMatrix::sourceName(int src) {
    if (ModSrc::isLfo(src)) return "XLFO " + juce::String(src + 1);
    if (ModSrc::isEnv(src)) return "ENV "  + juce::String(src - ModSrc::EnvFirst + 1);
    return "?";
}

// ── Serialization ────────────────────────────────────────────────────────────

void ModMatrix::toXml(juce::XmlElement& root) const {
    for (int i = 0; i < ModConst::NUM_XLFOS; ++i) {
        const auto& p = lfoParams[(size_t)i];
        auto* el = root.createNewChildElement("XLfo");
        el->setAttribute("index",       i);
        el->setAttribute("active",      p.active.load());
        el->setAttribute("shape",       p.shape.load());
        el->setAttribute("tempoSync",   p.tempoSync.load());
        el->setAttribute("rateHz",      (double)p.rateHz.load());
        el->setAttribute("syncDiv",     p.syncDiv.load());
        el->setAttribute("playMode",    p.playMode.load());
        el->setAttribute("trigMode",    p.trigMode.load());
        el->setAttribute("phaseOffset", (double)p.phaseOffset.load());
        el->setAttribute("fadeInSec",   (double)p.fadeInSec.load());
        el->setAttribute("smoothMs",    (double)p.smoothMs.load());
        el->setAttribute("pulseWidth",  (double)p.pulseWidth.load());
        el->setAttribute("amount",      (double)p.amount.load());
        p.curve.toXml(*el, "Curve");
    }

    for (int i = 0; i < ModConst::NUM_ENVS; ++i) {
        const auto& p = envParams[(size_t)i];
        auto* el = root.createNewChildElement("Env");
        el->setAttribute("index",        i);
        el->setAttribute("active",       p.active.load());
        el->setAttribute("attackSec",    (double)p.attackSec.load());
        el->setAttribute("releaseSec",   (double)p.releaseSec.load());
        el->setAttribute("followerGain", (double)p.followerGain.load());
        el->setAttribute("followerPeak", p.followerPeak.load());
    }

    for (int i = 0; i < ModConst::MAX_ROUTINGS; ++i) {
        const auto& r = routings[(size_t)i];
        if (!r.active.load()) continue;
        auto* el = root.createNewChildElement("Routing");
        el->setAttribute("source",   r.source.load());
        el->setAttribute("dest",     r.dest.load());
        el->setAttribute("amount",   (double)r.amount.load());
        el->setAttribute("bipolar",  r.bipolar.load());
        el->setAttribute("invert",   r.invert.load());
        el->setAttribute("smoothMs", (double)r.smoothMs.load());
    }
}

void ModMatrix::fromXml(const juce::XmlElement& root) {
    for (auto* el : root.getChildWithTagNameIterator("XLfo")) {
        const int i = el->getIntAttribute("index", -1);
        if (i < 0 || i >= ModConst::NUM_XLFOS) continue;
        auto& p = lfoParams[(size_t)i];
        p.active     .store(el->getBoolAttribute("active"));
        p.shape      .store(el->getIntAttribute ("shape"));
        p.tempoSync  .store(el->getBoolAttribute("tempoSync"));
        p.rateHz     .store((float)el->getDoubleAttribute("rateHz", 1.0));
        p.syncDiv    .store(el->getIntAttribute ("syncDiv", DEFAULT_SYNC_DIV));
        p.playMode   .store(el->getIntAttribute ("playMode"));
        p.trigMode   .store(el->getIntAttribute ("trigMode"));
        p.phaseOffset.store((float)el->getDoubleAttribute("phaseOffset"));
        p.fadeInSec  .store((float)el->getDoubleAttribute("fadeInSec"));
        p.smoothMs   .store((float)el->getDoubleAttribute("smoothMs", 5.0));
        p.pulseWidth .store((float)el->getDoubleAttribute("pulseWidth", 0.5));
        p.amount     .store((float)el->getDoubleAttribute("amount", 1.0));
        p.curve.fromXml(*el, "Curve");
    }

    for (auto* el : root.getChildWithTagNameIterator("Env")) {
        const int i = el->getIntAttribute("index", -1);
        if (i < 0 || i >= ModConst::NUM_ENVS) continue;
        auto& p = envParams[(size_t)i];
        p.active      .store(el->getBoolAttribute("active"));
        p.attackSec   .store((float)el->getDoubleAttribute("attackSec",    0.01));
        p.releaseSec  .store((float)el->getDoubleAttribute("releaseSec",   0.3));
        p.followerGain.store((float)el->getDoubleAttribute("followerGain", 4.0));
        p.followerPeak.store(el->getBoolAttribute("followerPeak", true));
    }

    for (auto& r : routings) r.active.store(false);
    int slot = 0;
    for (auto* el : root.getChildWithTagNameIterator("Routing")) {
        if (slot >= ModConst::MAX_ROUTINGS) break;
        auto& r = routings[(size_t)slot];
        r.source  .store(el->getIntAttribute("source"));
        r.dest    .store(el->getIntAttribute("dest"));
        r.amount  .store((float)el->getDoubleAttribute("amount", 0.25));
        r.bipolar .store(el->getBoolAttribute("bipolar", true));
        r.invert  .store(el->getBoolAttribute("invert"));
        r.smoothMs.store((float)el->getDoubleAttribute("smoothMs", 10.0));
        r.active  .store(true);
        ++slot;
    }
}
