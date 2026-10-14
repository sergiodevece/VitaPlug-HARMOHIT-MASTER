cmake_minimum_required(VERSION 3.22)
project(VitaPlugHarmoHitMasterR1 VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vitaplug_gainer_80/external/JUCE" CACHE PATH "Pinned JUCE 9.0.3")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set JUCE_DIR to pinned JUCE 9.0.3 sources.")
endif()
add_subdirectory("${JUCE_DIR}" JUCE)
juce_add_binary_data(HarmoHitMasterAssets SOURCES Resources/HARMOHIT/HarmoHitMasterApproved.png)
if(WIN32)
    set(MASTER_FORMATS VST3)
else()
    set(MASTER_FORMATS AU VST3)
endif()
juce_add_plugin(VitaPlugHarmoHitMasterR1
    COMPANY_NAME "VITAPLUG"
    BUNDLE_ID "com.vitaplug.harmohit.master.r1"
    PLUGIN_MANUFACTURER_CODE Vtpl PLUGIN_CODE HmR1
    FORMATS ${MASTER_FORMATS}
    PRODUCT_NAME "VitaPlug HARMOHIT MASTER"
    PLUGIN_NAME "VitaPlug HARMOHIT MASTER"
    DESCRIPTION "Mastering coloration with independent Mid/Side banks"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE)
set(MASTER_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp)
function(master_common target)
    target_sources(${target} PRIVATE ${MASTER_SOURCES})
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${target} PRIVATE HarmoHitMasterAssets
        juce::juce_audio_processors juce::juce_dsp juce::juce_gui_basics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endfunction()
master_common(VitaPlugHarmoHitMasterR1)
enable_testing()
add_executable(HarmoHitMasterTests Tests/MasterTests.cpp)
master_common(HarmoHitMasterTests)
add_test(NAME HarmoHitMasterTests COMMAND HarmoHitMasterTests)
add_executable(HarmoHitDriveTests Tests/DriveTests.cpp)
master_common(HarmoHitDriveTests)
add_test(NAME HarmoHitDriveTests COMMAND HarmoHitDriveTests)
juce_add_gui_app(HarmoHitMasterPreview PRODUCT_NAME "HARMOHIT MASTER Preview")
target_sources(HarmoHitMasterPreview PRIVATE Tests/MasterPreview.cpp)
master_common(HarmoHitMasterPreview)
target_compile_definitions(HarmoHitMasterPreview PRIVATE HARMOHIT_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(HarmoHitCoexistenceTests Tests/Coexistence.cpp)
master_common(HarmoHitCoexistenceTests)
target_compile_definitions(HarmoHitCoexistenceTests PRIVATE JUCE_PLUGINHOST_VST3=1)

