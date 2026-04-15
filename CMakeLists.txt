cmake_minimum_required(VERSION 3.22)
project(SFXShifter VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(C:/Users/joelc/Documents/SFXShifter/JUCE ./JUCE)

juce_add_plugin(SFXShifter
    PLUGIN_MANUFACTURER_CODE Joel
    PLUGIN_CODE Sfxs
    FORMATS VST3
    PRODUCT_NAME "SFX Shifter"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(SFXShifter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    C:/Users/joelc/Documents/SFXShifter/rubberband/single/RubberBandSingle.cpp
)

target_include_directories(SFXShifter PRIVATE
    C:/Users/joelc/Documents/SFXShifter/rubberband
)

target_compile_definitions(SFXShifter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    NOMINMAX=1
)

target_link_libraries(SFXShifter PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)