cmake_minimum_required(VERSION 3.22)

project(ROLLA VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(juce)

juce_add_binary_data(RollaData
    SOURCES
        resources/presets.json
        resources/fonts/InstrumentSans-SemiBold.ttf
        resources/fonts/InstrumentSans-Bold.ttf
        resources/fonts/GeistMono-Regular.ttf
        resources/fonts/GeistMono-Medium.ttf
        resources/fonts/GeistMono-SemiBold.ttf
)

set(ROLLA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ROLLA_FORMATS AU)
endif()

juce_add_plugin(ROLLA
    COMPANY_NAME "Rola"
    BUNDLE_ID com.rolla.instrument
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Rola
    PLUGIN_CODE Roll
    FORMATS ${ROLLA_FORMATS}
    PRODUCT_NAME "ROLLA"
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
)

target_sources(ROLLA
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/Engine/Engine.cpp
        src/Engine/Sampler.cpp
        src/Engine/SampleLibrary.cpp
        src/Generator/Generator.cpp
        src/Generator/Presets.cpp
        src/Generator/MidiFile.cpp
)

target_compile_definitions(ROLLA
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(ROLLA
    PRIVATE
        RollaData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    PUBLIC
        juce::juce_recommended_config_flags
)

add_executable(rolla-render
    src/RenderMain.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Generator/Generator.cpp
    src/Generator/Presets.cpp
    src/Generator/MidiFile.cpp
)

target_compile_definitions(rolla-render
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(rolla-render
    PRIVATE
        RollaData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

juce_generate_juce_header(ROLLA)

add_executable(rolla-sample-check
    src/SampleCheck.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Generator/Generator.cpp
    src/Generator/Presets.cpp
)

target_compile_definitions(rolla-sample-check
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(rolla-sample-check
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
