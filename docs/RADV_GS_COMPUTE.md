# Geometry shaders the hardware cannot run (RADV, route B)

_Design, 2026-09-27. Slices 1 to 3 done on the Mesa fork's branch
ps5-gs-compute (88e5322); slice 4 (pipeline libraries, shader objects) next._

## Why

A geometry shader on this GPU always runs as NGG: a legacy GS needs rings
whose registers AGC owns and no exported function sets
(HARDWARE_FINDINGS.md, 2026-09-26 and 2026-09-27). NGG covers every GS but two
kinds, and both are part of what I report:

1. **Transform feedback from a GS.** NGG streamout before GFX11 orders its
   buffer writes with GDS ordered counts, which a title cannot reach. Every
   dEQP-VK.transform_feedback case that captures from a GS fails (run xfb-1);
   capture from a vertex or tessellation evaluation shader passes, through
   legacy hardware VS streamout.
2. **A tessellated GS amplifying past 256 vertices** (4 invocations of 256
   vertices, or 32 invocations: Vulkan's minimum limits). One NGG subgroup
   holds 256 output vertices, and the per-instance mode that splits a
   primitive's invocations across subgroups hangs with tessellation on
   GFX10-class hardware (dEQP-VK.tessellation.geometry_interaction.limits).

Not reporting transform feedback, tessellation or geometry shaders would be
conformant too, but each is real on this GPU in every other case, and Eden
(the Switch emulator, a later target) uses all three.

## How

Run exactly those pipelines' pre-rasterization stages as compute, the way
Mesa's Asahi driver runs every GS (its hardware has none), with the shared
lowering in `src/poly` (also KosmicKrisp's):

- the vertex stage, or VS, TCS, the tessellator and TES for case 2, as
  compute writing their outputs to memory (`poly_nir_lower_vs_before_gs`,
  `poly_nir_lower_tcs`/`tes`, poly's software tessellator);
- a count pass when the GS's output count is not static, which keeps the
  GS's memory side effects so that they happen once (with static counts the
  GS proper keeps them instead);
- a prefix sum over the counts, which gives each input primitive its place
  in the transform feedback buffers and the index buffer in API order;
- the GS proper, writing transform feedback and the vertices to rasterize;
- an indexed indirect draw with a pass-through vertex shader reading them.

Every other pipeline keeps the hardware path. Queries (primitives generated,
transform feedback, pipeline statistics) take their counts from the compute
passes.

## Build

poly's kernels are OpenCL C compiled at build time by `mesa_clc` and
`vtn_bindgen2`, host tools built from the same Mesa revision (the host's LLVM
and Clang 22, libclc, the SPIR-V LLVM translator and SPIRV-Tools; a native
build of the two tools takes about 30 s). The PS5 cross build then uses them
with `-Dmesa-clc=system` and builds poly for RADV. Checked: the fork's poly
kernels compile to SPIR-V and bind with those tools (2026-09-27).

## Shape in RADV

Follow poly's model whole, as Asahi does: the vertex stage runs as a compute
dispatch over the unrolled vertex stream (poly's GS reads its inputs by
position in that stream, which a hardware VS, seeing only index values, cannot
give), then the GS count pass, the prefix sum, the pre-GS setup and the GS
proper as compute, all fed the graphics state's descriptors, push constants
and vertex buffers. The pipeline's hardware vertex stage becomes poly's
rasterization copy shader, reading the GS output, with the application's
fragment shader. Direct draws size poly's parameter blocks and buffers on the
CPU from the command buffer's upload pool; indirect draws use a GPU heap and
a setup kernel.

Built in vertical slices, each proved on the console before the next:

1. A VS and GS pipeline, direct non-indexed draws, forced through compute by
   a debug switch and checked by the smoke title's geometry checks (known
   answers from the NGG path).
2. Indexed, instanced and indirect draws; primitive restart.
3. Transform feedback and its queries (the target).
4. Graphics pipeline libraries and shader objects.
5. Tessellation, with poly's tessellator.

## Order of work

1. Host tools script, pinned like tools/build-radv.sh; poly enabled in the
   PS5 cross build.
2. Case 1 for a VS feeding a GS (the bulk of the failing cases).
3. Case 2 and case 1 behind tessellation, with poly's tessellator.
4. Queries and pipeline statistics; the transform_feedback, geometry and
   tessellation groups whole on the console.

## Slice 1 (2026-09-27)

Done: VS and GS pipelines, vkCmdDraw and vkCmdDrawIndexed, behind
`RADV_PS5_GS_COMPUTE=1`. Each draw dispatches the vertex shader, the count
pass and the GS proper with the graphics descriptor sets, push constants and
vertex buffers, then draws poly's output through the rasterization copy. The
smoke title passes 61 of 61 on the console with every GS forced through this
path: strips with a count chosen per primitive, early return, varyings,
primitive IDs, and a GS recording its invocations in a storage buffer.

Found on the way:

- RADV's dynamic topology holds the hardware primitive type, not the Vulkan
  enum; poly needs the latter.
- Invocations past poly's grid fault the GPU: every pass but the pre-GS setup
  checks its grid.
- With static counts poly makes no count pass and stripped the GS's memory
  writes from the GS proper, the only full run left. The fork's poly strips
  them only when a count pass keeps them (fde2cc2).

## Slice 2 (2026-09-27)

Every draw command handles such a pipeline. Multi-draws run one draw each.
A draw whose counts live in memory (indirect, indirect count, byte count)
is sized on the GPU by a meta pass: it reads the arguments, writes poly's
vertex and geometry parameters, allocates the vertex outputs, counts and
output indices from a heap each command buffer owns, and writes the
workgroup counts of the indirect dispatches that follow; the rasterization
copy draws indirectly. An indexed draw with primitive restart is first
unrolled on the GPU into an indexed draw of the decomposed list topology. A
draw too large for 32-bit counts or for the heap draws nothing rather than
write out of bounds. These passes are RADV's own OpenCL
(`src/amd/vulkan/cl/radv_gs_compute.cl`), turned into NIR builder functions
the way poly's are.

The smoke title's draw checks (indexed with a vertex offset, indexed
indirect, indirect, indirect count, restart direct and indirect) pass on the
NGG path and with every geometry shader forced through compute: 73 of 73.

## Slice 3 (2026-09-27)

A geometry shader with transform feedback outputs now always runs as
compute. Its draws continue from the offsets hardware streamout holds: they
go to memory, poly's pre-GS pass places each draw's output after a prefix sum
of the counts and advances them, the GS proper writes the buffers, and the
offsets go back to the hardware before the next draw, so hardware and
compute capture mix freely within one transform feedback. The rasterization
copy draws with streamout suspended and no shader query counting.

Primitives generated and written per stream count into memory counters each
command buffer owns. Transform feedback and primitives generated query
pools grow by a pair of snapshots of those counters, taken after the
hardware's at the beginning and the end, and their results add them in.

On the console, the 6685-case transform_feedback sample of run xfb-1 goes
from 1291 passing and 4529 failing to 5818 passing and 2 failing (run
xfb-gsc-3; both failures use graphics pipeline libraries, which slice 4
routes). Nothing that passed fails.

