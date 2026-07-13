#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Build namstack-mod.lv2 for the MOD Dwarf with an EXTERNAL cross toolchain
# (typically a GCC newer than mod-plugin-builder's), linked against
# mod-plugin-builder's staging sysroot so the binary matches the device's
# glibc. See the "MOD Audio" section of the README for the full write-up.
#
# Usage:
#   CROSS_PREFIX=aarch64-none-linux-gnu- \
#   MOD_SYSROOT=$HOME/mod-workdir/moddwarf/staging \
#   [MOD_DEVICE=root@192.168.51.1] \
#   ./scripts/build-moddwarf-external.sh
#
# Environment:
#   CROSS_PREFIX  cross compiler prefix (default aarch64-none-linux-gnu-)
#   MOD_SYSROOT   mod-plugin-builder staging dir. May be empty for a plain
#                 compile test against the toolchain's own sysroot, but a
#                 binary built that way is NOT guaranteed to run on the
#                 device (glibc mismatch).
#   BUILD_DIR     cmake build dir (default build-moddwarf)
#   MOD_DEVICE    if set (user@host), scp the bundle to /root/.lv2 on the
#                 device after building.
# ---------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")/.."

: "${CROSS_PREFIX:=aarch64-none-linux-gnu-}"
: "${MOD_SYSROOT:=}"
: "${BUILD_DIR:=build-moddwarf}"

if ! command -v "${CROSS_PREFIX}g++" >/dev/null 2>&1; then
    echo "error: ${CROSS_PREFIX}g++ not found in PATH" >&2
    echo "hint: install an aarch64 cross toolchain and/or set CROSS_PREFIX" >&2
    exit 1
fi

echo "== compiler: $(${CROSS_PREFIX}g++ --version | head -1)"

if [ -z "${MOD_SYSROOT}" ]; then
    echo "warning: MOD_SYSROOT is empty — building against the toolchain's own sysroot." >&2
    echo "         The result may not run on the device (glibc mismatch); set" >&2
    echo "         MOD_SYSROOT=\$HOME/mod-workdir/moddwarf/staging for a deployable build." >&2
elif [ ! -d "${MOD_SYSROOT}" ]; then
    echo "error: MOD_SYSROOT '${MOD_SYSROOT}' does not exist" >&2
    exit 1
fi

cmake -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-moddwarf.cmake \
    -DCROSS_PREFIX="${CROSS_PREFIX}" \
    -DMOD_SYSROOT="${MOD_SYSROOT}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNAMSTACK_BUILD_JUCE=OFF \
    -DNAMSTACK_BUILD_MOD_LV2=ON

cmake --build "${BUILD_DIR}" --parallel "$(nproc)" --target namstack_mod

BUNDLE="${BUILD_DIR}/mod_artefacts/namstack-mod.lv2"
SO="${BUNDLE}/namstack.so"

echo
echo "== bundle: ${BUNDLE}"
file "${SO}" 2>/dev/null || true

# Audit the glibc symbol versions the binary requires: the highest one must
# not exceed the glibc shipped on the device (that of MOD_SYSROOT).
MAX_GLIBC=$("${CROSS_PREFIX}objdump" -T "${SO}" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1 || true)
echo "== highest required glibc symbol version: ${MAX_GLIBC:-none}"

# libstdc++/libgcc must NOT appear here (they are linked statically).
echo "== dynamic dependencies:"
"${CROSS_PREFIX}objdump" -p "${SO}" | awk '/NEEDED/ { print "   " $2 }'
if "${CROSS_PREFIX}objdump" -p "${SO}" | grep -q 'NEEDED.*libstdc++'; then
    echo "error: libstdc++ is dynamically linked — -static-libstdc++ did not apply" >&2
    exit 1
fi

if [ -n "${MOD_DEVICE:-}" ]; then
    echo "== deploying to ${MOD_DEVICE}:/root/.lv2/"
    scp -r "${BUNDLE}" "${MOD_DEVICE}:/root/.lv2/"
    echo "   done — restart the device (or its audio services) so mod-ui rescans the plugins."
fi

echo "== OK"
