# Xerum — wavetable synth on JUCE 9, built as C++23.
cmake_minimum_required(VERSION 3.22)

project(Xerum VERSION 0.1.0 LANGUAGES C CXX)

# C++23. JUCE 9 chiede cxx_std_17 come INTERFACE, cioe' un minimo per chi lo consuma e non un
# tetto: alzare lo standard non lo viola. Misurato prima di adottarlo, con build pulite di tutti i
# target a 17, 20 e 23: stessi test verdi e lo stesso identico insieme di dieci warning, zero nuovi.
#
# Il punto da tenere d'occhio e' la libreria standard, non il compilatore: Apple clang regge il
# linguaggio C++23, ma libc++ e' indietro su parti della libreria. Se un <header> o una funzione
# C++23 risulta assente, e' quello il motivo, e la risposta e' non usarla — non tornare indietro
# con lo standard.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XERUM_COPY_PLUGIN "Copia AU/VST3 nelle cartelle plugin dell'utente dopo la build" ON)
option(XERUM_EMBED_WEBUI "Embed WebUI/dist into the plugin binary (Release)" OFF)

# Avoid building JUCE extras / examples
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

# JUCE resta un submodule pinnato (9.0.2), non FetchContent: WebUI/package.json linka il package
# webview-interop da dentro external/JUCE, quindi il checkout deve esistere prima di ogni configure.
add_subdirectory(external/JUCE)

include(cmake/CompilerWarnings.cmake)

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

