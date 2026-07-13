######################################
#
# namstack
#
# mod-plugin-builder recipe for the NamStack plain LV2 plugin
# (urn:pilali:NamStackMOD). Copy this directory to
# <mod-plugin-builder>/plugins/package/namstack and build with e.g.:
#
#   ./build moddwarf namstack
#
######################################

# Pin a commit (or tag) of https://github.com/pilali/NamStack here for
# reproducible builds. A branch name also works for development builds.
NAMSTACK_VERSION = claude/lv2-juce-amp-modeler-buvury
NAMSTACK_SITE = https://github.com/pilali/NamStack.git
NAMSTACK_SITE_METHOD = git
NAMSTACK_GIT_SUBMODULES = y
NAMSTACK_BUNDLES = namstack-mod.lv2

# needed for submodules support
NAMSTACK_PRE_DOWNLOAD_HOOKS += MOD_PLUGIN_BUILDER_DOWNLOAD_WITH_SUBMODULES

# ------------------------------------------------------- optimization flags
# Curated for the low-power ARM cores of MOD devices (Dwarf: Cortex-A35);
# same set that mod-audio uses for neural-amp-modeler-lv2. The base
# -mcpu/-mtune flags come from the buildroot toolchain via
# BR2_TARGET_OPTIMIZATION.
NAMSTACK_FILTERED_FLAGS = -funroll-loops
NAMSTACK_TARGET_OPT = $(filter-out $(NAMSTACK_FILTERED_FLAGS),$(subst ",,$(BR2_TARGET_OPTIMIZATION)))
NAMSTACK_TARGET_OPT += -fno-unroll-loops
NAMSTACK_TARGET_OPT += -ftree-vectorize -fmove-loop-invariants -fexceptions -funsafe-math-optimizations
NAMSTACK_TARGET_OPT += -fdata-sections -ffunction-sections -pipe -fno-math-errno -fno-trapping-math
NAMSTACK_TARGET_OPT += -falign-functions=16 -falign-loops=16
# aarch64 defaults to unsigned char, but the vendored WDL (AudioDSPTools
# resampler) requires a signed char, like on x86.
NAMSTACK_TARGET_OPT += -fsigned-char
# NOTE: -fsingle-precision-constant (used by some MOD packages) is NOT
# compatible with this codebase (breaks the vendored WDL/Lanczos resampler
# and std::clamp/std::max template deduction) — do not add it.
NAMSTACK_TARGET_OPT += -pthread

ifndef BR2_SKIP_LTO
NAMSTACK_TARGET_OPT += -flto -ffat-lto-objects
endif

# Eigen (used by both NAM core and RTNeural) must not spawn threads on device
NAMSTACK_TARGET_OPT += -DEIGEN_DONT_PARALLELIZE=ON

# ------------------------------------------------------------- cmake options
NAMSTACK_CONF_OPTS += -DCMAKE_C_FLAGS="$(TARGET_CFLAGS) $(NAMSTACK_TARGET_OPT)"
NAMSTACK_CONF_OPTS += -DCMAKE_CXX_FLAGS="$(TARGET_CXXFLAGS) $(NAMSTACK_TARGET_OPT)"
NAMSTACK_CONF_OPTS += -DCMAKE_SHARED_LINKER_FLAGS="$(TARGET_LDFLAGS) $(NAMSTACK_TARGET_OPT)"
NAMSTACK_CONF_OPTS += -DCMAKE_BUILD_TYPE=Release

# only the JUCE-free plain LV2 plugin makes sense on device (no X11 there)
NAMSTACK_CONF_OPTS += -DNAMSTACK_BUILD_JUCE=OFF
NAMSTACK_CONF_OPTS += -DNAMSTACK_BUILD_MOD_LV2=ON

# Cap on the resampled IR length, in samples. 8192 = 170 ms @ 48 kHz; lower
# it to 4096 to save CPU for heavier NAM models on the Dwarf.
NAMSTACK_CONF_OPTS += -DNAMSTACK_MAX_IR_SAMPLES=8192

define NAMSTACK_INSTALL_TARGET_CMDS
	install -d $(TARGET_DIR)/usr/lib/lv2
	cp -r $(@D)/mod_artefacts/$(NAMSTACK_BUNDLES) $(TARGET_DIR)/usr/lib/lv2/
endef

$(eval $(cmake-package))
