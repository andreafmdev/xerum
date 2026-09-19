// GENERATED da Source/parameters/parameters.json — non modificare a mano.
// Rigenera con: node scripts/gen-params.mjs
#pragma once

namespace params
{
enum class Kind { Float, Int, Bool, Choice };
enum class Map { None, Linear, Log, Db, MsSquared };
enum class Label { None, Hz, Time, Pan, Signed, ArpRate };

struct Spec
{
    const char* id; const char* name; const char* group;
    Kind kind; Map map; float min; float max; float offset; float def;
    const char* unit; int decimals; Label label;
    const char* const* options; int numOptions;
};

inline constexpr const char* const kOptions_wtIndex[] = { "Basic Shapes", "Analog Saws", "Digital Grit", "Vocal Formant", "Glass Bells", "PWM Sweep" };
inline constexpr const char* const kOptions_unison[] = { "1", "2", "4", "8" };
inline constexpr const char* const kOptions_ftype[] = { "LP", "HP", "BP" };
inline constexpr const char* const kOptions_slope[] = { "12", "24" };
inline constexpr const char* const kOptions_voiceMode[] = { "Poly", "Mono", "Legato" };
inline constexpr const char* const kOptions_lshape[] = { "Sine", "Tri", "Saw", "Square", "S&H" };
inline constexpr const char* const kOptions_arpMode[] = { "Up", "Down", "UpDn", "Rand" };

inline constexpr int kNumParams = 53;
inline constexpr Spec kTable[kNumParams] = {
    { "oscOn", "Oscillator on", "osc", Kind::Bool, Map::None, 0.0f, 1.0f, 0.0f, 1.0f, nullptr, 0, Label::None, nullptr, 0 },
    { "wtIndex", "Wavetable", "osc", Kind::Choice, Map::None, 0.0f, 1.0f, 0.0f, 0.0f, nullptr, 0, Label::None, kOptions_wtIndex, 6 },
    { "unison", "Unison", "osc", Kind::Choice, Map::None, 0.0f, 1.0f, 0.0f, 0.0f, nullptr, 0, Label::None, kOptions_unison, 4 },
    { "oct", "Octave", "osc", Kind::Int, Map::Linear, -3.0f, 3.0f, 0.0f, 0.0f, "OCT", 0, Label::None, nullptr, 0 },
    { "semi", "Semitones", "osc", Kind::Int, Map::Linear, -12.0f, 12.0f, 0.0f, 0.0f, "SEMI", 0, Label::None, nullptr, 0 },
    { "wtpos", "Position", "osc", Kind::Float, Map::Linear, 1.0f, 64.0f, 0.0f, 0.32f, nullptr, 1, Label::None, nullptr, 0 },
    { "warp", "Warp", "osc", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.1f, "%", 0, Label::None, nullptr, 0 },
    { "detune", "Detune", "osc", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.18f, "ct", 0, Label::None, nullptr, 0 },
    { "fine", "Fine", "osc", Kind::Float, Map::Linear, -100.0f, 100.0f, 0.0f, 0.5f, "ct", 0, Label::None, nullptr, 0 },
    { "level", "Level", "osc", Kind::Float, Map::Db, 0.0f, 1.0f, 0.0f, 0.85f, "dB", 1, Label::None, nullptr, 0 },
    { "filtOn", "Filter on", "filter", Kind::Bool, Map::None, 0.0f, 1.0f, 0.0f, 1.0f, nullptr, 0, Label::None, nullptr, 0 },
    { "ftype", "Filter type", "filter", Kind::Choice, Map::None, 0.0f, 1.0f, 0.0f, 0.0f, nullptr, 0, Label::None, kOptions_ftype, 3 },
    { "slope", "Slope", "filter", Kind::Choice, Map::None, 0.0f, 1.0f, 0.0f, 1.0f, nullptr, 0, Label::None, kOptions_slope, 2 },
    { "cutoff", "Cutoff", "filter", Kind::Float, Map::Log, 20.0f, 20000.0f, 0.0f, 0.62f, "Hz", 0, Label::Hz, nullptr, 0 },
    { "res", "Resonance", "filter", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.3f, "%", 0, Label::None, nullptr, 0 },
    { "drive", "Drive", "filter", Kind::Float, Map::Linear, 0.0f, 24.0f, 0.0f, 0.0f, "dB", 1, Label::None, nullptr, 0 },
    { "keytrk", "Key trk", "filter", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.5f, "%", 0, Label::None, nullptr, 0 },
    { "voiceMode", "Voice mode", "master", Kind::Choice, Map::None, 0.0f, 1.0f, 0.0f, 0.0f, nullptr, 0, Label::None, kOptions_voiceMode, 3 },
    { "pan", "Pan", "master", Kind::Float, Map::Linear, -50.0f, 50.0f, 0.0f, 0.5f, nullptr, 0, Label::Pan, nullptr, 0 },
    { "glide", "Glide", "master", Kind::Float, Map::MsSquared, 0.0f, 2000.0f, 0.0f, 0.0f, "ms", 0, Label::None, nullptr, 0 },
    { "volume", "Volume", "master", Kind::Float, Map::Db, 0.0f, 1.0f, 0.0f, 0.8f, "dB", 1, Label::None, nullptr, 0 },
    { "bypass", "Bypass", "master", Kind::Bool, Map::None, 0.0f, 1.0f, 0.0f, 0.0f, nullptr, 0, Label::None, nullptr, 0 },
    { "att", "Attack", "env", Kind::Float, Map::MsSquared, 1.0f, 8001.0f, 0.0f, 0.12f, nullptr, 0, Label::Time, nullptr, 0 },
    { "dec", "Decay", "env", Kind::Float, Map::MsSquared, 1.0f, 8001.0f, 0.0f, 0.4f, nullptr, 0, Label::Time, nullptr, 0 },
    { "sus", "Sustain", "env", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.7f, "%", 0, Label::None, nullptr, 0 },
    { "rel", "Release", "env", Kind::Float, Map::MsSquared, 1.0f, 8001.0f, 0.0f, 0.35f, nullptr, 0, Label::Time, nullptr, 0 },
    { "envVel", "Vel → amp", "env", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.6f, "%", 0, Label::None, nullptr, 0 },
    { "envCurve", "Curve", "env", Kind::Float, Map::Linear, -100.0f, 100.0f, 0.0f, 0.5f, nullptr, 0, Label::Signed, nullptr, 0 },
    { "lshape", "LFO shape", "lfo", Kind::Choice, Map::None, 0.0f, 1.0f, 0.0f, 0.0f, nullptr, 0, Label::None, kOptions_lshape, 5 },
    { "lsync", "LFO sync", "lfo", Kind::Bool, Map::None, 0.0f, 1.0f, 0.0f, 0.0f, nullptr, 0, Label::None, nullptr, 0 },
    { "lretrig", "LFO retrig", "lfo", Kind::Bool, Map::None, 0.0f, 1.0f, 0.0f, 1.0f, nullptr, 0, Label::None, nullptr, 0 },
    { "lrate", "Rate", "lfo", Kind::Float, Map::Log, 0.05f, 20.0f, 0.0f, 0.45f, "Hz", 2, Label::None, nullptr, 0 },
    { "lphase", "Phase", "lfo", Kind::Float, Map::Linear, 0.0f, 360.0f, 0.0f, 0.0f, "°", 0, Label::None, nullptr, 0 },
    { "lfade", "Fade in", "lfo", Kind::Float, Map::Linear, 0.0f, 4000.0f, 0.0f, 0.0f, "ms", 0, Label::None, nullptr, 0 },
    { "fx1On", "Chorus on", "fx1", Kind::Bool, Map::None, 0.0f, 1.0f, 0.0f, 1.0f, nullptr, 0, Label::None, nullptr, 0 },
    { "chRate", "Rate", "fx1", Kind::Float, Map::Linear, 0.1f, 5.1f, 0.0f, 0.3f, "Hz", 2, Label::None, nullptr, 0 },
    { "chDepth", "Depth", "fx1", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.4f, "%", 0, Label::None, nullptr, 0 },
    { "chMix", "Mix", "fx1", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.3f, "%", 0, Label::None, nullptr, 0 },
    { "fx2On", "Reverb on", "fx2", Kind::Bool, Map::None, 0.0f, 1.0f, 0.0f, 1.0f, nullptr, 0, Label::None, nullptr, 0 },
    { "rvSize", "Size", "fx2", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.6f, "%", 0, Label::None, nullptr, 0 },
    { "rvDamp", "Damp", "fx2", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.4f, "%", 0, Label::None, nullptr, 0 },
    { "rvMix", "Mix", "fx2", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.25f, "%", 0, Label::None, nullptr, 0 },
    { "arpOn", "Arp on", "arp", Kind::Bool, Map::None, 0.0f, 1.0f, 0.0f, 0.0f, nullptr, 0, Label::None, nullptr, 0 },
    { "arpMode", "Arp mode", "arp", Kind::Choice, Map::None, 0.0f, 1.0f, 0.0f, 0.0f, nullptr, 0, Label::None, kOptions_arpMode, 4 },
    { "arpRate", "Rate", "arp", Kind::Float, Map::Linear, 0.0f, 1.0f, 0.0f, 0.4f, nullptr, 0, Label::ArpRate, nullptr, 0 },
    { "arpGate", "Gate", "arp", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.6f, "%", 0, Label::None, nullptr, 0 },
    { "arpOct", "Octaves", "arp", Kind::Float, Map::Linear, 1.0f, 4.0f, 0.0f, 0.33f, nullptr, 0, Label::None, nullptr, 0 },
    { "arpSwing", "Swing", "arp", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.0f, "%", 0, Label::None, nullptr, 0 },
    { "att2", "Attack", "env2", Kind::Float, Map::MsSquared, 1.0f, 8001.0f, 0.0f, 0.12f, nullptr, 0, Label::Time, nullptr, 0 },
    { "dec2", "Decay", "env2", Kind::Float, Map::MsSquared, 1.0f, 8001.0f, 0.0f, 0.4f, nullptr, 0, Label::Time, nullptr, 0 },
    { "sus2", "Sustain", "env2", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.7f, "%", 0, Label::None, nullptr, 0 },
    { "rel2", "Release", "env2", Kind::Float, Map::MsSquared, 1.0f, 8001.0f, 0.0f, 0.35f, nullptr, 0, Label::Time, nullptr, 0 },
    { "chFeedback", "Feedback", "fx1", Kind::Float, Map::Linear, 0.0f, 100.0f, 0.0f, 0.0f, "%", 0, Label::None, nullptr, 0 },
};

inline constexpr const Spec* find (const char* id) noexcept
{
    for (const auto& s : kTable)
    {
        const char* a = s.id; const char* b = id;
        while (*a != 0 && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0) return &s;
    }
    return nullptr;
}

// --- slot del motore -------------------------------------------------------------------
//
// Identifica un parametro grezzo senza passare per il suo nome: chi implementa l'accessore
// risolve `id -> puntatore` una volta sola alla costruzione (vedi PluginProcessor::paramSlots_,
// che cicla su kSlotIds), e qui dentro e' solo un indice di array.
//
// Ci sono soltanto i parametri con "slot": true in parameters.json, cioe' quelli che il motore
// legge una volta per blocco attraverso params::collectEngineParams. Gli altri o non sono
// ancora cablati, o viaggiano per conto loro (wtIndex e volume, vedi PluginProcessor).
//
// L'ordine e' quello di parameters.json, ma resta un dettaglio interno fra questo header e chi
// scrive l'accessore, non un ABI pubblico: nessuno stato salvato contiene un indice di slot.
// Non coincide con l'indice dentro kTable, perche' i parametri senza slot creano dei buchi:
// per passare dall'uno all'altro c'e' specForSlot().
inline constexpr int kNumSlots = 37;

enum class ParamSlot : int
{
    oscOn, unison, oct, semi, wtpos, detune, fine, level,
    filtOn, ftype, slope, cutoff, res, drive, keytrk, pan,
    bypass, att, dec, sus, rel, envVel, lshape, lsync,
    lretrig, lrate, lphase, lfade, fx1On, chRate, chDepth, chMix,
    att2, dec2, sus2, rel2, chFeedback,
    count
};

static_assert ((int) ParamSlot::count == kNumSlots, "enum e conteggio devono coincidere");

/** L'id del parametro di ogni slot, nello stesso ordine dell'enum. */
inline constexpr const char* kSlotIds[kNumSlots] = {
    "oscOn", "unison", "oct", "semi", "wtpos", "detune", "fine", "level",
    "filtOn", "ftype", "slope", "cutoff", "res", "drive", "keytrk", "pan",
    "bypass", "att", "dec", "sus", "rel", "envVel", "lshape", "lsync",
    "lretrig", "lrate", "lphase", "lfade", "fx1On", "chRate", "chDepth", "chMix",
    "att2", "dec2", "sus2", "rel2", "chFeedback",
};

/** L'indice dentro kTable di ogni slot: kTable[kSlotTableIndex[i]].id e' kSlotIds[i]. */
inline constexpr int kSlotTableIndex[kNumSlots] = {
    0, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18,
    21, 22, 23, 24, 25, 26, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    48, 49, 50, 51, 52,
};

/** La spec del parametro dietro uno slot. constexpr: non costa niente a runtime. */
inline constexpr const Spec& specForSlot (ParamSlot s) noexcept
{
    return kTable[kSlotTableIndex[(int) s]];
}
} // namespace params
