#pragma once

namespace dsp
{
/** Stub multimode SVF — API reserved for phase 3+. */
class StateVariableFilter
{
public:
    enum class Type
    {
        lowPass,
        highPass,
        bandPass
    };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setType (Type type) noexcept;
    void setCutoffHz (float hz) noexcept;
    void setResonance (float q) noexcept;
    float processSample (float input) noexcept;

private:
    double sampleRate_ { 44100.0 };
    Type type_ { Type::lowPass };
    float cutoffHz_ { 1000.0f };
    float resonance_ { 0.707f };
};
} // namespace dsp
