#pragma once
#include <array>

// ── Shared modulation-system types & constants ───────────────────────────────
//
// The modulation framework is deliberately independent of the DSP code:
// destinations are opaque integer IDs registered by whoever owns the matrix
// (see ModDest below for this plugin's layout), and sources only ever write
// into per-sample float buffers. Adding a new source type or destination
// touches nothing in Source/DSP.

namespace ModConst {
    constexpr int NUM_XLFOS    = 8;
    constexpr int NUM_ENVS     = 4;
    constexpr int NUM_SOURCES  = NUM_XLFOS + NUM_ENVS;
    constexpr int MAX_ROUTINGS = 64;   // fixed pool → zero audio-thread allocation
}

// Modulation source IDs. XLFOs first, then envelope generators.
namespace ModSrc {
    enum : int {
        XLfoFirst = 0,                              // 0..7
        EnvFirst  = ModConst::NUM_XLFOS,            // 8..11
        Count     = ModConst::NUM_SOURCES
    };
    inline bool isEnv (int id) { return id >= EnvFirst && id < Count; }
    inline bool isLfo (int id) { return id >= 0 && id < EnvFirst; }
}

// Modulation destination IDs for this plugin.
// [0 .. NumAudioRate) are audio-rate destinations: the matrix renders a
// per-sample stereo offset buffer for each. IDs at/after NumAudioRate are
// per-block "meta" destinations (modulator modulating modulator).
namespace ModDest {
    enum : int {
        DryWet       = 0,
        NodeMixFirst = 1,                            // 1..5   node mix weights
        NodeP1First  = 6,                            // 6..10  node param 1
        NodeP2First  = 11,                           // 11..15 node param 2
        NumAudioRate  = 16,
        LfoRateFirst  = 16,                          // 16..23 XLFO rate   (meta)
        LfoAmtFirst   = 24,                          // 24..31 XLFO amount (meta)
        LfoPhaseFirst = 32,                          // 32..39 XLFO phase  (meta)
        LfoPwFirst    = 40,                          // 40..47 XLFO width  (meta)
        Count         = 48
    };
    inline int nodeMix (int n)  { return NodeMixFirst  + n; }
    inline int nodeP1  (int n)  { return NodeP1First   + n; }
    inline int nodeP2  (int n)  { return NodeP2First   + n; }
    inline int lfoRate (int i)  { return LfoRateFirst  + i; }
    inline int lfoAmt  (int i)  { return LfoAmtFirst   + i; }
    inline int lfoPhase(int i)  { return LfoPhaseFirst + i; }
    inline int lfoPw   (int i)  { return LfoPwFirst    + i; }
    inline bool isAudioRate(int id) { return id >= 0 && id < NumAudioRate; }
    inline bool isLfoMeta  (int id) { return id >= LfoRateFirst && id < Count; }
}

// Host transport snapshot, captured once per block by the processor.
struct TransportInfo {
    double bpm         = 120.0;
    double ppqPosition = 0.0;
    bool   isPlaying   = false;
    bool   justStarted = false;  // true on the first block after playback starts
};

// A MIDI trigger event with its offset inside the current block
// (sample-accurate envelope triggering).
struct ModTriggerEvent {
    int   sampleOffset = 0;
    bool  isNoteOn     = true;
    float velocity     = 1.0f;   // 0..1
};

// Fixed-capacity per-block event list (filled on the audio thread, no alloc).
struct ModTriggerList {
    static constexpr int MaxEvents = 32;
    std::array<ModTriggerEvent, MaxEvents> events;
    int numEvents = 0;
    void clear() { numEvents = 0; }
    void add(const ModTriggerEvent& e) {
        if (numEvents < MaxEvents) events[(size_t)numEvents++] = e;
    }
};

// ── Tempo-sync division table ────────────────────────────────────────────────
struct SyncDivision {
    const char* name;
    double beats;   // length of one cycle in quarter notes
};

// Straight, dotted and triplet divisions from 8 bars down to 1/64.
inline const std::array<SyncDivision, 22>& syncDivisions() {
    static const std::array<SyncDivision, 22> table {{
        { "8/1",   128.0        }, { "4/1",   64.0         }, { "2/1",  32.0        },
        { "1/1",   16.0         }, { "1/2",    8.0         }, { "1/2.", 12.0        },
        { "1/2T",  16.0 / 3.0   }, { "1/4",    4.0         }, { "1/4.",  6.0        },
        { "1/4T",   8.0 / 3.0   }, { "1/8",    2.0         }, { "1/8.",  3.0        },
        { "1/8T",   4.0 / 3.0   }, { "1/16",   1.0         }, { "1/16.", 1.5        },
        { "1/16T",  2.0 / 3.0   }, { "1/32",   0.5         }, { "1/32.", 0.75       },
        { "1/32T",  1.0 / 3.0   }, { "1/64",   0.25        }, { "1/64.", 0.375      },
        { "1/64T",  1.0 / 6.0   }
    }};
    return table;
}

constexpr int DEFAULT_SYNC_DIV = 7; // "1/4"
