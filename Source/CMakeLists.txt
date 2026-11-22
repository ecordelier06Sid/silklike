cmake_minimum_required(VERSION 3.22)
project(SilkLike VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SilkLike
    COMPANY_NAME "HomeStudio"
    PLUGIN_MANUFACTURER_CODE Hsmu
    PLUGIN_CODE Slkl
    FORMATS VST3
    PRODUCT_NAME "SilkLike Vocal"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(SilkAssets SOURCES
    Source/faceplate.jpg
    Source/knob.png
    Source/meter.png
    Source/meter_bezel.png)
set_target_properties(SilkAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(SilkLike PRIVATE Source/PluginProcessor.cpp)

target_compile_definitions(SilkLike PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SilkLike
    PRIVATE SilkAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
