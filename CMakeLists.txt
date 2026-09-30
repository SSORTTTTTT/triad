cmake_minimum_required(VERSION 3.22)
project(Triad VERSION 0.1.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(TRIAD_BUILD_TESTS "Build integration tests and a test effect" ON)
set(JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE 8.0.12 checkout (optional)")
if(JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850 GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(JUCE)
endif()
set(TRIAD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TRIAD_FORMATS AU)
endif()
juce_add_plugin(Triad
    COMPANY_NAME "Thomas Ross" BUNDLE_ID "audio.thomasross.triad"
    PLUGIN_MANUFACTURER_CODE Tsrt PLUGIN_CODE Trid
    PRODUCT_NAME "Triad" FORMATS ${TRIAD_FORMATS}
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx Tools
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Triad uses audio input to process your sound.")
target_sources(Triad PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/CrossoverDisplay.cpp)
target_compile_definitions(Triad PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_VST=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(Triad PRIVATE juce::juce_audio_utils juce::juce_dsp
    PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(APPLE)
    # Smaller shipped binaries: drop unreferenced code from the AU/VST3 at link time.
    target_link_options(Triad INTERFACE $<$<CONFIG:Release>:LINKER:-dead_strip>)
endif()
if(TRIAD_BUILD_TESTS)
    enable_testing()
    juce_add_plugin(TriadTestEffect COMPANY_NAME "Triad Tests" BUNDLE_ID "audio.thomasross.triad.testeffect" PLUGIN_MANUFACTURER_CODE Tsrt
        PLUGIN_CODE Ttfx PRODUCT_NAME "Triad Test Effect" FORMATS VST3
        IS_SYNTH FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(TriadTestEffect PRIVATE Tests/TestEffect.cpp)
    target_compile_definitions(TriadTestEffect PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(TriadTestEffect PRIVATE juce::juce_audio_utils)
    juce_add_console_app(TriadTests PRODUCT_NAME "Triad Tests")
    target_sources(TriadTests PRIVATE Tests/Tests.cpp)
    target_compile_definitions(TriadTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(TriadTests PRIVATE Triad juce::juce_audio_utils juce::juce_dsp)
endif()
