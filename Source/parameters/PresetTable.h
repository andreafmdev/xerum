#pragma once

// GENERATO da scripts/gen-params.mjs a partire da Source/parameters/presets.json.
// Non modificare a mano.

namespace params
{
struct PresetValue
{
    const char* id;
    float value;
};

struct Preset
{
    const char* name;
    const char* category;
    const PresetValue* values;
    int numValues;
};

inline constexpr const PresetValue* kPreset0Values = nullptr;
inline constexpr PresetValue kPreset1Values[] = { { "wtIndex", 1.0f }, { "wtpos", 0.1f }, { "cutoff", 0.35f }, { "res", 0.15f }, { "att", 0.0f }, { "dec", 0.3f }, { "sus", 0.6f }, { "rel", 0.2f } };
inline constexpr PresetValue kPreset2Values[] = { { "wtIndex", 0.2f }, { "wtpos", 0.45f }, { "cutoff", 0.4f }, { "res", 0.35f }, { "drive", 0.4f } };
inline constexpr PresetValue kPreset3Values[] = { { "wtIndex", 0.2f }, { "cutoff", 0.3f }, { "res", 0.7f }, { "keytrk", 0.8f }, { "dec", 0.25f }, { "sus", 0.1f } };
inline constexpr PresetValue kPreset4Values[] = { { "wtIndex", 0.4f }, { "wtpos", 0.6f }, { "cutoff", 0.7f }, { "res", 0.25f }, { "att", 0.05f } };
inline constexpr PresetValue kPreset5Values[] = { { "wtIndex", 0.2f }, { "wtpos", 0.0f }, { "cutoff", 0.75f }, { "res", 0.1f } };
inline constexpr PresetValue kPreset6Values[] = { { "wtIndex", 0.8f }, { "wtpos", 0.5f }, { "cutoff", 0.55f }, { "att", 0.45f }, { "rel", 0.6f }, { "sus", 0.8f } };
inline constexpr PresetValue kPreset7Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.35f }, { "cutoff", 0.5f }, { "att", 0.5f }, { "rel", 0.65f } };
inline constexpr PresetValue kPreset8Values[] = { { "wtIndex", 0.8f }, { "wtpos", 0.2f }, { "cutoff", 0.6f }, { "dec", 0.5f }, { "sus", 0.35f }, { "rel", 0.3f } };
inline constexpr PresetValue kPreset9Values[] = { { "wtIndex", 0.8f }, { "wtpos", 0.8f }, { "cutoff", 0.8f }, { "dec", 0.6f }, { "sus", 0.1f }, { "rel", 0.5f } };
inline constexpr PresetValue kPreset10Values[] = { { "wtIndex", 0.4f }, { "cutoff", 0.65f }, { "res", 0.3f }, { "att", 0.0f }, { "dec", 0.2f }, { "sus", 0.0f }, { "rel", 0.15f } };
inline constexpr PresetValue kPreset11Values[] = { { "wtIndex", 1.0f }, { "wtpos", 0.9f }, { "cutoff", 0.45f }, { "res", 0.5f }, { "att", 0.6f }, { "rel", 0.7f } };

inline constexpr int kNumPresets = 12;
inline constexpr Preset kPresetTable[kNumPresets] = {
    { "Init", "User", nullptr, 0 },
    { "Sub Pulse", "Bass", kPreset1Values, 8 },
    { "Reese Wide", "Bass", kPreset2Values, 5 },
    { "Acid Line", "Bass", kPreset3Values, 6 },
    { "Neon Lead", "Lead", kPreset4Values, 5 },
    { "Solid Saw", "Lead", kPreset5Values, 4 },
    { "Glass Pad", "Pad", kPreset6Values, 6 },
    { "Dust Choir", "Pad", kPreset7Values, 5 },
    { "Velvet Keys", "Keys", kPreset8Values, 6 },
    { "Bell Tower", "Keys", kPreset9Values, 6 },
    { "Wire Pluck", "Pluck", kPreset10Values, 7 },
    { "Cold Sweep", "FX", kPreset11Values, 6 },
};
} // namespace params
