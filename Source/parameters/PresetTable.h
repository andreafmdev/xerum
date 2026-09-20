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
inline constexpr PresetValue kPreset1Values[] = { { "wtIndex", 0.4166666666666667f }, { "wtpos", 0.1f }, { "oct", 0.3333f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.392f }, { "res", 0.25f }, { "drive", 0.3464f }, { "keytrk", 0.6f }, { "att", 0.0f }, { "dec", 0.25f }, { "sus", 0.75f }, { "rel", 0.078f }, { "level", 0.9f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset2Values[] = { { "wtIndex", 0.08333333333333333f }, { "wtpos", 0.45f }, { "oct", 0.3333f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.434f }, { "res", 0.35f }, { "drive", 0.5f }, { "keytrk", 0.5f }, { "att", 0.0f }, { "dec", 0.316f }, { "sus", 0.8f }, { "rel", 0.099f }, { "level", 0.6f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset3Values[] = { { "wtIndex", 0.08333333333333333f }, { "wtpos", 0.0f }, { "oct", 0.3333f }, { "ftype", 0.0f }, { "slope", 0.0f }, { "cutoff", 0.333f }, { "res", 0.78f }, { "drive", 0.4472f }, { "keytrk", 0.9f }, { "att", 0.0f }, { "dec", 0.193f }, { "sus", 0.1f }, { "rel", 0.049f }, { "level", 0.8f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset4Values[] = { { "wtIndex", 0.16666666666666666f }, { "wtpos", 0.6f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.693f }, { "res", 0.3f }, { "drive", 0.3873f }, { "keytrk", 0.5f }, { "att", 0.022f }, { "dec", 0.25f }, { "sus", 0.85f }, { "rel", 0.122f }, { "level", 0.8f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset5Values[] = { { "wtIndex", 0.08333333333333333f }, { "wtpos", 0.0f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.784f }, { "res", 0.15f }, { "drive", 0.0f }, { "keytrk", 0.5f }, { "att", 0.022f }, { "dec", 0.25f }, { "sus", 0.9f }, { "rel", 0.099f }, { "level", 0.85f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset6Values[] = { { "wtIndex", 0.3333333333333333f }, { "wtpos", 0.5f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.593f }, { "res", 0.2f }, { "drive", 0.0f }, { "keytrk", 0.4f }, { "att", 0.387f }, { "dec", 0.474f }, { "sus", 0.8f }, { "rel", 0.559f }, { "level", 0.7f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset7Values[] = { { "wtIndex", 0.25f }, { "wtpos", 0.35f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.534f }, { "res", 0.2f }, { "drive", 0.0f }, { "keytrk", 0.4f }, { "att", 0.316f }, { "dec", 0.474f }, { "sus", 0.75f }, { "rel", 0.474f }, { "level", 0.7f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset8Values[] = { { "wtIndex", 0.3333333333333333f }, { "wtpos", 0.2f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.634f }, { "res", 0.15f }, { "drive", 0.0f }, { "keytrk", 0.6f }, { "att", 0.022f }, { "dec", 0.25f }, { "sus", 0.35f }, { "rel", 0.193f }, { "level", 0.85f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset9Values[] = { { "wtIndex", 0.3333333333333333f }, { "wtpos", 0.8f }, { "ftype", 0.0f }, { "slope", 0.0f }, { "cutoff", 0.826f }, { "res", 0.1f }, { "drive", 0.0f }, { "keytrk", 0.8f }, { "att", 0.0f }, { "dec", 0.387f }, { "sus", 0.05f }, { "rel", 0.316f }, { "level", 0.8f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset10Values[] = { { "wtIndex", 0.16666666666666666f }, { "wtpos", 0.25f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.735f }, { "res", 0.35f }, { "drive", 0.3162f }, { "keytrk", 0.7f }, { "att", 0.0f }, { "dec", 0.158f }, { "sus", 0.0f }, { "rel", 0.078f }, { "level", 0.9f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset11Values[] = { { "wtIndex", 0.4166666666666667f }, { "wtpos", 0.9f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.492f }, { "res", 0.55f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.474f }, { "dec", 0.559f }, { "sus", 0.7f }, { "rel", 0.661f }, { "level", 0.7f }, { "volume", 0.7f } };

inline constexpr int kNumPresets = 12;
inline constexpr Preset kPresetTable[kNumPresets] = {
    { "Init", "User", nullptr, 0 },
    { "Sub Pulse", "Bass", kPreset1Values, 15 },
    { "Reese Wide", "Bass", kPreset2Values, 15 },
    { "Acid Line", "Bass", kPreset3Values, 15 },
    { "Neon Lead", "Lead", kPreset4Values, 14 },
    { "Solid Saw", "Lead", kPreset5Values, 14 },
    { "Glass Pad", "Pad", kPreset6Values, 14 },
    { "Dust Choir", "Pad", kPreset7Values, 14 },
    { "Velvet Keys", "Keys", kPreset8Values, 14 },
    { "Bell Tower", "Keys", kPreset9Values, 14 },
    { "Wire Pluck", "Pluck", kPreset10Values, 14 },
    { "Cold Sweep", "FX", kPreset11Values, 14 },
};
} // namespace params
