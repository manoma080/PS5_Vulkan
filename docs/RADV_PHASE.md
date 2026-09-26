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
