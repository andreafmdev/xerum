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
inline constexpr PresetValue kPreset1Values[] = { { "wtIndex", 0.5f }, { "wtpos", 0.1f }, { "oct", 0.3333f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.392f }, { "res", 0.25f }, { "drive", 0.3464f }, { "keytrk", 0.6f }, { "att", 0.0f }, { "dec", 0.25f }, { "sus", 0.75f }, { "rel", 0.078f }, { "level", 0.9f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset2Values[] = { { "wtIndex", 0.1f }, { "wtpos", 0.45f }, { "oct", 0.3333f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.434f }, { "res", 0.35f }, { "drive", 0.5f }, { "keytrk", 0.5f }, { "att", 0.0f }, { "dec", 0.316f }, { "sus", 0.8f }, { "rel", 0.099f }, { "level", 0.6f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset3Values[] = { { "wtIndex", 0.1f }, { "wtpos", 0.0f }, { "oct", 0.3333f }, { "ftype", 0.0f }, { "slope", 0.0f }, { "cutoff", 0.333f }, { "res", 0.78f }, { "drive", 0.4472f }, { "keytrk", 0.9f }, { "att", 0.0f }, { "dec", 0.193f }, { "sus", 0.1f }, { "rel", 0.049f }, { "level", 0.8f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset4Values[] = { { "wtIndex", 0.2f }, { "wtpos", 0.6f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.693f }, { "res", 0.3f }, { "drive", 0.3873f }, { "keytrk", 0.5f }, { "att", 0.022f }, { "dec", 0.25f }, { "sus", 0.85f }, { "rel", 0.122f }, { "level", 0.8f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset5Values[] = { { "wtIndex", 0.1f }, { "wtpos", 0.0f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.784f }, { "res", 0.15f }, { "drive", 0.0f }, { "keytrk", 0.5f }, { "att", 0.022f }, { "dec", 0.25f }, { "sus", 0.9f }, { "rel", 0.099f }, { "level", 0.85f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset6Values[] = { { "wtIndex", 0.4f }, { "wtpos", 0.5f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.593f }, { "res", 0.2f }, { "drive", 0.0f }, { "keytrk", 0.4f }, { "att", 0.387f }, { "dec", 0.474f }, { "sus", 0.8f }, { "rel", 0.559f }, { "level", 0.7f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset7Values[] = { { "wtIndex", 0.3f }, { "wtpos", 0.35f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.534f }, { "res", 0.2f }, { "drive", 0.0f }, { "keytrk", 0.4f }, { "att", 0.316f }, { "dec", 0.474f }, { "sus", 0.75f }, { "rel", 0.474f }, { "level", 0.7f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset8Values[] = { { "wtIndex", 0.4f }, { "wtpos", 0.2f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.634f }, { "res", 0.15f }, { "drive", 0.0f }, { "keytrk", 0.6f }, { "att", 0.022f }, { "dec", 0.25f }, { "sus", 0.35f }, { "rel", 0.193f }, { "level", 0.85f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset9Values[] = { { "wtIndex", 0.4f }, { "wtpos", 0.8f }, { "ftype", 0.0f }, { "slope", 0.0f }, { "cutoff", 0.826f }, { "res", 0.1f }, { "drive", 0.0f }, { "keytrk", 0.8f }, { "att", 0.0f }, { "dec", 0.387f }, { "sus", 0.05f }, { "rel", 0.316f }, { "level", 0.8f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset10Values[] = { { "wtIndex", 0.2f }, { "wtpos", 0.25f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.735f }, { "res", 0.35f }, { "drive", 0.3162f }, { "keytrk", 0.7f }, { "att", 0.0f }, { "dec", 0.158f }, { "sus", 0.0f }, { "rel", 0.078f }, { "level", 0.9f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset11Values[] = { { "wtIndex", 0.5f }, { "wtpos", 0.9f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.492f }, { "res", 0.55f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.474f }, { "dec", 0.559f }, { "sus", 0.7f }, { "rel", 0.661f }, { "level", 0.7f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset12Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.1342f }, { "oct", 0.3333f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.3591f }, { "res", 0.3936f }, { "drive", 0.3464f }, { "keytrk", 0.6f }, { "att", 0.0f }, { "dec", 0.3051f }, { "sus", 0.72f }, { "rel", 0.0935f }, { "level", 0.4624f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset13Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.4406f }, { "oct", 0.3333f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.4279f }, { "res", 0.3614f }, { "drive", 0.3464f }, { "keytrk", 0.6f }, { "att", 0.0f }, { "dec", 0.2982f }, { "sus", 0.72f }, { "rel", 0.119f }, { "level", 0.4624f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset14Values[] = { { "wtIndex", 0.7f }, { "wtpos", 0.0766f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5427f }, { "res", 0.463f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.5718f }, { "sus", 0.7f }, { "rel", 0.7164f }, { "level", 0.4752f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset15Values[] = { { "wtIndex", 0.7f }, { "wtpos", 0.4811f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.4902f }, { "res", 0.5819f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.4783f }, { "sus", 0.7f }, { "rel", 0.5983f }, { "level", 0.4752f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset16Values[] = { { "wtIndex", 1.0f }, { "wtpos", 0.8111f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5097f }, { "res", 0.4534f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.4881f }, { "sus", 0.7f }, { "rel", 0.693f }, { "level", 0.5265f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset17Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.4909f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5033f }, { "res", 0.4906f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.5482f }, { "sus", 0.7f }, { "rel", 0.6004f }, { "level", 0.4192f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset18Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.6874f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5992f }, { "res", 0.3882f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.4812f }, { "sus", 0.7f }, { "rel", 0.6685f }, { "level", 0.4192f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset19Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.4191f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.4582f }, { "res", 0.6032f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.613f }, { "sus", 0.7f }, { "rel", 0.6162f }, { "level", 0.4192f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset20Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.2735f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5133f }, { "res", 0.4717f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.5987f }, { "sus", 0.7f }, { "rel", 0.7109f }, { "level", 0.4192f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset21Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.5715f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5249f }, { "res", 0.4749f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.6217f }, { "sus", 0.7f }, { "rel", 0.6312f }, { "level", 0.4192f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset22Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.89f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.4751f }, { "res", 0.3912f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.6126f }, { "sus", 0.7f }, { "rel", 0.5833f }, { "level", 0.4192f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset23Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.5338f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.4233f }, { "res", 0.5863f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.5667f }, { "sus", 0.7f }, { "rel", 0.7084f }, { "level", 0.4192f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset24Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.5347f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5362f }, { "res", 0.4749f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.5602f }, { "sus", 0.7f }, { "rel", 0.6196f }, { "level", 0.4192f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset25Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.1822f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5506f }, { "res", 0.5615f }, { "drive", 0.0f }, { "keytrk", 0.3f }, { "att", 0.45f }, { "dec", 0.516f }, { "sus", 0.7f }, { "rel", 0.6162f }, { "level", 0.4192f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset26Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.3471f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5809f }, { "res", 0.1826f }, { "drive", 0.0f }, { "keytrk", 0.5f }, { "att", 0.02f }, { "dec", 0.3009f }, { "sus", 0.8f }, { "rel", 0.1122f }, { "level", 0.4809f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset27Values[] = { { "wtIndex", 1.0f }, { "wtpos", 0.124f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.7282f }, { "res", 0.1873f }, { "drive", 0.0f }, { "keytrk", 0.65f }, { "att", 0.015f }, { "dec", 0.2689f }, { "sus", 0.28f }, { "rel", 0.2725f }, { "level", 0.6194f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset28Values[] = { { "wtIndex", 1.0f }, { "wtpos", 0.0652f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.594f }, { "res", 0.1427f }, { "drive", 0.0f }, { "keytrk", 0.65f }, { "att", 0.015f }, { "dec", 0.2995f }, { "sus", 0.28f }, { "rel", 0.2682f }, { "level", 0.6194f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset29Values[] = { { "wtIndex", 1.0f }, { "wtpos", 0.1759f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.7086f }, { "res", 0.081f }, { "drive", 0.0f }, { "keytrk", 0.65f }, { "att", 0.015f }, { "dec", 0.292f }, { "sus", 0.28f }, { "rel", 0.1835f }, { "level", 0.6194f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset30Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.0626f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.6433f }, { "res", 0.1138f }, { "drive", 0.0f }, { "keytrk", 0.65f }, { "att", 0.015f }, { "dec", 0.3157f }, { "sus", 0.28f }, { "rel", 0.2321f }, { "level", 0.4932f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset31Values[] = { { "wtIndex", 1.0f }, { "wtpos", 0.6562f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.6221f }, { "res", 0.1851f }, { "drive", 0.0f }, { "keytrk", 0.65f }, { "att", 0.015f }, { "dec", 0.3291f }, { "sus", 0.28f }, { "rel", 0.1683f }, { "level", 0.6194f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset32Values[] = { { "wtIndex", 1.0f }, { "wtpos", 0.7984f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.6049f }, { "res", 0.1511f }, { "drive", 0.0f }, { "keytrk", 0.65f }, { "att", 0.015f }, { "dec", 0.2862f }, { "sus", 0.28f }, { "rel", 0.1607f }, { "level", 0.6194f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset33Values[] = { { "wtIndex", 0.8f }, { "wtpos", 0.0565f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.6902f }, { "res", 0.2531f }, { "drive", 0.3162f }, { "keytrk", 0.5f }, { "att", 0.022f }, { "dec", 0.1968f }, { "sus", 0.86f }, { "rel", 0.076f }, { "level", 0.5662f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset34Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.8923f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.678f }, { "res", 0.2627f }, { "drive", 0.3162f }, { "keytrk", 0.5f }, { "att", 0.022f }, { "dec", 0.2678f }, { "sus", 0.86f }, { "rel", 0.1312f }, { "level", 0.4809f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset35Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.1934f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.7078f }, { "res", 0.2178f }, { "drive", 0.3162f }, { "keytrk", 0.5f }, { "att", 0.022f }, { "dec", 0.29f }, { "sus", 0.86f }, { "rel", 0.1261f }, { "level", 0.4809f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset36Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.0692f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.7733f }, { "res", 0.1734f }, { "drive", 0.3162f }, { "keytrk", 0.5f }, { "att", 0.022f }, { "dec", 0.2836f }, { "sus", 0.86f }, { "rel", 0.139f }, { "level", 0.4809f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset37Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.6811f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.6568f }, { "res", 0.2562f }, { "drive", 0.3162f }, { "keytrk", 0.5f }, { "att", 0.022f }, { "dec", 0.247f }, { "sus", 0.86f }, { "rel", 0.0884f }, { "level", 0.4809f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset38Values[] = { { "wtIndex", 1.0f }, { "wtpos", 0.5775f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.6008f }, { "res", 0.1742f }, { "drive", 0.0f }, { "keytrk", 0.4f }, { "att", 0.35f }, { "dec", 0.5429f }, { "sus", 0.78f }, { "rel", 0.5358f }, { "level", 0.5419f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset39Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.4082f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5271f }, { "res", 0.164f }, { "drive", 0.0f }, { "keytrk", 0.4f }, { "att", 0.35f }, { "dec", 0.3987f }, { "sus", 0.78f }, { "rel", 0.5311f }, { "level", 0.4315f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset40Values[] = { { "wtIndex", 0.9f }, { "wtpos", 0.7486f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.5749f }, { "res", 0.1453f }, { "drive", 0.0f }, { "keytrk", 0.4f }, { "att", 0.35f }, { "dec", 0.4519f }, { "sus", 0.78f }, { "rel", 0.5768f }, { "level", 0.9798f }, { "volume", 0.7f } };
inline constexpr PresetValue kPreset41Values[] = { { "wtIndex", 0.8f }, { "wtpos", 0.4313f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.6754f }, { "res", 0.2717f }, { "drive", 0.3162f }, { "keytrk", 0.7f }, { "att", 0.0f }, { "dec", 0.1796f }, { "sus", 0.0f }, { "rel", 0.0757f }, { "level", 0.5953f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset42Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.6639f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.7684f }, { "res", 0.4042f }, { "drive", 0.3162f }, { "keytrk", 0.7f }, { "att", 0.0f }, { "dec", 0.156f }, { "sus", 0.0f }, { "rel", 0.0799f }, { "level", 0.5055f }, { "volume", 0.8f } };
inline constexpr PresetValue kPreset43Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.1417f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.6579f }, { "res", 0.388f }, { "drive", 0.2887f }, { "keytrk", 0.6f }, { "att", 0.0f }, { "dec", 0.1175f }, { "sus", 0.05f }, { "rel", 0.0604f }, { "level", 0.4932f }, { "volume", 0.8f }, { "arpOn", 1.0f }, { "arpMode", 0.0f }, { "arpRate", 0.5f }, { "arpGate", 0.5929f } };
inline constexpr PresetValue kPreset44Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.7422f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.7251f }, { "res", 0.2957f }, { "drive", 0.2887f }, { "keytrk", 0.6f }, { "att", 0.0f }, { "dec", 0.1458f }, { "sus", 0.05f }, { "rel", 0.089f }, { "level", 0.4932f }, { "volume", 0.8f }, { "arpOn", 1.0f }, { "arpMode", 0.0f }, { "arpRate", 0.5f }, { "arpGate", 0.5711f } };
inline constexpr PresetValue kPreset45Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.217f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.764f }, { "res", 0.4644f }, { "drive", 0.2887f }, { "keytrk", 0.6f }, { "att", 0.0f }, { "dec", 0.1353f }, { "sus", 0.05f }, { "rel", 0.0444f }, { "level", 0.4932f }, { "volume", 0.8f }, { "arpOn", 1.0f }, { "arpMode", 0.0f }, { "arpRate", 0.5f }, { "arpGate", 0.5202f } };
inline constexpr PresetValue kPreset46Values[] = { { "wtIndex", 0.6f }, { "wtpos", 0.3285f }, { "ftype", 0.0f }, { "slope", 1.0f }, { "cutoff", 0.6709f }, { "res", 0.4315f }, { "drive", 0.2887f }, { "keytrk", 0.6f }, { "att", 0.0f }, { "dec", 0.1507f }, { "sus", 0.05f }, { "rel", 0.0538f }, { "level", 0.4932f }, { "volume", 0.8f }, { "arpOn", 1.0f }, { "arpMode", 0.0f }, { "arpRate", 0.5f }, { "arpGate", 0.6167f } };

inline constexpr int kNumPresets = 47;
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
    { "Racing Destruction Kit 5", "Bass", kPreset12Values, 15 },
    { "Racing Destruction Kit 6", "Bass", kPreset13Values, 15 },
    { "GG Sisters 4", "FX", kPreset14Values, 14 },
    { "GG Sisters 5", "FX", kPreset15Values, 14 },
    { "Leaderboard 0", "FX", kPreset16Values, 14 },
    { "Racing Destruction Kit 6 2", "FX", kPreset17Values, 14 },
    { "Racing Destruction Kit 7", "FX", kPreset18Values, 14 },
    { "Racing Destruction Kit 8", "FX", kPreset19Values, 14 },
    { "Spindizzy", "FX", kPreset20Values, 14 },
    { "Spindizzy 2", "FX", kPreset21Values, 14 },
    { "Spindizzy 3", "FX", kPreset22Values, 14 },
    { "Spindizzy 4", "FX", kPreset23Values, 14 },
    { "Spindizzy 5", "FX", kPreset24Values, 14 },
    { "Spindizzy 6", "FX", kPreset25Values, 14 },
    { "Neochip Base 1", "User", kPreset26Values, 14 },
    { "Leaderboard 7", "Keys", kPreset27Values, 14 },
    { "Leaderboard 8", "Keys", kPreset28Values, 14 },
    { "Leaderboard 9", "Keys", kPreset29Values, 14 },
    { "Racing Destruction Kit 1", "Keys", kPreset30Values, 14 },
    { "Uridium 1", "Keys", kPreset31Values, 14 },
    { "Uridium 2", "Keys", kPreset32Values, 14 },
    { "Commando 18", "Lead", kPreset33Values, 14 },
    { "Racing Destruction Kit 2", "Lead", kPreset34Values, 14 },
    { "Racing Destruction Kit 3", "Lead", kPreset35Values, 14 },
    { "Racing Destruction Kit 4", "Lead", kPreset36Values, 14 },
    { "Racing Destruction Kit 5 2", "Lead", kPreset37Values, 14 },
    { "Leaderboard 0 2", "Pad", kPreset38Values, 14 },
    { "Racing Destruction Kit 5 3", "Pad", kPreset39Values, 14 },
    { "Uridium 1 2", "Pad", kPreset40Values, 14 },
    { "Commando 14", "Pluck", kPreset41Values, 14 },
    { "Racing Destruction Kit 9", "Pluck", kPreset42Values, 14 },
    { "Spindizzy 10", "Seq", kPreset43Values, 18 },
    { "Spindizzy 7", "Seq", kPreset44Values, 18 },
    { "Spindizzy 8", "Seq", kPreset45Values, 18 },
    { "Spindizzy 9", "Seq", kPreset46Values, 18 },
};
} // namespace params
