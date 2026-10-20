cmake_minimum_required(VERSION 3.22)
project(Triad VERSION 0.8.0 LANGUAGES C CXX)
# Installed as "Triad v7.vst3" / "Triad v7.component"; the plug-in IDs are unchanged from Triad 0.1,
# so projects made with the earlier "Triad", "Triad v2" ... "Triad v6" open with Triad v7.
# Each new release gets the next name (v7, v8, ...) and replaces the earlier ones when installed.
set(TRIAD_PRODUCT_NAME "Triad v7")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(TRIAD_BUILD_TESTS "Build integration tests and a test effect" ON)
option(TRIAD_BUILD_STANDALONE "Also build a standalone app (development only, not shipped)" OFF)
option(TRIAD_SIZE_OPTIMISED "Optimise framework/UI code for size (audio path stays -O3) and use LTO" ON)
set(JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE 8.0.12 checkout (optional)")
if(JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850 GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(JUCE)
endif()
set(TRIAD_FORMATS VST3)
if(APPLE)
    list(APPEND TRIAD_FORMATS AU)
endif()
if(TRIAD_BUILD_STANDALONE)
    list(APPEND TRIAD_FORMATS Standalone)
endif()
juce_add_plugin(Triad
    COMPANY_NAME "Thomas Ross" BUNDLE_ID "audio.thomasross.triad"
    PLUGIN_MANUFACTURER_CODE Tsrt PLUGIN_CODE Trid
    PRODUCT_NAME "${TRIAD_PRODUCT_NAME}" FORMATS ${TRIAD_FORMATS}
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx Tools
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Triad uses audio input to process your sound.")
target_sources(Triad PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/CrossoverDisplay.cpp
    Source/BandStrip.cpp Source/PluginCatalog.cpp Source/AudioUnitList.cpp Source/Modulation.cpp Source/Settings.cpp
    Source/Widgets.cpp Source/Look.cpp Source/ShapePanel.cpp Source/LfoPage.cpp Source/MatrixPage.cpp Source/PlatformWindows.cpp
    Source/Generator.cpp Source/ShapeLibrary.cpp Source/TriadTime.cpp)
if(APPLE)
    target_sources(Triad PRIVATE Source/PlatformWindowsMac.mm)
endif()
target_compile_definitions(Triad PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=$<IF:$<PLATFORM_ID:Darwin>,1,0> JUCE_PLUGINHOST_VST=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    TRIAD_PRODUCT_NAME="${TRIAD_PRODUCT_NAME}" TRIAD_VERSION="${PROJECT_VERSION}")
# Only the JUCE modules the plugin uses: plugin hosting + UI. No juce_audio_utils / juce_dsp
# (they add audio-device I/O and the FLAC/Ogg/WAV/AIFF codecs, none of which Triad needs).
target_link_libraries(Triad PRIVATE juce::juce_audio_processors PUBLIC juce::juce_recommended_warning_flags)
if(TRIAD_BUILD_STANDALONE)
    target_link_libraries(Triad PRIVATE juce::juce_audio_utils)
endif()
if(APPLE)
    # AudioToolbox lists Audio Units (effects vs instruments) without loading them;
    # CoreAudioKit provides the generic editor view used when hosting Audio Units.
    target_link_libraries(Triad PUBLIC "-framework AudioToolbox" "-framework CoreAudioKit")
endif()
# "Triad Time": Triad's time and volume gate (built into Triad) as a plug-in of its own, for any track in any DAW.
set(TRIAD_TIME_FORMATS VST3)
if(APPLE)
    list(APPEND TRIAD_TIME_FORMATS AU)
endif()
juce_add_plugin(TriadTime
    COMPANY_NAME "Thomas Ross" BUNDLE_ID "audio.thomasross.triadtime"
    PLUGIN_MANUFACTURER_CODE Tsrt PLUGIN_CODE TrTm
    PRODUCT_NAME "Triad Time" FORMATS ${TRIAD_TIME_FORMATS}
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx)
target_sources(TriadTime PRIVATE Source/TriadTime.cpp Source/ShapePanel.cpp Source/Look.cpp Source/Modulation.cpp
    Source/Generator.cpp Source/ShapeLibrary.cpp Source/Settings.cpp)
target_compile_definitions(TriadTime PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_VST=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 TRIAD_TIME_PLUGIN=1
    TRIAD_VERSION="${PROJECT_VERSION}")
target_link_libraries(TriadTime PRIVATE juce::juce_audio_processors PUBLIC juce::juce_recommended_warning_flags)
# "Triad Scanner": a small helper that opens a plug-in in its own process when Triad cannot tell
# from disk whether it is an effect, so a plug-in that crashes while being checked cannot take
# the DAW down. It is copied into every plug-in bundle as Contents/Helpers/Triad Scanner.
juce_add_console_app(TriadScanner PRODUCT_NAME "Triad Scanner")
target_sources(TriadScanner PRIVATE Source/Scanner.cpp)
target_compile_definitions(TriadScanner PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_LADSPA=0 JUCE_PLUGINHOST_LV2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(TriadScanner PRIVATE juce::juce_audio_processors_headless PUBLIC juce::juce_recommended_warning_flags)
foreach(target IN ITEMS Triad TriadTime TriadScanner)
    if(TRIAD_SIZE_OPTIMISED AND NOT MSVC)
        # Framework and UI code optimised for size; the audio path (PluginProcessor.cpp,
        # which inlines the crossover and delay lines) keeps full -O3 speed.
        target_compile_options(${target} PUBLIC $<$<CONFIG:Debug>:-g -O0> $<$<CONFIG:Release>:-Os>)
        target_link_libraries(${target} PUBLIC juce::juce_recommended_lto_flags)
    else()
        target_link_libraries(${target} PUBLIC juce::juce_recommended_config_flags)
    endif()
endforeach()
if(TRIAD_SIZE_OPTIMISED AND NOT MSVC)
    set_source_files_properties(Source/PluginProcessor.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:-O3>")
endif()
if(APPLE)
    # Drop unreferenced code at link time.
    target_link_options(Triad INTERFACE $<$<CONFIG:Release>:LINKER:-dead_strip>)
    target_link_options(TriadTime INTERFACE $<$<CONFIG:Release>:LINKER:-dead_strip>)
    target_link_options(TriadScanner PRIVATE $<$<CONFIG:Release>:LINKER:-dead_strip>)
elseif(NOT MSVC)
    foreach(target IN ITEMS Triad TriadTime TriadScanner)
        target_compile_options(${target} PUBLIC $<$<CONFIG:Release>:-ffunction-sections -fdata-sections>)
    endforeach()
    target_link_options(Triad INTERFACE $<$<CONFIG:Release>:LINKER:--gc-sections>)
    target_link_options(TriadTime INTERFACE $<$<CONFIG:Release>:LINKER:--gc-sections>)
    target_link_options(TriadScanner PRIVATE $<$<CONFIG:Release>:LINKER:--gc-sections>)
endif()
foreach(format IN ITEMS VST3 AU)
    if(TARGET Triad_${format})
        add_dependencies(Triad_${format} TriadScanner)
        # <bundle>/Contents/MacOS (or x86_64-linux) -> <bundle>/Contents/Helpers
        add_custom_command(TARGET Triad_${format} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Triad_${format}>/../Helpers"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:TriadScanner>" "$<TARGET_FILE_DIR:Triad_${format}>/../Helpers/Triad Scanner"
            VERBATIM)
    endif()
endforeach()
if(TRIAD_BUILD_TESTS)
    enable_testing()
    juce_add_plugin(TriadTestEffect COMPANY_NAME "Triad Tests" BUNDLE_ID "audio.thomasross.triad.testeffect" PLUGIN_MANUFACTURER_CODE Tsrt
        PLUGIN_CODE Ttfx PRODUCT_NAME "Triad Test Effect" FORMATS VST3
        IS_SYNTH FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(TriadTestEffect PRIVATE Tests/TestEffect.cpp)
    target_compile_definitions(TriadTestEffect PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(TriadTestEffect PRIVATE juce::juce_audio_utils)
    # An instrument fixture: the effect list must never offer it.
    juce_add_plugin(TriadTestSynth COMPANY_NAME "Triad Tests" BUNDLE_ID "audio.thomasross.triad.testsynth" PLUGIN_MANUFACTURER_CODE Tsrt
        PLUGIN_CODE Ttsy PRODUCT_NAME "Triad Test Synth" FORMATS VST3
        IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(TriadTestSynth PRIVATE Tests/TestSynth.cpp)
    target_compile_definitions(TriadTestSynth PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(TriadTestSynth PRIVATE juce::juce_audio_utils)
    # A "plug-in" that crashes (or hangs) as soon as it is loaded: Triad must never load it itself.
    add_library(TriadCrashFixture MODULE Tests/CrashFixture.cpp)
    set_target_properties(TriadCrashFixture PROPERTIES PREFIX "" OUTPUT_NAME "Triad Crash Fixture")
    if(APPLE)
        set_target_properties(TriadCrashFixture PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION vst3
            LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/fixtures>")
    else()
        set_target_properties(TriadCrashFixture PROPERTIES SUFFIX ".so"
            LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/fixtures/Triad Crash Fixture.vst3/Contents/${CMAKE_SYSTEM_PROCESSOR}-linux>")
    endif()
    juce_add_console_app(TriadTests PRODUCT_NAME "Triad Tests")
    target_sources(TriadTests PRIVATE Tests/Tests.cpp)
    target_compile_definitions(TriadTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(TriadTests PRIVATE Triad juce::juce_audio_utils juce::juce_dsp)
    add_dependencies(TriadTests TriadScanner TriadCrashFixture)
    # Development tool: the preset shapes and the generator's output as JSON (to look at them).
    juce_add_console_app(TriadShapeDump PRODUCT_NAME "Triad Shape Dump")
    target_sources(TriadShapeDump PRIVATE Tests/ShapeDump.cpp Source/Modulation.cpp Source/Generator.cpp Source/ShapeLibrary.cpp)
    target_compile_definitions(TriadShapeDump PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(TriadShapeDump PRIVATE juce::juce_core)
    # Stress test: hundreds of hosted effects, random edits while audio runs, many instances.
    juce_add_console_app(TriadStress PRODUCT_NAME "Triad Stress")
    target_sources(TriadStress PRIVATE Tests/Stress.cpp)
    if(APPLE)
        target_sources(TriadStress PRIVATE Tests/StressMac.mm)
        target_link_libraries(TriadStress PRIVATE "-framework OpenGL" "-framework AppKit" "-framework QuartzCore")
    endif()
    target_compile_definitions(TriadStress PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(TriadStress PRIVATE Triad juce::juce_audio_utils)
    add_dependencies(TriadStress TriadScanner TriadCrashFixture)
endif()
