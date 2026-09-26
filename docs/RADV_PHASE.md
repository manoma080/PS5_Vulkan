# RADV on the console: run log

Append-only, like the M5 phase logs: dated entries, never rewritten. The plan
is [VULKAN_1_4_PLAN.md](VULKAN_1_4_PLAN.md) (route B); the CTS set-up is in
[CTS.md](CTS.md). The driver is my Mesa fork `PS5_Mesa` (branch `ps5-port`,
RADV with a PS5 winsys), the CTS my fork `PS5_VK-GL-CTS` (branch `ps5-port`),
and the platform pieces are in the payload SDK fork's `platform/`.

## 2026-09-26 — the CTS runs on the console

RADV passes the smoke title (PPSA99014, 9 of 9: device creation, fill, copy,
compute and a triangle read back exactly). The CTS runs as PPSA99015
(`tools/build-cts-title.sh`, `tools/run-cts.py`), first against 1.4.5.3 and
then against 1.4.6.2. What the first runs found, in order:

1. **An 11.5 s case was 17 ms of work.** The image format query cases each
   took exactly 11.51 s, which read as a hang. Each logs 322 messages, and
   `--deqp-log-flush=enable` flushes the log after every XML element; a
   `write()` to the console's storage costs about 3.3 ms whatever its size
   (the SDK platform's PROBE.md). Without the flush the same cases take 17 and
   6.5 ms, and `dEQP-VK.api.info.*` (8,175 cases) runs in 56 s.
2. **libc's heap ran out in the first shader build.** `api.smoke.create_shader`
   was a ResourceError: glslang's 8 KiB pool page (`operator new[]`) was
   refused. The platform layer now gives a title a heap in direct memory
   (`ps5platform/heap.h`: dlmalloc over one reserved 16 GiB range, reached
   through `--wrap` of the malloc family); `api.smoke.*` passes 6 of 6.
3. **What RADV reported that the console does not have.** With no window
   system, `VK_EXT_headless_surface` came without `VK_KHR_surface`; the fd,
   sync-fd and dma-buf external handles and host-pointer import were exposed
   with nothing behind them; memory reports carried object id 0. RADV now
   takes these from the winsys (`has_external_fd`, `has_userptr`, a unique
   `obj_id`). `dEQP-VK.info.*` and `dEQP-VK.memory.*` then had one failure:
   `VK_KHR_device_address_commands` is unknown to CTS 1.4.5.3, which is why
   the pin moved to 1.4.6.2, where it passes.
4. **Event status read a stale 1.** amdgpu zeroes GTT buffers and RADV relies
   on it; the console's direct memory arrives holding what it last held. PS5
   GTT buffers are now zeroed (`pool_reset_reuse`, `submit_count_*` pass).
5. **E5B9G9R9 does not render** (HARDWARE_FINDINGS.md, this date): 500
   blits read back wrong; the format is no longer a colour target there.
6. **A submission's size and starting state** (HARDWARE_FINDINGS.md, this
   date). 131,000 draws in one command buffer never completed; split into two
   AGC submissions they drew only the first part. RADV now splits a long
   stream before a draw or dispatch when the winsys asks and re-emits its
   state after the split; the winsys splits only there or where a stream
   starts and repeats the preamble. `record_many_draws_*_2` pass.
7. **The tooling had to see failures.** A title may not `dup2` (EPERM), so the
   platform's `ps5_klog_capture_stderr` moves the `stderr` stream to a pipe
   that a thread writes to klog: RADV's messages and assertion failures now
   arrive. The CTS's crash handler hung the title (it writes its backtrace
   through a file in the working directory), so the PS5 platform reports a
   crash itself: the case is logged as Crash, the fault and a stack scan go
   to klog, and the title ends. A hang report gives the main thread's place
   after 45 s without output. `run-cts.py` runs lists in batches, resumes
   after a crash or hang, and records every result.

The E5B9G9R9 finding was on 1.4.5.3 with the fixes of item 3; the others
were rechecked on 1.4.6.2 (`recheck-5`: every case of the list passes or is
not supported). The full `api` group is the next run.

## 2026-09-26 — tessellation and geometry

The first full pass over the mustpass list (`main-1`) stopped at
tessellation and geometry, which faulted or drew nothing. The RADV smoke
title (PPSA99014) got checks for both (a quad patch from coordinates, from
control points and with varyings, levels 2 to 9; geometry strips of 16 to 128
vertices from 1, 2 and 4 points, counts from gl_PrimitiveIDIn, a colour
varying, recorded primitive IDs) and a probe loop around them: 50 of 50 pass.

1. **AGC owns the tessellation rings' registers** (HARDWARE_FINDINGS.md).
   RADV's winsys now hands AGC the factor ring (`sceAgcDriverSetTFRing`,
   through a new `ctx_set_tess_factor_ring` hook) and RADV's off-chip
   parameter (`sceAgcDriverSetHsOffchipParam`).
2. **No integer dot products** (HARDWARE_FINDINGS.md). An earlier fix that
   turned NGG culling off was the wrong cause and is gone: culling is on and
   the dot-product instructions are off.
3. **No legacy geometry shaders.** A legacy GS hangs and AGC exports no way
   to set its rings, so a GS compiled with the stage before it stays NGG
   (`radeon_info.has_legacy_gs`). Open: a tessellated GS amplifying past 256
   vertices (dEQP-VK.tessellation.geometry_interaction.limits.*, which NGG
   multi-cycling cannot serve with tessellation), streamout from a GS, and
   separately compiled GS shader objects (dEQP-VK.shader_object.link.*
   with an unlinked GS). Each still loses the device.
4. **Tooling.** Driver messages reach klog (the platform's stderr capture now
   moves the stream, since `dup2` is refused); the CTS title reports crashes
   itself (the CTS's handler hung) and names the case; a hang report gives
   the main thread's place; `run-cts.py --env` and the smoke title's
   `/app0/radv-smoke-env.txt` set the driver's environment.

Also this round: host image copy is off on this GPU as upstream keeps it off
for GFX10's swizzles (`radeon_info.gfx10_1_swizzles`); default thread stacks
come from direct memory (256 concurrent threads had exhausted flexible
memory); a fill-buffer case asked for a compute-only queue without checking
for one (backported upstream's check to the CTS fork); and the driver
rejects layers and reports only the priorities it serves.
