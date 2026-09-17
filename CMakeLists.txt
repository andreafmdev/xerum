# SerumStyleSynth — Phase 1 scaffolding
cmake_minimum_required(VERSION 3.22)

project(SerumStyleSynth VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Avoid building JUCE extras / examples
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

add_subdirectory(external/JUCE)

include(cmake/CompilerWarnings.cmake)

juce_add_plugin(SerumStyleSynth
    COMPANY_NAME "Xerum"
    BUNDLE_ID "com.xerum.serumstylesynth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Xeru
    PLUGIN_CODE Ss01
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "SerumStyleSynth"
)

target_sources(SerumStyleSynth
    PRIVATE
        Source/plugin/PluginProcessor.cpp
        Source/plugin/PluginEditor.cpp
        Source/engine/SynthEngine.cpp
        Source/engine/VoiceManager.cpp
        Source/engine/SynthVoice.cpp
        Source/dsp/WavetableOscillator.cpp
        Source/dsp/StateVariableFilter.cpp
        Source/dsp/ADSREnvelope.cpp
        Source/parameters/ParameterLayout.cpp
)

target_include_directories(SerumStyleSynth
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_compile_definitions(SerumStyleSynth
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

serum_style_synth_set_warnings(SerumStyleSynth)

target_link_libraries(SerumStyleSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
