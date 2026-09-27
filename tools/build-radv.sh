#!/usr/bin/env bash
# PS5 Vulkan - build RADV from my Mesa fork at its pinned revision.
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The RADV port lives in my Mesa fork, ../PS5_Mesa (branch ps5-port: the Mesa
# 26.2.0 release with a PS5 winsys, -Dradv-winsys=ps5). The pinned revision is
# exported with git archive, so a build never depends on the fork's working
# tree, and built with meson for the console (tooling/radv/ps5-cross.ini):
#
#   .deps/native/radv/lib/libvulkan_radeon.ps5.a   RADV, ACO, NIR and Mesa's
#                                                  Vulkan runtime in one archive
#   .deps/native/radv/include/vulkan/              the headers it was built with
#   .deps/native/radv/PROVENANCE.txt
#
# Titles link the archive with tools/radv-link.sh. While a change to the fork
# is being worked on, RADV_ARCHIVE can name the fork's own build instead
# (../PS5_Mesa/build-ps5/src/amd/vulkan/libvulkan_radeon.a).

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mesa_fork="${PS5_MESA_FORK:-$root/../PS5_Mesa}"
mesa_revision=777221d2645e2f8684697b249252822d79ea5092
sdk="$root/.deps/native/ps5-payload-sdk"
source_tree="$root/.deps/work/radv-src"
build="$root/.deps/work/radv-build-ps5"
install="$root/.deps/native/radv"
meson=${MESON:-$(command -v meson || echo "$HOME/.local/bin/meson")}
ninja=${NINJA:-$(command -v ninja || echo "$HOME/.local/bin/ninja")}

[[ -x $meson && -x $ninja ]] || { echo "meson and ninja are needed (uv tool install meson ninja)" >&2; exit 2; }
[[ -f $sdk/.ps5-sdk-revision ]] || { echo "run tools/setup-native-dependencies.sh first" >&2; exit 2; }
if [[ -f $install/PROVENANCE.txt ]] && grep -q "^revision: $mesa_revision$" "$install/PROVENANCE.txt" &&
    grep -q "^sdk: $(cat "$sdk/.ps5-sdk-revision")$" "$install/PROVENANCE.txt"; then
    echo "==> [radv] $install is RADV at ${mesa_revision:0:12}"
    exit 0
fi
git -C "$mesa_fork" cat-file -e "$mesa_revision^{commit}" 2>/dev/null ||
    { echo "the Mesa fork at $mesa_fork does not have $mesa_revision" >&2; exit 2; }

if [[ ! -f $source_tree/.revision || $(<"$source_tree/.revision") != "$mesa_revision" ]]; then
    rm -rf "$source_tree" "$build"
    mkdir -p "$source_tree"
    git -C "$mesa_fork" archive "$mesa_revision" | tar -x -C "$source_tree"
    printf '%s\n' "$mesa_revision" > "$source_tree/.revision"
fi

constants="$root/.deps/work/radv-cross-constants.ini"
printf "[constants]\nsdk = '%s'\n" "$sdk" > "$constants"
if [[ ! -f $build/build.ninja ]]; then
    "$meson" setup "$build" "$source_tree" \
        --cross-file "$constants" --cross-file "$root/tooling/radv/ps5-cross.ini" \
        -Dvulkan-drivers=amd -Dgallium-drivers= -Dplatforms= -Dradv-winsys=ps5 \
        -Dllvm=disabled -Damd-use-llvm=false -Dvideo-codecs= \
        -Dbuildtype=debugoptimized -Db_ndebug=false \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
        -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled -Dzlib=disabled -Dexpat=disabled \
        -Dxmlconfig=disabled -Dshader-cache=disabled -Dbuild-tests=false -Dvulkan-layers= -Dtools= \
        -Dradv-build-id="$mesa_revision" > "$build.setup.log" 2>&1 ||
        { tail -20 "$build.setup.log" >&2; exit 1; }
fi
"$ninja" -C "$build" src/amd/vulkan/libvulkan_radeon.a > "$build.log" 2>&1 ||
    { grep -E "error|FAILED" "$build.log" | head -20 >&2; exit 1; }

rm -rf "$install"
mkdir -p "$install/lib" "$install/include"
cp "$build/src/amd/vulkan/libvulkan_radeon.a" "$install/lib/libvulkan_radeon.ps5.a"
cp -r "$source_tree/include/vulkan" "$install/include/vulkan"
cp -r "$source_tree/include/vk_video" "$install/include/vk_video" 2>/dev/null || true
cat > "$install/PROVENANCE.txt" <<PROV
RADV for the PlayStation 5, built by tools/build-radv.sh
fork: $mesa_fork
revision: $mesa_revision
sdk: $(cat "$sdk/.ps5-sdk-revision")
archive sha256: $(sha256sum "$install/lib/libvulkan_radeon.ps5.a" | cut -d' ' -f1)
PROV
echo "==> [radv] built RADV at ${mesa_revision:0:12} into $install"
