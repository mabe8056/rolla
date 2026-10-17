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
        resources/SAMPLE_MANIFEST.json
        resources/mix_profiles.json
        resources/main_sum_trim.json
        resources/mix_makeup.json
        resources/fonts/Unbounded-SemiBold.ttf
        resources/fonts/Unbounded-Bold.ttf
        resources/fonts/Unbounded-ExtraBold.ttf
        resources/fonts/MartianMono-SemiCond-Regular.ttf
        resources/fonts/MartianMono-SemiCond-Medium.ttf
        resources/fonts/MartianMono-SemiCond-SemiBold.ttf
        resources/fonts/MartianMono-SemiCond-Bold.ttf
        resources/fonts/OFL-UI.txt
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
        src/ui/Theme.cpp
        src/Engine/Engine.cpp
        src/Engine/Sampler.cpp
        src/Engine/SampleLibrary.cpp
        src/Sampler/SamplePrep.cpp
        src/Sampler/UserSlots.cpp
        src/Generator/Generator.cpp
        src/Generator/Presets.cpp
        src/Generator/MidiFile.cpp
        src/Drums/DrumMachine.cpp
        src/Drums/DrumPattern.cpp
        src/Mix/MixProgram.cpp
        src/Mix/MixRuntime.cpp
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
    src/Sampler/SamplePrep.cpp
    src/Generator/Generator.cpp
    src/Generator/Presets.cpp
    src/Generator/MidiFile.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
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
    src/Sampler/SamplePrep.cpp
    src/Generator/Generator.cpp
    src/Generator/Presets.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
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

add_executable(rolla-ui-shot
    src/UiSnapshot.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/Theme.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Sampler/UserSlots.cpp
    src/Generator/Generator.cpp
    src/Generator/Presets.cpp
    src/Generator/MidiFile.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
)

target_compile_definitions(rolla-ui-shot
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(rolla-ui-shot
    PRIVATE
        RollaData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_executable(rolla-sampler-test
    tests/sampler/sampler_test.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Sampler/UserSlots.cpp
    src/Generator/Generator.cpp
    src/Generator/Presets.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
)

target_include_directories(rolla-sampler-test PRIVATE src)

target_compile_definitions(rolla-sampler-test
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(rolla-sampler-test
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_executable(rolla-roll-test
    tests/roll/roll_test.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/Theme.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Sampler/UserSlots.cpp
    src/Generator/Generator.cpp
    src/Generator/Presets.cpp
    src/Generator/MidiFile.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
)

target_include_directories(rolla-roll-test PRIVATE src)

target_compile_definitions(rolla-roll-test
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(rolla-roll-test
    PRIVATE
        RollaData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_executable(rolla-mix-render
    src/MixRender.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Sampler/UserSlots.cpp
    src/Generator/Generator.cpp
    src/Generator/Presets.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
)

target_compile_definitions(rolla-mix-render
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(rolla-mix-render
    PRIVATE
        RollaData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Offline variety check. No JUCE. Not part of the CI workflow.
add_executable(rolla-variety
    tools/variety_main.cpp
    tools/PatternStats.cpp
    src/Generator/Generator.cpp
)
target_include_directories(rolla-variety PRIVATE src tools)
target_compile_options(rolla-variety PRIVATE -Wall -Wextra -Wpedantic)

# 20 rolls per style to MIDI + WAV, plus tools/out/STATS.md.
# Left out of CI: eighty renders is a local listening pass.
add_executable(rolla-rolls
    tools/roll_batch.cpp
    tools/PatternStats.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Generator/Generator.cpp
    src/Generator/Presets.cpp
    src/Generator/MidiFile.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
)
target_include_directories(rolla-rolls PRIVATE src tools)
target_compile_definitions(rolla-rolls
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)
target_link_libraries(rolla-rolls
    PRIVATE
        RollaData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
