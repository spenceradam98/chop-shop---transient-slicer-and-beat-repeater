// The parameter table: the single source of truth for the engine, the plugin's
// AudioProcessorValueTreeState, the presets and the editor. Per-slice settings are not
// host parameters (the slice count changes); they live in the Kit and in the saved state.
#pragma once

namespace cs
{
enum class Kind { Float, Choice, Bool, Int };

struct ParamDef
{
    const char* id;
    const char* name;
    Kind kind;
    float min, max, def;
    float centre;          // value at 12 o'clock for skewed ranges (0 = linear)
    const char* unit;
    const char* choices;   // '|' separated, for Choice
};

// X(enumName, id, display name, kind, min, max, default, centre, unit, choices)
#define CS_PARAMS(X) \
    X (SliceMode,   "slice_mode",   "Slice Mode",       Choice, 0, 3, 0, 0, "", "Transient|Grid 1/8|Grid 1/16|Manual") \
    X (Sensitivity, "sensitivity",  "Sensitivity",      Float,  0, 1, 0.6f, 0, "", "") \
    X (MinSlice,    "min_slice",    "Min Slice",        Float, 10, 500, 60, 100, "ms", "") \
    X (RootNote,    "root_note",    "Root Note",        Int,    0, 96, 36, 0, "note", "") \
    X (Polyphony,   "polyphony",    "Voices",           Choice, 0, 1, 0, 0, "", "Poly|Mono") \
    X (Choke,       "choke",        "Choke",            Bool,   0, 1, 1, 0, "", "") \
    X (PatOn,       "pat_on",       "Pattern On",       Bool,   0, 1, 1, 0, "", "") \
    X (FollowHost,  "follow_host",  "Follow Host",      Bool,   0, 1, 1, 0, "", "") \
    X (Order,       "order",        "Order",            Choice, 0, 3, 0, 0, "", "Original|Random|Reverse|Shuffle in bar") \
    X (Speed,       "speed",        "Speed",            Choice, 0, 2, 1, 0, "", "Half|Normal|Double") \
    X (Swing,       "swing",        "Swing",            Float,  0, 1, 0, 0, "", "") \
    X (Mutation,    "mutation",     "Mutation",         Float,  0, 2, 1, 0, "", "") \
    X (Seed,        "seed",         "Seed",             Int,    1, 9999, 1, 0, "", "") \
    X (Lock,        "lock",         "Lock",             Bool,   0, 1, 0, 0, "", "") \
    X (Stretch,     "stretch",      "Time-stretch",     Choice, 0, 2, 1, 0, "", "Off|Repitch|Stretch") \
    X (Filter,      "filter",       "Filter",           Float, -1, 1, 0, 0, "bip", "") \
    X (Crush,       "crush",        "Crush",            Float,  0, 1, 0, 0, "", "") \
    X (Output,      "output",       "Output",           Float, -24, 12, 0, 0, "dB", "") \
    X (Mix,         "mix",          "Mix",              Float,  0, 1, 1, 0, "", "") \
    X (CaptureBars, "capture_bars", "Capture Bars",     Choice, 0, 3, 1, 0, "", "1|2|4|8") \
    X (Capture,     "capture",      "Capture",          Bool,   0, 1, 0, 0, "", "")

enum P : int
{
#define CS_ENUM(e, ...) e,
    CS_PARAMS (CS_ENUM)
#undef CS_ENUM
    NumParams
};

inline const ParamDef* paramDefs()
{
#define CS_DEF(e, id, name, kind, mn, mx, df, ct, unit, ch) ParamDef { id, name, Kind::kind, (float) (mn), (float) (mx), (float) (df), (float) (ct), unit, ch },
    static const ParamDef defs[NumParams] = { CS_PARAMS (CS_DEF) };
#undef CS_DEF
    return defs;
}

int paramIndex (const char* id);   // index for an id string, or -1
} // namespace cs
