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

# On ARM (Raspberry Pi, MOD devices...) plain char is unsigned by default,
# which the WDL code vendored by NeuralAmpModelerCore rejects with a static
# assertion. Build everything with signed char, like on x86.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)" AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    add_compile_options(-fsigned-char)
endif()

# The WDL vendored by NAM core's AudioDSPTools (wdltypes.h, heapbuf.h) includes
# windows.h, which defines min and max as macros unless told not to. They then
# eat every std::min / std::max downstream -- std::max( becomes std::( -- and the
# resampler will not compile:
#   error C2589: '(': illegal token on right side of '::'
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

# An LV2 plugin is dlopen'd into a host that may already have loaded another
# plugin built from the same libraries -- neural-amp-modeler-lv2, for instance,
# also vendors NeuralAmpModelerCore. With default visibility both .so files
# export the whole nam:: namespace and the dynamic linker interposes them:
#   * nam::ConfigParserRegistry's singleton is emitted as an STB_GNU_UNIQUE
#     symbol, which glibc unifies across DSOs even under RTLD_LOCAL. The second
#     plugin to load then re-registers its parsers into an already-populated
#     registry and aborts the host:
#       "Config parser already registered for: SlimmableContainer"
#   * even without that, nam::create_dsp and friends would bind to whichever
#     copy loaded first, silently mixing two NAM core versions.
# Hide everything by default (the LV2 entry point carries an explicit
# LV2_SYMBOL_EXPORT) and stop GCC from emitting unique symbols at all. Must be
# set before the targets below are defined, RTNeural included.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fno-gnu-unique)
endif()

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
    src/dsp/GraphicEQ.cpp
    src/dsp/Spread.cpp)

target_include_directories(namstack_core PUBLIC src)
target_link_libraries(namstack_core PUBLIC nam_core RTNeural)

# AudioDSPTools' ResamplingContainer.h (vendored by NeuralAmpModelerCore) has a
# member alias `using LanczosResampler = LanczosResampler<...>` that shadows the
# class of the same name. GCC 11+ makes this a hard error governed by
# -fpermissive; the in-source `#pragma GCC diagnostic ignored "-Wchanges-meaning"`
# only silences it on GCC 14+ (where that flag exists). Downgrade it to a warning
# for the single translation unit that includes the header, on GCC only.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(src/dsp/NeuralModel.cpp
        PROPERTIES COMPILE_OPTIONS -fpermissive)
endif()

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

    # Belt and braces alongside the hidden visibility set above: symbols pulled
    # in from the static archives must not be re-exported by the module either
    # (--exclude-libs), and whatever still slips through is localised by the
    # version script. That last layer is load-bearing: libstdc++'s headers pin
    # their template instantiations to default visibility, overriding
    # -fvisibility=hidden, and which ones a given GCC emits out of line varies
    # by version -- GCC 11 exports four of them from this module, GCC 13 none.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(namstack_mod PRIVATE
            -Wl,--exclude-libs,ALL
            -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/mod/namstack.map)
        set_property(TARGET namstack_mod APPEND PROPERTY
            LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mod/namstack.map)
    endif()

    # Assemble the LV2 bundle next to the binary. This is driven by a stamp file
    # that DEPENDS on the data files rather than by a POST_BUILD command on
    # namstack_mod: POST_BUILD only fires when the .so itself is relinked, so
    # editing a .ttl or a modgui asset would otherwise leave a stale bundle.
    set(MOD_BUNDLE_DIR ${CMAKE_BINARY_DIR}/mod_artefacts/namstack-mod.lv2)

    set(MOD_TTL_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/mod/lv2/manifest.ttl
        ${CMAKE_CURRENT_SOURCE_DIR}/mod/lv2/namstack.ttl
        ${CMAKE_CURRENT_SOURCE_DIR}/mod/lv2/modgui.ttl)

    set(MOD_GUI_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/knob.png
        ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/switch.png
        ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/footswitch.png
        ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/fader.png
        ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/thumbnail-namstack.png
        ${CMAKE_CURRENT_SOURCE_DIR}/modgui/resources/screenshot-namstack-mod.png
        ${CMAKE_CURRENT_SOURCE_DIR}/mod/modgui/icon-namstack-mod.html
        ${CMAKE_CURRENT_SOURCE_DIR}/mod/modgui/stylesheet-namstack-mod.css)

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/namstack-mod-bundle.stamp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MOD_BUNDLE_DIR}/modgui
        COMMAND ${CMAKE_COMMAND} -E copy ${MOD_TTL_FILES} ${MOD_BUNDLE_DIR}/
        COMMAND ${CMAKE_COMMAND} -E copy ${MOD_GUI_FILES} ${MOD_BUNDLE_DIR}/modgui/
        COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/namstack-mod-bundle.stamp
        DEPENDS ${MOD_TTL_FILES} ${MOD_GUI_FILES}
        COMMENT "Assembling namstack-mod.lv2 bundle"
        VERBATIM)

    add_custom_target(namstack_mod_bundle ALL
        DEPENDS ${CMAKE_BINARY_DIR}/namstack-mod-bundle.stamp)
    add_dependencies(namstack_mod_bundle namstack_mod)

    install(DIRECTORY ${MOD_BUNDLE_DIR}
        DESTINATION lib/lv2
        USE_SOURCE_PERMISSIONS)
endif()
