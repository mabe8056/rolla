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
        src/Generator/PhraseBank.cpp
        src/Generator/Presets.cpp
        src/Generator/MidiFile.cpp
        src/Drums/DrumMachine.cpp
        src/Drums/DrumPattern.cpp
        src/Mix/MixProgram.cpp
        src/Mix/MixRuntime.cpp
)

target_compile_definitions(ROLLA
    PUBLIC
        # Samples only in the plugin: no preset or saved state brings the synth back.
        ROLLA_PLUGIN_BUILD=1
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

# A sample pack copied into each built plugin bundle, Contents/Resources/Samples,
# where ROLLA looks first. Audio stays out of the repo: point this at a folder on disk.
#   cmake -B build -DROLLA_SAMPLE_PACK_DIR=/path/to/pack
set(ROLLA_SAMPLE_PACK_DIR "" CACHE PATH "Sample pack folder copied into the built plugin bundles")
if(ROLLA_SAMPLE_PACK_DIR)
    if(NOT IS_DIRECTORY "${ROLLA_SAMPLE_PACK_DIR}")
        message(FATAL_ERROR "ROLLA_SAMPLE_PACK_DIR is not a folder: ${ROLLA_SAMPLE_PACK_DIR}")
    endif()
    set(rolla_pack_formats VST3 AU)
    if(APPLE)
        list(APPEND rolla_pack_formats Standalone)
    endif()
    foreach(rolla_format IN LISTS rolla_pack_formats)
        if(NOT TARGET ROLLA_${rolla_format})
            continue()
        endif()
        get_target_property(rolla_bundle ROLLA_${rolla_format} JUCE_PLUGIN_ARTEFACT_FILE)
        if(WIN32)
            add_custom_command(TARGET ROLLA_${rolla_format} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E remove_directory "${rolla_bundle}/Contents/Resources/Samples"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${ROLLA_SAMPLE_PACK_DIR}" "${rolla_bundle}/Contents/Resources/Samples"
                VERBATIM)
        else()
            # Copies (APFS clones on macOS), writes .rolla-pack.json, re-signs ad hoc.
            add_custom_command(TARGET ROLLA_${rolla_format} POST_BUILD
                COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tools/inject_pack.sh" "${rolla_bundle}" "${ROLLA_SAMPLE_PACK_DIR}"
                VERBATIM)
        endif()
    endforeach()
endif()

add_executable(rolla-render
    src/RenderMain.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Generator/Generator.cpp
    src/Generator/PhraseBank.cpp
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
    src/Generator/PhraseBank.cpp
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
    src/Generator/PhraseBank.cpp
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
    src/Generator/PhraseBank.cpp
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

# Master true-peak safety stage: guard DSP + every style x INTENSITY.
add_executable(rolla-tp-test
    tests/tpguard/tp_guard_test.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Sampler/UserSlots.cpp
    src/Generator/Generator.cpp
    src/Generator/Presets.cpp
    src/Generator/PhraseBank.cpp
    src/Generator/Arrangement.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
)

target_include_directories(rolla-tp-test PRIVATE src)

target_compile_definitions(rolla-tp-test
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(rolla-tp-test
    PRIVATE
        RollaData
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
    src/Generator/PhraseBank.cpp
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

# DEEP / JACKIN phrase grammar checks. No JUCE.
add_executable(rolla-phrase-test
    tests/phrase/phrase_test.cpp
    src/Generator/PhraseBank.cpp
)

target_include_directories(rolla-phrase-test PRIVATE src)

add_executable(rolla-mix-render
    src/MixRender.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Sampler/UserSlots.cpp
    src/Generator/Generator.cpp
    src/Generator/PhraseBank.cpp
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

# Bar-8 turnaround and 7+1 harmony helpers. No JUCE.
add_executable(rolla-arrange-test
    tests/arrange/arrange_test.cpp
    src/Generator/Arrangement.cpp
)

target_include_directories(rolla-arrange-test PRIVATE src)

# Export end to end: MIDI == played notes, stems, names, folder export.
add_executable(rolla-export-test
    tests/export/export_test.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/Theme.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Sampler/UserSlots.cpp
    src/Generator/Generator.cpp
    src/Generator/PhraseBank.cpp
    src/Generator/Presets.cpp
    src/Generator/MidiFile.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
)

target_include_directories(rolla-export-test PRIVATE src)

target_compile_definitions(rolla-export-test
    PRIVATE
        # Lets the test pump the message loop on macOS (runDispatchLoopUntil).
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(rolla-export-test
    PRIVATE
        RollaData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Bundled sample pack: resolve order, one bar per lane from the bundle, silence
# with no samples, settings never take a temp root. Scratch profile only.
add_executable(rolla-bundle-test
    tests/bundle/bundle_root_test.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/Theme.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Sampler/UserSlots.cpp
    src/Generator/Generator.cpp
    src/Generator/PhraseBank.cpp
    src/Generator/Presets.cpp
    src/Generator/MidiFile.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
)

target_include_directories(rolla-bundle-test PRIVATE src)

target_compile_definitions(rolla-bundle-test
    PRIVATE
        # The plugin's rules (samples only), with ROLLA_BUNDLE_ROOT standing in for the bundle.
        ROLLA_PLUGIN_BUILD=1
        ROLLA_TEST_HOOKS=1
        # Lets the test pump the message loop on macOS (runDispatchLoopUntil).
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(rolla-bundle-test
    PRIVATE
        RollaData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_executable(rolla-knob-test
    tests/knob/knob_test.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/Theme.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Sampler/UserSlots.cpp
    src/Generator/Generator.cpp
    src/Generator/PhraseBank.cpp
    src/Generator/Presets.cpp
    src/Generator/MidiFile.cpp
    src/Drums/DrumMachine.cpp
    src/Drums/DrumPattern.cpp
    src/Mix/MixProgram.cpp
    src/Mix/MixRuntime.cpp
)

target_include_directories(rolla-knob-test PRIVATE src)

target_compile_definitions(rolla-knob-test
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(rolla-knob-test
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
    src/Generator/PhraseBank.cpp
)
target_include_directories(rolla-variety PRIVATE src tools)
target_compile_options(rolla-variety PRIVATE -Wall -Wextra -Wpedantic)

# JSONL grooves for tools/score_rolls. No JUCE. Not part of CI.
add_executable(rolla-dump
    tools/dump_grooves.cpp
    src/Generator/Generator.cpp
    src/Generator/PhraseBank.cpp
)
target_include_directories(rolla-dump PRIVATE src)
target_compile_options(rolla-dump PRIVATE -Wall -Wextra -Wpedantic)

# Headless roll renderer. With --samples DIR --style ID --count N --seed S
# it writes that batch. Without those flags it still writes the local 20-roll set.
add_executable(rolla-rolls
    tools/roll_batch.cpp
    tools/PatternStats.cpp
    src/Engine/Engine.cpp
    src/Engine/Sampler.cpp
    src/Engine/SampleLibrary.cpp
    src/Sampler/SamplePrep.cpp
    src/Sampler/UserSlots.cpp
    src/Generator/Generator.cpp
    src/Generator/PhraseBank.cpp
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
