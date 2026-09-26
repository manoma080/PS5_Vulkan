#!/usr/bin/env bash
# PS5 Vulkan - link recipe for titles that link RADV.
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by tools/build-radv-title.sh (and, later, by every title that links
# the RADV archive), so each links it the same way:
# - the archive my Mesa fork builds with -Dradv-winsys=ps5 (RADV, its compiler
#   and Mesa's runtime in one), linked whole;
# - the payload SDK's libc++, libc++abi and libunwind for ACO's C++, and
#   Clang's builtins for __emutls_get_address, as tools/psbc-link.sh has them;
# - the SDK's platform layer, whose ps5_ functions stand in for the libc
#   functions no system module exports, bound to libc's names here: a title
#   that defined libc's names itself would export them, which the title
#   converter refuses (platform/include/ps5platform/libc.h in the SDK fork).
#
# radv_link_recipe ROOT SDK_ROOT ARCHIVE sets radv_linker_script,
# radv_link_inputs and radv_link_flags. It returns 2 when an input is missing.

radv_link_recipe() {
    local root=$1 sdk_root=$2 archive=$3
    local compiler=${PS5_CLANG:-$(command -v clang || command -v clang-18 || true)}
    [[ -n $compiler ]] || { echo "clang was not found; set PS5_CLANG" >&2; return 2; }
    local builtins
    builtins="$("$compiler" --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a"
    local platform="$sdk_root/target/lib/libps5platform.a"

    radv_linker_script=(-T "$root/tooling/psbc/ps5-pie-unwind.ld" -L "$root/tooling/native")
    # Whole: Mesa's dispatch tables name every entry point through a weak
    # reference, and a weak reference pulls no archive member in, so an entry
    # point whose object nothing else needs would be NULL (the first console
    # run of PPSA99014 called one inside wsi_device_init).
    radv_link_inputs=(-L "$sdk_root/target/lib" --whole-archive "$archive" --no-whole-archive
        --start-group "$sdk_root/target/lib/libc++.a" "$sdk_root/target/lib/libc++abi.a"
        "$sdk_root/target/lib/libunwind.a" "$builtins" "$platform" --end-group)
    radv_link_flags=()
    local name
    for name in qsort_r mkstemps openlog popen pclose open_memstream __xuname __assert \
            __memset_chk regcomp regexec regfree regerror localtime_r newlocale freelocale \
            strtod_l strtof_l dladdr; do
        radv_link_flags+=("--defsym=$name=ps5_$name")
    done

    local file
    for file in "$archive" "$platform" "$sdk_root/target/lib/libc++.a" \
            "$sdk_root/target/lib/libc++abi.a" "$sdk_root/target/lib/libunwind.a" "$builtins"; do
        [[ -f $file ]] || { echo "missing $file" >&2; return 2; }
    done
}