# --- Core: dsp/, engine/, parameters/ ----------------------------------------
# Un solo elenco per il plugin e per i test. INTERFACE e non STATIC: i sorgenti si compilano
# dentro ogni consumatore con i *suoi* flag JUCE (il plugin ha JUCE_WEB_BROWSER=1, i test 0), che
# e' l'unico modo perche' i moduli JUCE inclusi dagli header vedano definizioni coerenti.
add_library(XerumCore INTERFACE)
set(XERUM_CORE_SOURCES
    Source/dsp/ADSREnvelope.cpp
    Source/dsp/Chorus.cpp
    Source/dsp/Lfo.cpp
    Source/dsp/MipTable.cpp
    Source/dsp/PlateReverb.cpp
    Source/dsp/StateVariableFilter.cpp
    Source/dsp/StereoDelay.cpp
    Source/dsp/WavetableBlob.cpp
    Source/dsp/WavetableOscillator.cpp
    Source/dsp/WavetableStore.cpp
    Source/engine/Arpeggiator.cpp
    Source/engine/SynthEngine.cpp
    Source/engine/SynthVoice.cpp
    Source/engine/VoiceManager.cpp
    Source/parameters/ParameterLayout.cpp
    Source/state/StateToEngine.cpp
)
list(TRANSFORM XERUM_CORE_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
target_sources(XerumCore INTERFACE ${XERUM_CORE_SOURCES})
target_include_directories(XerumCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(XerumCore
    INTERFACE
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_data_structures
        juce::juce_dsp
        WavetableAssets)

# --- Plugin ----------------------------------------------------------------
# Identita' del plugin (BUNDLE_ID, codici AU/VST3, nome del bundle): cambiarli invalida le
# sessioni DAW salvate, non lo stato dei parametri. Vedi docs/build.md.
juce_add_plugin(Xerum
    COMPANY_NAME "Xerum"
    BUNDLE_ID "com.xerum.xerum"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    NEEDS_WEB_BROWSER TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${XERUM_COPY_PLUGIN}
    PLUGIN_MANUFACTURER_CODE Xeru
    PLUGIN_CODE Xrm1
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Xerum"
    # Icone di app e bundle dal marchio in Resources/branding (rigenerabili con build.mjs).
    # JUCE compone da queste due l'.icns della Standalone e l'icona del bundle VST3/AU.
    ICON_BIG "Resources/branding/icons/xerum.iconset/icon_512x512@2x.png"
    ICON_SMALL "Resources/branding/icons/xerum-128.png"
)

target_sources(Xerum
    PRIVATE
        Source/plugin/PluginProcessor.cpp
        Source/plugin/PluginEditor.cpp
        Source/bridge/StateChannel.cpp
        Source/bridge/WebRelays.cpp
        Source/bridge/MeterChannel.cpp
        Source/bridge/MidiChannel.cpp
        Source/bridge/MidiDeviceChannel.cpp
        Source/bridge/AudioSettingsChannel.cpp
        Source/bridge/WebAssets.cpp
)

target_compile_definitions(Xerum
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

xerum_set_warnings(Xerum)

# I flag "recommended" restano PUBLIC di proposito: e' cosi' che li linka l'esempio ufficiale di
# JUCE, perche' devono raggiungere anche i target di formato (VST3/AU/Standalone) generati da
# juce_add_plugin, che compilano il codice dei wrapper.
target_link_libraries(Xerum
    PRIVATE
        XerumCore
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Standalone: app nostra, finestra senza chrome -------------------------
# Due file, due target diversi, e la divisione non e' arbitraria:
#
# XerumWindowMac.mm sta su Xerum (il codice condiviso da AU, VST3 e Standalone) perche' le
# funzioni xerum::* che definisce sono chiamate dal bridge, che vive li'. Metterlo solo su
# Xerum_Standalone lascerebbe AU e VST3 con riferimenti non risolti in fase di link.
#
# E' il primo .mm del progetto: CMake, con OBJCXX non abilitato da project(), lo lascia nella
# lista di estensioni di CXX e clang lo compila come Objective-C++ in base all'estensione —
# stesso trattamento che ricevono i .mm dei moduli JUCE.
if(APPLE)
    target_sources(Xerum PRIVATE Source/standalone/XerumWindowMac.mm)
    target_link_libraries(Xerum PRIVATE "-framework Cocoa")
endif()

# L'app dello Standalone e' nostra: JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP spegne quella di JUCE
# (juce_audio_plugin_client_Standalone.cpp:58) e al suo posto chiede un juce_CreateApplication().
# Questo sorgente va sul target di formato, non su Xerum: e' quello a compilare il wrapper dello
# Standalone, ed e' l'unico che definisce JucePlugin_Build_Standalone.
if(TARGET Xerum_Standalone AND APPLE)
    target_sources(Xerum_Standalone PRIVATE Source/standalone/XerumStandaloneApp.cpp)
    target_compile_definitions(Xerum_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
endif()

# --- Unit tests -----------------------------------------------------------
# Console app: gira juce::UnitTestRunner su dsp/, engine/ e parameters/. Non tocca la UI ne'
# l'host, quindi niente juce_gui_extra e niente WebView.
juce_add_console_app(XerumTests PRODUCT_NAME "XerumTests")

target_sources(XerumTests
    PRIVATE
        Tests/main.cpp
        Tests/WavetableTests.cpp
        Tests/EnvelopeFilterTests.cpp
        Tests/EngineParamsTests.cpp
        Tests/VoiceStealingTests.cpp
        Tests/SaturationTests.cpp
        Tests/ParamCollectTests.cpp
        Tests/ModulationTests.cpp
        Tests/UnisonTests.cpp
        Tests/ModulationStressTests.cpp
        Tests/ControlRateTests.cpp
        Tests/ModulationDepthTests.cpp
        Tests/PresetGainStagingTests.cpp
        Tests/StateTreeTests.cpp
        Tests/NoteMaskTests.cpp
        Tests/PluginProcessorTests.cpp
        Tests/MidiDevicesTests.cpp
        Tests/AudioSettingsTests.cpp
        Tests/DelayTests.cpp
        Tests/FxRackTests.cpp
        # Il processore, senza editor (XERUM_HEADLESS_TESTS): il round-trip dello stato salvato
        # passa da qui, e prima non aveva nessun test.
        Source/plugin/PluginProcessor.cpp
        Tests/PresetValueTests.cpp
        Tests/ParameterSeamTests.cpp
        Tests/ModMatrixTests.cpp
        Tests/LfoTests.cpp
        Tests/ChorusTests.cpp
        Tests/ReverbTests.cpp
        Tests/GlideVoiceModeTests.cpp
        Tests/ArpeggiatorTests.cpp
        Tests/PitchBendTests.cpp
        Tests/SustainPedalTests.cpp)

target_compile_definitions(XerumTests
    PRIVATE
        JUCE_UNIT_TESTS=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        XERUM_HEADLESS_TESTS=1
        "JucePlugin_Name=\"Xerum\"")

xerum_set_warnings(XerumTests)

target_link_libraries(XerumTests
    PRIVATE
        XerumCore
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME XerumTests COMMAND XerumTests)

# --- Banco di misura ------------------------------------------------------
# Console app come i test, ma fuori da ctest: non asserisce niente, misura. Ha senso solo in
# Release (in Debug i numeri dicono quanto costa il codice non ottimizzato, che non interessa
# a nessuno), e per questo non dipende da nulla che la build di Debug debba costruire.
juce_add_console_app(XerumBench PRODUCT_NAME "XerumBench")

target_sources(XerumBench PRIVATE Tests/bench/BenchMain.cpp)

target_compile_definitions(XerumBench
    PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        XERUM_HEADLESS_TESTS=1
        "JucePlugin_Name=\"Xerum\"")

xerum_set_warnings(XerumBench)

# Gli stessi flag del plugin, LTO compresa: misurare con una ricetta di compilazione diversa da
# quella che viene spedita darebbe numeri di un binario che nessuno usa.
target_link_libraries(XerumBench
    PRIVATE
        XerumCore
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Web UI bundle ---------------------------------------------------------
# `webui` ricostruisce WebUI/dist quando cambiano i sorgenti web; in Release WebUIAssets ne
# dipende, cosi' il binario incorpora sempre l'ultima UI.
set(XERUM_WEBUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/WebUI)
set(XERUM_WEBUI_DIST ${XERUM_WEBUI_DIR}/dist)

find_program(XERUM_PNPM pnpm)
if(XERUM_PNPM)
    file(GLOB_RECURSE XERUM_WEBUI_SOURCES CONFIGURE_DEPENDS
        ${XERUM_WEBUI_DIR}/src/*
        ${XERUM_WEBUI_DIR}/packages/ui/src/*
        ${XERUM_WEBUI_DIR}/index.html
        ${XERUM_WEBUI_DIR}/package.json)
    add_custom_command(
        OUTPUT ${XERUM_WEBUI_DIST}/index.html
        COMMAND ${XERUM_PNPM} --dir ${XERUM_WEBUI_DIR} ui:build
        COMMAND ${XERUM_PNPM} --dir ${XERUM_WEBUI_DIR} build
        DEPENDS ${XERUM_WEBUI_SOURCES}
        COMMENT "Building the Web UI bundle (WebUI/dist)"
        VERBATIM)
    add_custom_target(webui DEPENDS ${XERUM_WEBUI_DIST}/index.html)
endif()

if(XERUM_EMBED_WEBUI)
    # L'elenco dei file va conosciuto a configure (juce_add_binary_data): la prima build della UI
    # resta a carico di chi configura, poi il target webui la tiene aggiornata.
    if(NOT EXISTS ${XERUM_WEBUI_DIST}/index.html)
        message(FATAL_ERROR "XERUM_EMBED_WEBUI=ON but ${XERUM_WEBUI_DIST}/index.html is missing.\nBuild it first: cd WebUI && pnpm ui:build && pnpm build   (or: cmake --build --preset macos-debug --target webui)")
    endif()
    file(GLOB_RECURSE XERUM_WEBUI_FILES CONFIGURE_DEPENDS ${XERUM_WEBUI_DIST}/*)
    juce_add_binary_data(WebUIAssets SOURCES ${XERUM_WEBUI_FILES} NAMESPACE WebUIAssets)
    if(TARGET webui)
        add_dependencies(WebUIAssets webui)
    endif()
    target_link_libraries(Xerum PRIVATE WebUIAssets)
    target_compile_definitions(Xerum PRIVATE XERUM_EMBED_WEBUI=1)
endif()
