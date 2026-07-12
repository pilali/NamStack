cmake_minimum_required(VERSION 3.22)

project(NamStack VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(external/JUCE)

# ------------------------------------------------------------------ RTNeural
set(RTNEURAL_EIGEN ON CACHE BOOL "" FORCE)
add_subdirectory(external/RTNeural)

# ------------------------------------------------------- NeuralAmpModelerCore
set(NAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/NeuralAmpModelerCore)

file(GLOB NAM_SOURCES
    ${NAM_DIR}/NAM/*.cpp
    ${NAM_DIR}/NAM/wavenet/*.cpp)

add_library(nam_core STATIC ${NAM_SOURCES})

# NAM's Eigen (a full copy, including unsupported/) is listed here and, since
# nam_core is linked before RTNeural, ends up first on the include path of the
# plugin target too — so a single Eigen version is used for both backends.
target_include_directories(nam_core PUBLIC
    ${NAM_DIR}
    ${NAM_DIR}/Dependencies/nlohmann
    ${NAM_DIR}/Dependencies/AudioDSPTools
    ${NAM_DIR}/Dependencies/eigen)

target_compile_definitions(nam_core PUBLIC
    NAM_SAMPLE_FLOAT
    DSP_SAMPLE_FLOAT)

# --------------------------------------------------------------------- Plugin
juce_add_plugin(NamStack
    COMPANY_NAME "Pilali"
    BUNDLE_ID com.pilali.namstack
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Plal
    PLUGIN_CODE NmSk
    FORMATS LV2 VST3 Standalone
    PRODUCT_NAME "NamStack"
    LV2URI "urn:pilali:NamStack")

target_sources(NamStack PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/NeuralModel.cpp
    src/dsp/ToneStack.cpp
    src/dsp/IRStack.cpp
    src/dsp/Doubler.cpp)

target_include_directories(NamStack PRIVATE src)

target_compile_definitions(NamStack PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(NamStack
    PRIVATE
        nam_core
        RTNeural
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
