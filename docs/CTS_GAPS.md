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

main-1 ended on 2026-09-27 with 2,786,062 cases: 1,098,388 pass, 43 quality
warnings, 1,675,626 not supported, and 12,005 that did not pass (11,909
failures, 66 crashes, 26 lost devices, 4 resource errors).

## Failures

Of main-1's 12,005, 1,435 pass in later runs (fixes made during main-1),
10,560 belong to features switched off during main-1 (variable-rate shading
4,701, fragment barycentrics 4,850, capture and replay addresses 1,010), and
the rest are below.

| Item | Cases | Status |
| --- | --- | --- |
| The switched-off features' cases | 10,560 | a targeted rerun on the merged driver must show them not supported |
| conditional_rendering.transform_feedback | 9 | fixed on ps5-gs-compute (b26797e): a geometry shader's push constants now reach its rasterization copy (gsc-pc-gate-1) |
| descriptor_indexing.*_minNonUniform, rasterization.culling.primitive_id | 7 | pass on 3057cb5 (triage-untriaged-1); they failed on builds from before the fixes made during main-1 |
| memory.map_placed | 5 | placed maps implemented on ps5-gaps (a2dc2fc); 8 more cases need memfd_create, now in the platform layer (SDK fork 71f2494), to be proved together |
| tessellation.geometry_interaction.limits | 2 | open (slice 5, RADV_GS_COMPUTE.md) |
| shader_object.misc.*.geometry_streams.enabled | 2 | open: a geometry shader object with streams needs the compute passes built when its draw binds (RADV_GS_COMPUTE.md, Open) |
| ray_tracing_pipeline (acceleration structures, builtin) | 5 | with ray tracing pipelines, below |

## Not supported: gaps the port caused

Upstream RADV offers these on Navi21; the port does not yet.

| Item | Cases | What closes it |
| --- | --- | --- |
| Queue families: one graphics family only (upstream adds four compute queues and a sparse family). Exclusive compute queue, compute-only statistics, multi-queue synchronization, concurrent sharing | 249,885 + 2,846 | A console probe of AGC's asynchronous compute submission (sceAgcDriverSubmitAcb), designed but not run yet. Public hints: the AnyPS5 emulator models it as (queue, the submission description sceAgcDriverSubmitDcb takes), queues 0x20 to 0x57; Mesa keeps GFX1013's compute queue off as broken, and a public BC-250 project traces that to asynchronous threadgroup dispatches with partial threadgroups (RADV's has_async_compute_threadgroup_bug workaround) and to amdgpu's queue teardown. The winsys also needs cross-queue waits |
| Ray tracing: acceleration structures and ray queries | part of 116,323 | Done on ps5-rt (d55c9c0, rq-full-1); merge and report |
| Ray tracing pipelines, maintenance1, position fetch, pipeline library group handles | rest of 116,323 | ps5-rt-pipelines: the buffer-scratch calls on top of ps5-rt's traversal fix, measured with dEQP-VK.ray_tracing_pipeline |
| Sparse binding and residency, sparse atomics, shaderResourceMinLod and Residency, image2DViewOf3DSparse | 36,666 | Sparse virtual ranges in the winsys over exported direct-memory mapping functions (a range of pages mapped at several addresses already works for the platform's shared memory): a feasibility probe first |
| Mesh and task shaders | 59,923 | RADV's mesh path on GFX10.3 exports per-primitive attributes through the parameter cache, which this GPU does not have (measured: an implicit primitive ID read 0 as a per-primitive parameter, HARDWARE_FINDINGS.md). Real mesh shaders need that emulated: the mesh shader run as compute and its primitives drawn with per-primitive values as flat attributes, as geometry shaders run now. Task shaders also need a compute queue |
| Capture and replay addresses (buffer device address, descriptor buffer and heap, acceleration structures, ray tracing group handles) | about 1,000 | The winsys allocating at a requested address, which the console's fixed-address mapping allows (R88) |
| Memory types: no non-device-local memory | 2,650 | The coherence probe (VULKAN_1_4_PLAN.md S7) decides whether a host-cached type can be real |
| Calibrated timestamps, present timing | 1,299 | A GPU and CPU clock pair the winsys can read |
| External host memory (VK_EXT_external_memory_host) | 11 | The userptr probe (has_userptr) |
| Video (VK_KHR_video_*) | 11,149 | Open investigation: the console decodes video in hardware (ProsperoLight uses it); whether Vulkan Video can stand on its exported decoder is unknown |
| Window system: no surface or swapchain for the CTS (headless) | part of 32,544 | Mesa's headless WSI, and the VideoOut swapchain the port needs anyway |
| The sparse-binding bit on the only queue family, with no sparse support | honesty | Removed on ps5-gaps (a2dc2fc) |
| conformanceVersion reports upstream's 1.4.5.3 | honesty | 0.0.0.0 on ps5-gaps (a2dc2fc) until the full run passes |

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
  ARM extensions, tile images, opacity micromaps, data graphs), inherited
  conditional rendering (482), advanced blend operations, fragment density
  maps, partitioned subgroups, multisampled render to single sampled, unified
  image layouts, protected memory.

## Order

1. Merge ps5-rt, ps5-gs-compute (with b26797e), the GFX1013 traits and
   ps5-gaps (with memfd_create), then the targeted rerun: every main-1 case
   that did not pass, the whole transform_feedback group, and the groups the
   branches touch.
2. Ray tracing pipelines, the shader object streams, slice 5.
3. Compute queues (the largest gap), capture and replay, sparse, memory types,
   timestamps and host memory.
4. Mesh shaders as compute, the headless WSI, the video investigation.
5. The full CTS run.
