# CTS gaps before the next full run

Every case main-1 did not pass, and every reason it gave for "not supported",
has an owner here. The next full CTS run happens only once each item is
closed: fixed and proved by a targeted run of the groups behind it, or
recorded with the reason it cannot be offered. A "not supported" that the
port caused is open work, not an acceptable result.

The reference for "the port caused it" is upstream RADV itself: the same
Mesa revision built for Linux, faking a Navi21 with the amdgpu DRM shim, was
compared with the PS5 winsys's description through `vulkaninfo --json`
(2026-09-27). Upstream reports 22 extensions and 40 features the port does
not, and four more queue families. Everything else the CTS reports as not
supported, upstream reports the same way.

Counts are main-1's, over the 2,570,311 cases it had run when this was
written (of 2,786,062); they are refreshed when it ends.

## Failures

| Item | Cases | Status |
| --- | --- | --- |
| conditional_rendering.transform_feedback: four-stream capture from a GS under conditional rendering | 9 | open (ps5-gs-compute) |
| tessellation.geometry_interaction.limits: a tessellated GS past one NGG subgroup | 2 | open (slice 5, RADV_GS_COMPUTE.md) |
| descriptor_indexing.*_minNonUniform | 6 | open, not triaged |
| memory.map_placed: placed mapping fails, and the tests read /proc/self/maps | 5 | open: implement, or stop reporting VK_EXT_map_memory_placed |
| rasterization.culling.primitive_id | 1 | open, not triaged |
| The rest of main-1, and the transform_feedback group on the merged driver | ? | pending |

Recorded as failures but already closed: 1,418 fixed or reclassified by later
runs; about 10,500 in features since switched off (variable-rate shading,
fragment barycentrics, capture/replay addresses, device-generated commands),
which a targeted rerun must show as not supported.

## Not supported: gaps the port caused

Upstream RADV offers these on Navi21; the port does not yet.

| Item | Cases | What closes it |
| --- | --- | --- |
| Queue families: one graphics family only (upstream adds four compute queues and a sparse family). Exclusive compute queue, compute-only statistics, multi-queue synchronization, concurrent sharing | 249,885 + 2,846 | A probe of AGC's asynchronous compute. Note: upstream amdgpu leaves GFX1013's compute rings off as broken |
| Ray tracing: acceleration structures and ray queries | part of 116,323 | Done on ps5-rt (d55c9c0, rq-full-1); merge and report |
| Ray tracing pipelines, maintenance1, position fetch, pipeline library group handles | rest of 116,323 | The buffer-scratch branch (ps5-rt-buffer-scratch) and dEQP-VK.ray_tracing_pipeline |
| Sparse binding and residency, sparse atomics, shaderResourceMinLod and Residency, image2DViewOf3DSparse | 36,666 | Sparse virtual ranges in the winsys over exported direct-memory mapping functions: a feasibility probe first |
| Mesh and task shaders | 59,923 | Likely a hardware reason (the parameter cache has no per-primitive parameters, which mesh outputs need; task shaders also need a compute queue): prove it with a probe, then record it or implement it |
| Capture and replay addresses (buffer device address, descriptor buffer and heap, acceleration structures, ray tracing group handles) | about 1,000 | The winsys allocating at a requested address, which the console's fixed-address mapping allows (R88) |
| Memory types: no non-device-local memory | 2,650 | The coherence probe (VULKAN_1_4_PLAN.md S7) decides whether a host-cached type can be real |
| Calibrated timestamps, present timing | 1,299 | A GPU and CPU clock pair the winsys can read |
| External host memory (VK_EXT_external_memory_host) | 11 | The userptr probe (has_userptr) |
| Video (VK_KHR_video_*) | 11,149 | Open investigation: the console decodes video in hardware (ProsperoLight uses it); whether Vulkan Video can stand on its exported decoder is unknown |
| Window system: no surface or swapchain for the CTS (headless) | part of 32,544 | Mesa's headless WSI, and the VideoOut swapchain the port needs anyway |
| The sparse-binding bit on the only queue family, with no sparse support | honesty | Fixed with the sparse work, or removed now |
| conformanceVersion reports upstream's 1.4.5.3 | honesty | 0.0.0.0 until the full run passes |

## Not supported: with a reason

No work, each reason recorded where it was measured.

- **Hardware** (the GPU's fixed-function blocks are GC 10.1.3's,
  HARDWARE_FINDINGS.md): variable-rate shading (96,530), fragment
  barycentrics (8,313), RGB9E5 as a colour target, accelerated dot products
  and VK_VALVE_shader_mixed_float_dot_product.
- **Platform**: device-generated commands (238; B8, command fetch in the
  system context), performance queries (13; no exported stable power state),
  Linux file descriptors, dma-buf and DRM (52,824), other window systems,
  Win32 and Fuchsia handles.
- **Not offered by RADV for GFX10.3 either**: formats and sample counts
  (464,417: ASTC, ETC2, D24S8 and others), other vendors' and newer
  generations' extensions (187,331: cooperative matrices and vectors, NV and
  ARM extensions, tile images, opacity micromaps, data graphs), advanced blend
  operations, fragment density maps, partitioned subgroups, multisampled
  render to single sampled, unified image layouts, protected memory.

## Order

1. After main-1: merge ps5-rt, ps5-gs-compute and the GFX1013 traits, with
   the two honesty fixes.
2. The failures above, then whatever the end of main-1 and the
   transform_feedback group add.
3. Compute queues (the largest gap), then ray tracing pipelines, capture and
   replay, sparse, memory types, timestamps and host memory.
4. The mesh shader probe, the headless WSI, the video investigation, slice 5.
5. The full CTS run.
