# CMake toolchain file: cross-compile the NamStack MOD LV2 bundle for the
# MOD Dwarf (aarch64, Cortex-A35) with an EXTERNAL cross-GCC — typically a
# GCC more recent than the one shipped by mod-plugin-builder — while linking
# against mod-plugin-builder's staging sysroot so that the binary matches the
# glibc of the device.
#
# Used by scripts/build-moddwarf-external.sh; see the "MOD Audio" section of
# the README for the full procedure.
#
# Inputs (CMake cache or environment):
#   CROSS_PREFIX  cross compiler prefix, e.g. aarch64-none-linux-gnu-
#   MOD_SYSROOT   mod-plugin-builder staging dir,
#                 e.g. $HOME/mod-workdir/moddwarf/staging
#                 (may be left empty to use the toolchain's own sysroot,
#                 e.g. for a quick compile test)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED CROSS_PREFIX OR CROSS_PREFIX STREQUAL "")
    set(CROSS_PREFIX "$ENV{CROSS_PREFIX}")
endif()
if(CROSS_PREFIX STREQUAL "")
    set(CROSS_PREFIX "aarch64-none-linux-gnu-")
endif()

set(CMAKE_C_COMPILER ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)

if(NOT DEFINED MOD_SYSROOT OR MOD_SYSROOT STREQUAL "")
    set(MOD_SYSROOT "$ENV{MOD_SYSROOT}")
endif()
if(NOT MOD_SYSROOT STREQUAL "")
    set(CMAKE_SYSROOT ${MOD_SYSROOT})
    set(CMAKE_FIND_ROOT_PATH ${MOD_SYSROOT})
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# MOD Dwarf CPU + the optimization set used by MOD plugin packages.
# (-fsingle-precision-constant is deliberately NOT used: it breaks the
# vendored WDL/Lanczos code and template deduction in the DSP sources.)
# -fsigned-char: aarch64 defaults to unsigned char, but the vendored WDL
# (AudioDSPTools resampler) requires a signed char, like on x86.
set(NAMSTACK_DWARF_OPT "-mcpu=cortex-a35 -O3 -ffast-math -fno-math-errno -fno-trapping-math -ftree-vectorize -fmove-loop-invariants -funsafe-math-optimizations -fsigned-char -fdata-sections -ffunction-sections -fexceptions -pthread -DEIGEN_DONT_PARALLELIZE=ON")

set(CMAKE_C_FLAGS_INIT "${NAMSTACK_DWARF_OPT}")
set(CMAKE_CXX_FLAGS_INIT "${NAMSTACK_DWARF_OPT}")

# Embed the (newer) libstdc++/libgcc of the external toolchain in the plugin
# instead of depending on the device's older runtime, and do not re-export
# their symbols into mod-host's process. The LV2 ABI is plain C, so carrying
# a private libstdc++ inside the .so is safe.
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,--gc-sections")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,--gc-sections")
