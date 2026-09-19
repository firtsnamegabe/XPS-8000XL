cmake_minimum_required(VERSION 3.22)

# Project/target name is "XPS8000XL" (CMake identifiers can't have spaces).
# The DAW-visible plugin name is PRODUCT_NAME below - "XPS-8000XL", short
# for "Xolo Production Sampler 8000XL".
project(XPS8000XL VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE
#
# Option A (default): fetch JUCE automatically at configure time.
# Option B: if you already have a local JUCE checkout, point JUCE_DIR at it
#           and skip FetchContent, e.g.:
#           cmake -B build -DJUCE_LOCAL_DIR=/path/to/JUCE
# ---------------------------------------------------------------------------
if (JUCE_LOCAL_DIR)
    add_subdirectory(${JUCE_LOCAL_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(XPS8000XL
    COMPANY_NAME              "Xolo Audio"
    BUNDLE_ID                 com.xoloaudio.xps8000xl
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         TRUE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    PLUGIN_MANUFACTURER_CODE  Xolo
    PLUGIN_CODE               X8xl
    FORMATS                   VST3 Standalone
    PRODUCT_NAME               "XPS-8000XL"
    # Off deliberately: JUCE's auto-manifest step runs a POST_BUILD shell
    # command (`sh -c "cmake -E remove -f <path>/moduleinfo.json ..."`)
    # that only escapes spaces in the build path, not other shell
    # metacharacters like parentheses - so it breaks if the project is
    # ever built from a path containing them (e.g. a folder literally
    # named "build (2)", which is exactly what happened). Most VST3 hosts scan fine without
    # this manifest; it mainly speeds up scanning in hosts that support
    # it. Re-enable if you specifically need it AND can guarantee a
    # shell-safe build path (no spaces, parens, or other special
    # characters anywhere in it).
    VST3_AUTO_MANIFEST        FALSE
)

target_sources(XPS8000XL
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/SamplerEngine.cpp
        Source/Pad.cpp
        Source/PadComponent.cpp
        Source/BankTabsComponent.cpp
        Source/WaveformEditorComponent.cpp
        Source/EditParamsComponent.cpp
        Source/ChopParamsComponent.cpp
        Source/LayersPanelComponent.cpp
        Source/MasterPanelComponent.cpp
)

target_compile_definitions(XPS8000XL
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Enables JUCE's bundled MP3 decoder, off by default. JUCE's own
        # docs attach an explicit disclaimer to this flag: the decoder
        # code isn't guaranteed free of 3rd-party IP claims, and enabling
        # it is "at your own risk" - flagged to and confirmed by the user
        # before turning this on, not a default anyone should flip
        # silently. Decode-only (no MP3 encoding/export - JUCE doesn't
        # provide an encoder here regardless of this flag).
        JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(XPS8000XL
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
