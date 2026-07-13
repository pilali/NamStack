cmake_minimum_required(VERSION 3.22)

project(NamStack VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The JUCE formats (VST3 / LV2 with GUI / Standalone) need X11 and are meant
# for desktop use. The plain LV2 (MOD) variant is JUCE-free and is the one to
# build with mod-plugin-builder for MOD devices (Dwarf, Duo, DuoX).
option(NAMSTACK_BUILD_JUCE "Build the JUCE plugin formats (VST3, LV2, Standalone)" ON)
option(NAMSTACK_BUILD_MOD_LV2 "Build the plain LV2 plugin for MOD devices" ON)

# Cap on the (resampled) IR length of the MOD variant, to bound CPU use on
# embedded targets. 8192 samples = 170 ms at 48 kHz.
set(NAMSTACK_MAX_IR_SAMPLES 8192 CACHE STRING "Maximum IR length in samples (MOD variant)")

# Specialized fast path for A2-architecture NAM models (A2 standard and A2
# nano — WaveNet-shaped files detected by is_a2_shape()). Without it A2
# models still load, but through the slower generic WaveNet implementation.
option(NAMSTACK_ENABLE_A2_FAST "Enable the NAM A2 fast-path WaveNet implementation" ON)

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
# plugin targets too — so a single Eigen version is used for both backends.
target_include_directories(nam_core PUBLIC
    ${NAM_DIR}
    ${NAM_DIR}/Dependencies/nlohmann
    ${NAM_DIR}/Dependencies/AudioDSPTools
    ${NAM_DIR}/Dependencies/eigen)

target_compile_definitions(nam_core PUBLIC
    NAM_SAMPLE_FLOAT
    DSP_SAMPLE_FLOAT)

if(NAMSTACK_ENABLE_A2_FAST)
    target_compile_definitions(nam_core PUBLIC NAM_ENABLE_A2_FAST)
endif()

# ------------------------------------------------------------ shared DSP core
# JUCE-free DSP shared by the JUCE plugin and the MOD LV2 plugin.
add_library(namstack_core STATIC
    src/dsp/NeuralModel.cpp
    src/dsp/NamArchitectures.cpp
    src/dsp/ToneStack.cpp
    src/dsp/Doubler.cpp)

target_include_directories(namstack_core PUBLIC src)
target_link_libraries(namstack_core PUBLIC nam_core RTNeural)

# --------------------------------------------------------------- JUCE plugin
if(NAMSTACK_BUILD_JUCE)
    add_subdirectory(external/JUCE)

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
        src/dsp/IRStack.cpp)

    target_include_directories(NamStack PRIVATE src)

    target_compile_definitions(NamStack PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(NamStack
        PRIVATE
            namstack_core
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags)

    # -------------------------------------------------------------- modgui
    # Install the MOD web GUI into the JUCE LV2 bundle. JUCE regenerates
    # manifest.ttl on every build (juce_lv2_helper runs as an earlier
    # POST_BUILD step), so the modgui reference is (re-)appended here.
    set(MODGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/modgui)
    add_custom_command(TARGET NamStack_LV2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${MODGUI_DIR}/resources $<TARGET_FILE_DIR:NamStack_LV2>/modgui
        COMMAND ${CMAKE_COMMAND} -E copy
            ${MODGUI_DIR}/modgui.ttl $<TARGET_FILE_DIR:NamStack_LV2>/modgui.ttl
        COMMAND bash -c "grep -q 'modgui.ttl' '$<TARGET_FILE_DIR:NamStack_LV2>/manifest.ttl' || cat '${MODGUI_DIR}/manifest-modgui.ttl' >> '$<TARGET_FILE_DIR:NamStack_LV2>/manifest.ttl'"
        COMMENT "Adding modgui to the JUCE LV2 bundle"
        VERBATIM)
endif()

# ------------------------------------------------------- MOD LV2 (JUCE-free)
if(NAMSTACK_BUILD_MOD_LV2)
    add_library(pffft STATIC
        external/vendored/pffft/pffft.c
        external/vendored/pffft/pffft_common.c)
    target_include_directories(pffft PUBLIC external/vendored/pffft)

    add_library(namstack_mod MODULE
        mod/NamStackMod.cpp
        src/core/Convolver.cpp
        src/core/IRLoader.cpp
        src/core/IRMixer.cpp)

    set_target_properties(namstack_mod PROPERTIES
        PREFIX ""
        OUTPUT_NAME "namstack"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/mod_artefacts/namstack-mod.lv2)

    target_include_directories(namstack_mod PRIVATE
        src
        external/lv2/include
        external/vendored/dr_libs)

    target_compile_definitions(namstack_mod PRIVATE
        NAMSTACK_MAX_IR_SAMPLES=${NAMSTACK_MAX_IR_SAMPLES})

    target_link_libraries(namstack_mod PRIVATE namstack_core pffft)

    # assemble the LV2 bundle next to the binary
    set(MOD_BUNDLE_DIR ${CMAKE_BINARY_DIR}/mod_artefacts/namstack-mod.lv2)
    add_custom_command(TARGET namstack_mod POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/mod/lv2/manifest.ttl
            ${CMAKE_CURRENT_SOURCE_DIR}/mod/lv2/namstack.ttl
            ${CMAKE_CURRENT_SOURCE_DIR}/mod/lv2/modgui.ttl
            ${MOD_BUNDLE_DIR}/
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MOD_BUNDLE_DIR}/modgui
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/knob.png
            ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/switch.png
            ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/footswitch.png
            ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/thumbnail-namstack.png
            ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/screenshot-namstack-mod.png
            ${CMAKE_CURRENT_SOURCE_DIR}/mod/modgui/icon-namstack-mod.html
            ${CMAKE_CURRENT_SOURCE_DIR}/mod/modgui/stylesheet-namstack-mod.css
            ${MOD_BUNDLE_DIR}/modgui/
        COMMENT "Assembling namstack-mod.lv2 bundle"
        VERBATIM)

    install(DIRECTORY ${MOD_BUNDLE_DIR}
        DESTINATION lib/lv2
        USE_SOURCE_PERMISSIONS)
endif()
