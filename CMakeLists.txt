cmake_minimum_required(VERSION 3.22)
project(Bombo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE via FetchContent, pinned tag. No system install required.
# Override with -DBOMBO_USE_SYSTEM_JUCE=ON to use find_package instead.
option(BOMBO_USE_SYSTEM_JUCE "Use system-installed JUCE via find_package" OFF)

if(BOMBO_USE_SYSTEM_JUCE)
    find_package(JUCE CONFIG REQUIRED)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.12
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# AU is macOS-only; JUCE silently drops it on other platforms.
juce_add_plugin(Bombo
    COMPANY_NAME "Hyperfocus DSP"
    COMPANY_WEBSITE "https://hyperfocusdsp.com"
    COMPANY_EMAIL "hello@hyperfocusdsp.com"
    PLUGIN_MANUFACTURER_CODE Hfds
    PLUGIN_CODE Bomb
    BUNDLE_ID com.hyperfocusdsp.bombo
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Bombo"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Instrument" "Drum"
    AU_MAIN_TYPE "kAudioUnitType_MusicDevice"
    DESCRIPTION "Half kick instrument, half 1992 BBS. One plugin.")

juce_add_binary_data(BomboResources
    SOURCES
        Resources/Fonts/AllertaStencil-Regular.ttf
        Resources/Themes/bandw.json
        Resources/Themes/phosphor.json
        Resources/Themes/nightrun.json)

target_sources(Bombo
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/X11CompositorClaim.cpp
        Source/Parameters.h
        Source/GUI/Colours.h
        Source/GUI/Theme/Palette.h
        Source/GUI/Theme/ThemeProvider.h
        Source/GUI/Theme/ThemeProvider.cpp
        Source/GUI/Theme/ThemeLoader.h
        Source/GUI/Theme/ThemeLoader.cpp
        Source/State/PersistentState.h
        Source/State/PersistentState.cpp
        Source/GUI/Fonts.h
        Source/GUI/Fonts.cpp
        Source/GUI/BalanceFader.h
        Source/GUI/BomboLookAndFeel.h
        Source/GUI/BpmDisplay.h
        Source/GUI/DiceButton.h
        Source/GUI/FaceplatePanel.h
        Source/GUI/FaceplatePanel.cpp
        Source/GUI/SampleSlotWidget.h
        Source/GUI/WaveBuffer.h
        Source/GUI/ScopeComponent.h
        Source/GUI/ScopeComponent.cpp
        Source/DSP/Oscillators.h
        Source/DSP/Envelopes.h
        Source/DSP/ClickGen.h
        Source/DSP/NoiseGen.h
        Source/DSP/SampleSlot.h
        Source/DSP/VoiceClip.h
        Source/DSP/BombVoice.h
        Source/DSP/BiquadFilter.h
        Source/DSP/Delay.h
        Source/DSP/FdnReverb.h
        Source/DSP/Ducker.h
        Source/DSP/MultibandLimiter.h
        Source/DSP/MasterBus.h
        Source/DSP/RumbleChain.h)

target_compile_definitions(Bombo
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Linux X11: enable XRender so JUCE selects an ARGB visual when
        # setOpaque(false) is set on the top-level window. Required for
        # the bomb-tail corner wedges to be truly transparent.
        JUCE_USE_XRENDER=1)

# LTO is a big build-time RAM hog (lto1 can take >4GB on the
# Standalone link with -flto). Default ON for release builds; flip
# OFF on memory-limited boxes via -DBOMBO_LTO=OFF.
option(BOMBO_LTO "Link with -flto (juce_recommended_lto_flags)" ON)

target_link_libraries(Bombo
    PRIVATE
        BomboResources
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
    PUBLIC
        juce::juce_recommended_config_flags)
if(BOMBO_LTO)
    target_link_libraries(Bombo PUBLIC juce::juce_recommended_lto_flags)
endif()

# X11 is needed for the compositor-selection claim that bridges JUCE's
# transparency check on Hyprland/XWayland (see Source/X11CompositorClaim.cpp).
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(Bombo PRIVATE ${X11_LIBRARIES})
    target_include_directories(Bombo PRIVATE ${X11_INCLUDE_DIR})
endif()

# ── DSP unit tests ────────────────────────────────────────────────
# JUCE UnitTestRunner. Builds a console exe that runs every registered
# UnitTest subclass; ctest treats nonzero exit as failure.
option(BOMBO_BUILD_TESTS "Build Bombo DSP unit tests" ON)
if(BOMBO_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(Bombo_Tests
        PRODUCT_NAME "Bombo_Tests")
    target_sources(Bombo_Tests PRIVATE
        tests/RunTests.cpp
        tests/PaletteTests.cpp
        Source/GUI/Theme/ThemeProvider.cpp
        Source/GUI/Theme/ThemeLoader.cpp
        Source/State/PersistentState.cpp)
    target_include_directories(Bombo_Tests PRIVATE Source tests)
    target_compile_definitions(Bombo_Tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(Bombo_Tests
        PRIVATE
            BomboResources
            juce::juce_core
            juce::juce_data_structures
            juce::juce_events
            juce::juce_graphics
            juce::juce_dsp
            juce::juce_audio_basics
        PUBLIC
            juce::juce_recommended_config_flags)
    if(BOMBO_LTO)
        target_link_libraries(Bombo_Tests PUBLIC juce::juce_recommended_lto_flags)
    endif()
    add_test(NAME bombo_unit_tests COMMAND Bombo_Tests)
endif()
