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

## 2026-09-27 — shader objects, host image copy, depth clears

The second pass over main-1's failures (190 cases) left two groups, and a
rerun of the shader-object geometry cases (181) a third. All three are fixed
in the Mesa fork, and every one of those cases now passes or is not
supported.

1. **Geometry shader objects stay NGG.** A GS compiled alone read its NGG
   mode from a stage that was never initialised, and a VS or TES compiled
   for a GS disagreed with it (an assertion on user SGPRs, or a lost
   device). Where no legacy GS can run, every GS and every stage before one
   is NGG, and merged shaders compiled separately have no NGG culling. The
   14 cases left need mesh shaders, which this GPU does not have.
2. **Host image copy.** Vulkan 1.4 asks for hostImageCopy or a second queue
   that transfers (dEQP-VK.info.device_mandatory_features). Upstream keeps
   host image copy off on GFX10 for addrlib regressions; they are two bugs
   in addrlib's microblock copies with GFX10's (non-RB+) swizzles, which
   this GPU uses: an 8bpp 64KB_R_X microblock is not a rectangle (addrlib
   trapped), and x3 of a 16bpp 64KB_R_X or 64KB_Z_X microblock also flips
   address bit 8 (half of each microblock landed in another). addrlib now
   uses the microblock copies only where a microblock is 256 contiguous
   bytes of exactly its rectangle. A host check against addrlib's own
   per-texel address (every mode, 8 to 128 bpp, full mip chains, both
   directions, memcpy round trips) found both and passes; on the console,
   dEQP-VK.image.host_image_copy (73291 cases) passes or is not supported,
   and RADV turns host image copy on for GFX10.
3. **The depth clear bug** (HARDWARE_FINDINGS.md): the GPU description turns
   on Mesa's workaround for GFX1013's TC-compatible HTILE clear bug.

Next: the rest of main-1 on this build, with the shader-object group back
in; transform feedback stays out until a GS can stream out.

## 2026-09-27 — graphicsfuzz, device-generated commands, scratch

main-1's next groups turned up three problems, and the runner a fourth.

1. **Floats parsed as integers.** Graphicsfuzz shaders whose colour is decided
   by constants took their else branch: the CTS assembles SPIR-V text with
   SPIRV-Tools, whose `istream >> float` read 0.100000001 as 100000001. The
   console's `localeconv()` reports an empty decimal point while its `strtod`
   reads '.', and the platform's C-locale `strtof_l` spliced the empty point
   in place of '.'. Fixed in the shared platform layer (PS5_PayloadSDK
   9cf8084, its PROBE.md records the measurement). Found by comparing
   `NIR_DEBUG=print_fs` on the host and on the console pass by pass: the
   first difference was the SPIR-V constants.
2. **No device-generated commands.** The console cannot run a command buffer
   the GPU wrote (INDIRECT_BUFFER, B8), so VK_EXT_device_generated_commands
   is no longer reported (`radeon_info.has_gpu_written_ibs`).
3. **Buffer scratch** (HARDWARE_FINDINGS.md): ACO addresses scratch through
   buffer instructions on this GPU; the smoke title gained scratch checks
   (58 of 58 pass) and graphicsfuzz passes whole (757 cases).
4. **Tooling.** An asynchronous GPU fault kills the CTS title after the draw
   that caused it, and the unflushed QPA log with it: the runner blamed one
   innocent case per launch and lost the rest. It now takes the results klog
   printed for what the log lost, and notes when a fault was asynchronous.
   `run-cts.py --stderr-file` sends the driver's stderr and stdout to a file
   on the console and fetches it (klog drops lines of a large dump).

Checked and ruled out on the way: the console's libm results, its half-float
conversions (F16C and software) and its MXCSR (flush-to-zero and
denormals-are-zero are on, but clearing them changed nothing).

Open: the tessellated GS with more than 256 output vertices per input
primitive (dEQP-VK.tessellation.geometry_interaction.limits.*): NGG needs
per-instance multi-cycling there, which does not work with tessellation on
GFX10-class hardware, and no legacy GS can run.

Later the same day, three capabilities the console cannot serve stopped
being reported (each group now reads not supported):

- **Performance queries** (`radeon_info.has_perf_counters`): their
  profiling lock needs a stable power state, which no exported function
  sets.
- **Ray tracing.** Ray tracing pipelines call their shaders with a stack in
  scratch, addressed with flat scratch on GFX9+ (`radv_rt_pipelines_enabled`).
  An ACO port of those calls to buffer scratch compiles pipelines on the host
  (the Mesa fork's local branch `ps5-rt-buffer-scratch`, unverified), but
  every acceleration structure build faulted the GPU first, a write far past
  every buffer, even for an empty top level, so the GPU description turns
  ray tracing off until the build runs.
- A tessellation distribution mode does not rescue the GS amplification
  limit: the limits cases lost the device in all four modes.

Then three more from main-1's glsl and barycentric groups, each measured on
the console first (HARDWARE_FINDINGS.md has the measurements):

- **Fragment shader barycentrics** are no longer reported: this GPU's
  parameter cache is GFX10.1's, and per-vertex inputs of a second triangle
  arrived rotated whatever ROTATE_PC_PTR said (`radeon_info.
  has_ps_strict_vertex_order`; upstream draws the same line at GFX10.3). The
  smoke title's probes reproduce it: one triangle of either winding reads in
  order, the CTS's pair of triangles does not.
- **Depth mip layout.** addrlib now gets GFX1013's revision: with Navi10's it
  laid out 8 and 16 bpp depth mips without the mipmap fix the console's depth
  block follows, and shadow textures were written past their end.
- **The IEEE floating-point state.** The console starts a title with
  flush-to-zero and denormals-are-zero; the CTS's double-precision reference
  intervals flushed denormal quotients and rejected correct results. The
  platform's `ps5_fp_ieee()` now runs first in every PS5_Vulkan title, and
  threads inherit their creator's MXCSR. RetroArch gets the same call when it
  moves to the platform helpers.
