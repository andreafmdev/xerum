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
        Source/bridge/StateChannel.cpp
        Source/bridge/WebRelays.cpp
        Source/bridge/MeterChannel.cpp
        Source/bridge/WebAssets.cpp
        Source/engine/SynthEngine.cpp
        Source/engine/VoiceManager.cpp
        Source/engine/SynthVoice.cpp
        Source/engine/ModMatrix.cpp
        Source/dsp/Lfo.cpp
        Source/dsp/WavetableOscillator.cpp
        Source/dsp/StateVariableFilter.cpp
        Source/dsp/ADSREnvelope.cpp
        Source/dsp/WavetableBlob.cpp
        Source/dsp/MipTable.cpp
        Source/dsp/WavetableStore.cpp
        Source/parameters/ParameterLayout.cpp
        Source/ui/XerumKeyboard.cpp
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

# --- Wavetables ------------------------------------------------------------
# Sempre incorporate (a differenza del bundle web, che è solo in Release):
# senza tavole il sintetizzatore non ha niente da suonare.
file(GLOB XERUM_WAVETABLES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/wavetables/*.xwt)
if(NOT XERUM_WAVETABLES)
    message(FATAL_ERROR "Nessuna tavola in Resources/wavetables. Generale con: node scripts/fetch-wavetables.mjs")
endif()
# HEADER_NAME esplicito: il default (BinaryData.h) collide con il target
# WebUIAssets, che genera già un BinaryData.h proprio per l'embedding web.
juce_add_binary_data(WavetableAssets SOURCES ${XERUM_WAVETABLES} NAMESPACE WavetableAssets HEADER_NAME WavetableData.h)
target_link_libraries(SerumStyleSynth PRIVATE WavetableAssets)

# --- Unit tests -----------------------------------------------------------
# Console app: gira juce::UnitTestRunner su dsp/ ed engine/. Non tocca la UI
# né l'host, quindi niente juce_gui_extra e niente WebView.
juce_add_console_app(XerumTests PRODUCT_NAME "XerumTests")

target_sources(XerumTests
    PRIVATE
        Tests/main.cpp
        Tests/WavetableTests.cpp
        Tests/EnvelopeFilterTests.cpp
        Tests/EngineTests.cpp
        Tests/PresetValueTests.cpp
        Tests/ParameterSeamTests.cpp
        Tests/ModMatrixTests.cpp
        Tests/LfoTests.cpp
        Source/dsp/WavetableBlob.cpp
        Source/dsp/MipTable.cpp
        Source/dsp/WavetableStore.cpp
        Source/dsp/Lfo.cpp
        Source/engine/ModMatrix.cpp
        Source/dsp/WavetableOscillator.cpp
        Source/dsp/ADSREnvelope.cpp
        Source/dsp/StateVariableFilter.cpp
        Source/engine/SynthEngine.cpp
        Source/engine/VoiceManager.cpp
        Source/engine/SynthVoice.cpp
        Source/parameters/ParameterLayout.cpp)

target_include_directories(XerumTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(XerumTests
    PRIVATE
        JUCE_UNIT_TESTS=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

serum_style_synth_set_warnings(XerumTests)

target_link_libraries(XerumTests
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_data_structures
        juce::juce_audio_processors
        WavetableAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

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

# --- Web UI embedding (Release) -------------------------------------------
option(XERUM_EMBED_WEBUI "Embed WebUI/dist into the plugin binary (Release)" OFF)

find_program(XERUM_PNPM pnpm)
if(XERUM_PNPM)
    add_custom_target(webui
        COMMAND ${XERUM_PNPM} --dir ${CMAKE_CURRENT_SOURCE_DIR}/WebUI ui:build
        COMMAND ${XERUM_PNPM} --dir ${CMAKE_CURRENT_SOURCE_DIR}/WebUI build
        COMMENT "Building the Web UI bundle (WebUI/dist)")
endif()

if(XERUM_EMBED_WEBUI)
    set(XERUM_WEBUI_DIST ${CMAKE_CURRENT_SOURCE_DIR}/WebUI/dist)
    if(NOT EXISTS ${XERUM_WEBUI_DIST}/index.html)
        message(FATAL_ERROR "XERUM_EMBED_WEBUI=ON but ${XERUM_WEBUI_DIST}/index.html is missing.\nBuild it first: cd WebUI && pnpm ui:build && pnpm build   (or: cmake --build --preset macos-debug --target webui)")
    endif()
    file(GLOB_RECURSE XERUM_WEBUI_FILES CONFIGURE_DEPENDS ${XERUM_WEBUI_DIST}/*)
    juce_add_binary_data(WebUIAssets SOURCES ${XERUM_WEBUI_FILES} NAMESPACE WebUIAssets)
    target_link_libraries(SerumStyleSynth PRIVATE WebUIAssets)
    target_compile_definitions(SerumStyleSynth PRIVATE XERUM_EMBED_WEBUI=1)
endif()
