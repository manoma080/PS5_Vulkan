/*
 * PS5 Vulkan - RADV smoke test.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The first program RADV runs on the console (docs/VULKAN_1_4_PLAN.md, Phase 1):
 * an instance and a device through the RADV archive the title links, then four
 * results a working GPU path has to produce exactly -- a buffer fill, a buffer
 * copy, a compute dispatch and a triangle drawn over a clear -- each read back
 * and compared on the CPU. Every line goes to klog and to radv-smoke.txt beside
 * the title (/app0), which FTP reaches.
 *
 * It links no Vulkan loader: every command comes from RADV's
 * vk_icdGetInstanceProcAddr, as a title that links the archive gets them.
 */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "radv_smoke_shaders.h"

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

#if defined(__PROSPERO__)
#define RESULTS_PATH "/app0/radv-smoke.txt"
/* libkernel's klog writer: a title's stderr does not reach klog, and klog is
 * what tools/run-title.py watches for the run's end. */
int sceKernelDebugOutText(int channel, const char *text);
#else
#define RESULTS_PATH "radv-smoke.txt"
#endif

static FILE *results;
static unsigned passed, failed;

static void
report(const char *format, ...)
{
   char line[512];
   va_list args;
   va_start(args, format);
   vsnprintf(line, sizeof(line), format, args);
   va_end(args);
   fprintf(stderr, "[radv-smoke] %s\n", line);
   fflush(stderr);
#if defined(__PROSPERO__)
   char klog_line[600];
   snprintf(klog_line, sizeof(klog_line), "[radv-smoke] %s\n", line);
   sceKernelDebugOutText(0, klog_line);
#endif
   if (results) {
      fprintf(results, "%s\n", line);
      fflush(results);
   }
}

static void
check(bool ok, const char *what)
{
   report("%s %s", ok ? "PASS" : "FAIL", what);
   if (ok)
      passed++;
   else
      failed++;
}

/* ------------------------------------------------------------ entry points */

#define INSTANCE_COMMANDS(X)                                                                       \
   X(DestroyInstance)                                                                              \
   X(EnumeratePhysicalDevices)                                                                     \
   X(GetPhysicalDeviceProperties2)                                                                 \
   X(GetPhysicalDeviceQueueFamilyProperties)                                                       \
   X(GetPhysicalDeviceMemoryProperties)                                                            \
   X(CreateDevice)                                                                                 \
   X(GetDeviceProcAddr)

#define DEVICE_COMMANDS(X)                                                                         \
   X(DestroyDevice)                                                                                \
   X(GetDeviceQueue)                                                                               \
   X(QueueSubmit)                                                                                  \
   X(QueueWaitIdle)                                                                                \
   X(DeviceWaitIdle)                                                                               \
   X(CreateFence)                                                                                  \
   X(DestroyFence)                                                                                 \
   X(WaitForFences)                                                                                \
   X(ResetFences)                                                                                  \
   X(CreateCommandPool)                                                                            \
   X(DestroyCommandPool)                                                                           \
   X(AllocateCommandBuffers)                                                                       \
   X(BeginCommandBuffer)                                                                           \
   X(EndCommandBuffer)                                                                             \
   X(ResetCommandBuffer)                                                                           \
   X(CreateBuffer)                                                                                 \
   X(DestroyBuffer)                                                                                \
   X(GetBufferMemoryRequirements)                                                                  \
   X(CreateImage)                                                                                  \
   X(DestroyImage)                                                                                 \
   X(GetImageMemoryRequirements)                                                                   \
   X(CreateImageView)                                                                              \
   X(DestroyImageView)                                                                             \
   X(AllocateMemory)                                                                               \
   X(FreeMemory)                                                                                   \
   X(BindBufferMemory)                                                                             \
   X(BindImageMemory)                                                                              \
   X(MapMemory)                                                                                    \
   X(CmdFillBuffer)                                                                                \
   X(CmdCopyBuffer)                                                                                \
   X(CmdCopyImageToBuffer)                                                                         \
   X(CmdPipelineBarrier2)                                                                          \
   X(CmdBindPipeline)                                                                              \
   X(CmdBindDescriptorSets)                                                                        \
   X(CmdDispatch)                                                                                  \
   X(CmdBeginRendering)                                                                            \
   X(CmdEndRendering)                                                                              \
   X(CmdDraw)                                                                                      \
   X(CmdSetViewport)                                                                               \
   X(CmdSetScissor)                                                                                \
   X(CreateShaderModule)                                                                           \
   X(DestroyShaderModule)                                                                          \
   X(CreateDescriptorSetLayout)                                                                    \
   X(DestroyDescriptorSetLayout)                                                                   \
   X(CreatePipelineLayout)                                                                         \
   X(DestroyPipelineLayout)                                                                        \
   X(CreateComputePipelines)                                                                       \
   X(CreateGraphicsPipelines)                                                                      \
   X(DestroyPipeline)                                                                              \
   X(CreateDescriptorPool)                                                                         \
   X(DestroyDescriptorPool)                                                                        \
   X(AllocateDescriptorSets)                                                                       \
   X(UpdateDescriptorSets)

#define DECLARE(name) static PFN_vk##name vk##name;
INSTANCE_COMMANDS(DECLARE)
DEVICE_COMMANDS(DECLARE)
static PFN_vkCreateInstance vkCreateInstance;

static bool
load_instance(VkInstance instance)
{
   bool ok = true;
#define LOAD(name)                                                                                 \
   vk##name = (PFN_vk##name)vk_icdGetInstanceProcAddr(instance, "vk" #name);                       \
   if (!vk##name) {                                                                                \
      report("missing vk" #name);                                                                  \
      ok = false;                                                                                  \
   }
   INSTANCE_COMMANDS(LOAD)
#undef LOAD
   return ok;
}

static bool
load_device(VkDevice device)
{
   bool ok = true;
#define LOAD(name)                                                                                 \
   vk##name = (PFN_vk##name)vkGetDeviceProcAddr(device, "vk" #name);                               \
   if (!vk##name) {                                                                                \
      report("missing vk" #name);                                                                  \
      ok = false;                                                                                  \
   }
   DEVICE_COMMANDS(LOAD)
#undef LOAD
   return ok;
}

/* ----------------------------------------------------------------- context */

struct context {
   VkInstance instance;
   VkPhysicalDevice physical;
   VkDevice device;
   VkQueue queue;
   uint32_t family;
   VkPhysicalDeviceMemoryProperties memory;
   VkCommandPool pool;
   VkCommandBuffer cmd;
   VkFence fence;
};

static uint32_t
memory_type(const struct context *c, uint32_t bits, VkMemoryPropertyFlags wanted)
{
   for (uint32_t i = 0; i < c->memory.memoryTypeCount; i++) {
      if ((bits & (1u << i)) && (c->memory.memoryTypes[i].propertyFlags & wanted) == wanted)
         return i;
   }
   return UINT32_MAX;
}

struct buffer {
   VkBuffer buffer;
   VkDeviceMemory memory;
   void *map;
   VkDeviceSize size;
};

static bool
buffer_create(struct context *c, VkDeviceSize size, VkBufferUsageFlags usage, struct buffer *out)
{
   *out = (struct buffer){.size = size};
   const VkBufferCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   if (vkCreateBuffer(c->device, &info, NULL, &out->buffer) != VK_SUCCESS)
      return false;
   VkMemoryRequirements req;
   vkGetBufferMemoryRequirements(c->device, out->buffer, &req);
   const uint32_t type = memory_type(c, req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   const VkMemoryAllocateInfo alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = type,
   };
   if (type == UINT32_MAX || vkAllocateMemory(c->device, &alloc, NULL, &out->memory) != VK_SUCCESS ||
       vkBindBufferMemory(c->device, out->buffer, out->memory, 0) != VK_SUCCESS ||
       vkMapMemory(c->device, out->memory, 0, VK_WHOLE_SIZE, 0, &out->map) != VK_SUCCESS)
      return false;
   return true;
}

static void
buffer_destroy(struct context *c, struct buffer *b)
{
   vkDestroyBuffer(c->device, b->buffer, NULL);
   vkFreeMemory(c->device, b->memory, NULL);
}

static bool
begin(struct context *c)
{
   const VkCommandBufferBeginInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   return vkResetCommandBuffer(c->cmd, 0) == VK_SUCCESS && vkBeginCommandBuffer(c->cmd, &info) == VK_SUCCESS;
}

/* Ends, submits and waits for the command buffer, and makes what the GPU wrote
 * visible to the host's reads. */
static bool
submit_and_wait(struct context *c, const char *what)
{
   const VkMemoryBarrier2 host = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
      .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
   };
   const VkDependencyInfo dependency = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &host,
   };
   vkCmdPipelineBarrier2(c->cmd, &dependency);
   VkResult result = vkEndCommandBuffer(c->cmd);
   if (result != VK_SUCCESS) {
      report("%s: vkEndCommandBuffer returned %d", what, result);
      return false;
   }
   const VkSubmitInfo submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &c->cmd,
   };
   vkResetFences(c->device, 1, &c->fence);
   result = vkQueueSubmit(c->queue, 1, &submit, c->fence);
   if (result != VK_SUCCESS) {
      report("%s: vkQueueSubmit returned %d", what, result);
      return false;
   }
   result = vkWaitForFences(c->device, 1, &c->fence, VK_TRUE, UINT64_C(20000000000));
   if (result != VK_SUCCESS) {
      report("%s: vkWaitForFences returned %d", what, result);
      return false;
   }
   return true;
}

/* ------------------------------------------------------------------- tests */

static void
test_fill(struct context *c)
{
   struct buffer b;
   const VkDeviceSize size = 65536;
   if (!buffer_create(c, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &b)) {
      check(false, "fill: buffer");
      return;
   }
   memset(b.map, 0xcd, size);
   bool ok = begin(c);
   vkCmdFillBuffer(c->cmd, b.buffer, 0, size, 0x12345678u);
   ok = ok && submit_and_wait(c, "fill");
   uint32_t wrong = 0, first_wrong = UINT32_MAX;
   const uint32_t *words = b.map;
   for (uint32_t i = 0; ok && i < size / 4; i++) {
      if (words[i] != 0x12345678u) {
         if (first_wrong == UINT32_MAX)
            first_wrong = i;
         wrong++;
      }
   }
   if (wrong)
      report("fill: %u of %u words wrong, the first at %u reads 0x%08x", wrong, (unsigned)(size / 4), first_wrong,
             words[first_wrong]);
   check(ok && wrong == 0, "fill: vkCmdFillBuffer of 64 KiB reads back exactly");
   buffer_destroy(c, &b);
}

static void
test_copy(struct context *c)
{
   struct buffer src, dst;
   const VkDeviceSize size = 1u << 20;
   if (!buffer_create(c, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &src) ||
       !buffer_create(c, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &dst)) {
      check(false, "copy: buffers");
      return;
   }
   uint32_t *const in = src.map;
   for (uint32_t i = 0; i < size / 4; i++)
      in[i] = i * 2654435761u;
   memset(dst.map, 0, size);
   bool ok = begin(c);
   const VkBufferCopy region = {.srcOffset = 0, .dstOffset = 0, .size = size};
   vkCmdCopyBuffer(c->cmd, src.buffer, dst.buffer, 1, &region);
   ok = ok && submit_and_wait(c, "copy");
   const bool same = ok && memcmp(src.map, dst.map, size) == 0;
   if (ok && !same) {
      const uint32_t *const out = dst.map;
      for (uint32_t i = 0; i < size / 4; i++) {
         if (out[i] != in[i]) {
            report("copy: word %u reads 0x%08x, not 0x%08x", i, out[i], in[i]);
            break;
         }
      }
   }
   check(same, "copy: vkCmdCopyBuffer of 1 MiB reads back exactly");
   buffer_destroy(c, &src);
   buffer_destroy(c, &dst);
}

static VkShaderModule
shader(struct context *c, const uint32_t *code, size_t bytes)
{
   const VkShaderModuleCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = bytes,
      .pCode = code,
   };
   VkShaderModule module = VK_NULL_HANDLE;
   vkCreateShaderModule(c->device, &info, NULL, &module);
   return module;
}

static void
test_compute(struct context *c)
{
   enum { COUNT = 4096 };
   struct buffer b;
   if (!buffer_create(c, COUNT * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &b)) {
      check(false, "compute: buffer");
      return;
   }
   memset(b.map, 0, COUNT * 4);
   const VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
   };
   const VkDescriptorSetLayoutCreateInfo set_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &binding,
   };
   VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
   VkPipelineLayout layout = VK_NULL_HANDLE;
   VkPipeline pipeline = VK_NULL_HANDLE;
   VkDescriptorPool pool = VK_NULL_HANDLE;
   VkDescriptorSet set = VK_NULL_HANDLE;
   VkShaderModule module = shader(c, radv_smoke_comp, sizeof(radv_smoke_comp));
   bool ok = module && vkCreateDescriptorSetLayout(c->device, &set_info, NULL, &set_layout) == VK_SUCCESS;
   const VkPipelineLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &set_layout,
   };
   ok = ok && vkCreatePipelineLayout(c->device, &layout_info, NULL, &layout) == VK_SUCCESS;
   const VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
         {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = module,
            .pName = "main",
         },
      .layout = layout,
   };
   ok = ok && vkCreateComputePipelines(c->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline) == VK_SUCCESS;
   check(ok, "compute: SPIR-V compiles to a pipeline on the console");
   const VkDescriptorPoolSize size = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1};
   const VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &size,
   };
   ok = ok && vkCreateDescriptorPool(c->device, &pool_info, NULL, &pool) == VK_SUCCESS;
   const VkDescriptorSetAllocateInfo set_alloc = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &set_layout,
   };
   ok = ok && vkAllocateDescriptorSets(c->device, &set_alloc, &set) == VK_SUCCESS;
   if (ok) {
      const VkDescriptorBufferInfo buffer_info = {.buffer = b.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
      const VkWriteDescriptorSet write = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set,
         .dstBinding = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &buffer_info,
      };
      vkUpdateDescriptorSets(c->device, 1, &write, 0, NULL);
      ok = begin(c);
      vkCmdBindPipeline(c->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      vkCmdBindDescriptorSets(c->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
      vkCmdDispatch(c->cmd, COUNT / 64, 1, 1);
      ok = ok && submit_and_wait(c, "compute");
   }
   uint32_t wrong = 0;
   const uint32_t *const values = b.map;
   for (uint32_t i = 0; ok && i < COUNT; i++) {
      if (values[i] != i * 3u + 7u) {
         if (wrong == 0)
            report("compute: value %u reads %u, not %u", i, values[i], i * 3u + 7u);
         wrong++;
      }
   }
   check(ok && wrong == 0, "compute: 64 workgroups of 64 write every value their index names");
   vkDestroyPipeline(c->device, pipeline, NULL);
   vkDestroyDescriptorPool(c->device, pool, NULL);
   vkDestroyPipelineLayout(c->device, layout, NULL);
   vkDestroyDescriptorSetLayout(c->device, set_layout, NULL);
   vkDestroyShaderModule(c->device, module, NULL);
   buffer_destroy(c, &b);
}

static void
test_triangle(struct context *c)
{
   enum { SIZE = 256 };
   const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
   VkImage image = VK_NULL_HANDLE;
   VkDeviceMemory image_memory = VK_NULL_HANDLE;
   VkImageView view = VK_NULL_HANDLE;
   VkPipelineLayout layout = VK_NULL_HANDLE;
   VkPipeline pipeline = VK_NULL_HANDLE;
   struct buffer readback;
   if (!buffer_create(c, SIZE * SIZE * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback)) {
      check(false, "triangle: readback buffer");
      return;
   }
   memset(readback.map, 0, SIZE * SIZE * 4);
   const VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {SIZE, SIZE, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   bool ok = vkCreateImage(c->device, &image_info, NULL, &image) == VK_SUCCESS;
   if (ok) {
      VkMemoryRequirements req;
      vkGetImageMemoryRequirements(c->device, image, &req);
      const VkMemoryAllocateInfo alloc = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = req.size,
         .memoryTypeIndex = memory_type(c, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
      };
      ok = vkAllocateMemory(c->device, &alloc, NULL, &image_memory) == VK_SUCCESS &&
           vkBindImageMemory(c->device, image, image_memory, 0) == VK_SUCCESS;
   }
   const VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
   };
   ok = ok && vkCreateImageView(c->device, &view_info, NULL, &view) == VK_SUCCESS;

   VkShaderModule vert = shader(c, radv_smoke_vert, sizeof(radv_smoke_vert));
   VkShaderModule frag = shader(c, radv_smoke_frag, sizeof(radv_smoke_frag));
   const VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
   ok = ok && vert && frag && vkCreatePipelineLayout(c->device, &layout_info, NULL, &layout) == VK_SUCCESS;
   const VkPipelineShaderStageCreateInfo stages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vert,
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = frag,
       .pName = "main"},
   };
   const VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
   const VkPipelineInputAssemblyStateCreateInfo assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
   };
   const VkPipelineViewportStateCreateInfo viewport_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
   };
   const VkPipelineRasterizationStateCreateInfo raster = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
   };
   const VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };
   const VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask = 0xf};
   const VkPipelineColorBlendStateCreateInfo blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blend_attachment,
   };
   const VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
   const VkPipelineDynamicStateCreateInfo dynamic = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates = dynamic_states,
   };
   const VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &format,
   };
   const VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = 2,
      .pStages = stages,
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &assembly,
      .pViewportState = &viewport_state,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pColorBlendState = &blend,
      .pDynamicState = &dynamic,
      .layout = layout,
   };
   ok = ok && vkCreateGraphicsPipelines(c->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline) == VK_SUCCESS;
   check(ok, "triangle: a graphics pipeline compiles on the console");

   if (ok) {
      ok = begin(c);
      const VkImageMemoryBarrier2 to_attachment = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .image = image,
         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      const VkDependencyInfo first = {
         .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
         .imageMemoryBarrierCount = 1,
         .pImageMemoryBarriers = &to_attachment,
      };
      vkCmdPipelineBarrier2(c->cmd, &first);
      const VkRenderingAttachmentInfo attachment = {
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = view,
         .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .clearValue = {.color = {.float32 = {0.0f, 0.0f, 1.0f, 1.0f}}},
      };
      const VkRenderingInfo rendering = {
         .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
         .renderArea = {{0, 0}, {SIZE, SIZE}},
         .layerCount = 1,
         .colorAttachmentCount = 1,
         .pColorAttachments = &attachment,
      };
      vkCmdBeginRendering(c->cmd, &rendering);
      vkCmdBindPipeline(c->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      const VkViewport viewport = {0.0f, 0.0f, (float)SIZE, (float)SIZE, 0.0f, 1.0f};
      const VkRect2D scissor = {{0, 0}, {SIZE, SIZE}};
      vkCmdSetViewport(c->cmd, 0, 1, &viewport);
      vkCmdSetScissor(c->cmd, 0, 1, &scissor);
      vkCmdDraw(c->cmd, 3, 1, 0, 0);
      vkCmdEndRendering(c->cmd);
      const VkImageMemoryBarrier2 to_transfer = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
         .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
         .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .image = image,
         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      const VkDependencyInfo second = {
         .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
         .imageMemoryBarrierCount = 1,
         .pImageMemoryBarriers = &to_transfer,
      };
      vkCmdPipelineBarrier2(c->cmd, &second);
      const VkBufferImageCopy copy = {
         .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
         .imageExtent = {SIZE, SIZE, 1},
      };
      vkCmdCopyImageToBuffer(c->cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
      ok = ok && submit_and_wait(c, "triangle");
   }

   /* The triangle covers x/128 + y/256 < 1 (Vulkan's y runs down the target);
    * every other texel keeps the clear. */
   uint32_t wrong = 0;
   const uint32_t red = 0xff0000ffu, blue = 0xffff0000u;
   const uint32_t *const texels = readback.map;
   for (uint32_t y = 0; ok && y < SIZE; y++) {
      for (uint32_t x = 0; x < SIZE; x++) {
         const double edge = (x + 0.5) / 128.0 + (y + 0.5) / 256.0;
         /* Texels whose centre is within half a texel of the edge may go
          * either way. */
         if (edge > 0.995 && edge < 1.005)
            continue;
         const uint32_t expected = edge < 1.0 ? red : blue;
         if (texels[y * SIZE + x] != expected) {
            if (wrong < 4)
               report("triangle: texel (%u, %u) reads 0x%08x, not 0x%08x", x, y, texels[y * SIZE + x], expected);
            wrong++;
         }
      }
   }
   if (wrong)
      report("triangle: %u texels wrong", wrong);
   check(ok && wrong == 0, "triangle: a clear and a draw read back texel for texel");

   vkDestroyPipeline(c->device, pipeline, NULL);
   vkDestroyPipelineLayout(c->device, layout, NULL);
   vkDestroyShaderModule(c->device, vert, NULL);
   vkDestroyShaderModule(c->device, frag, NULL);
   vkDestroyImageView(c->device, view, NULL);
   vkDestroyImage(c->device, image, NULL);
   vkFreeMemory(c->device, image_memory, NULL);
   buffer_destroy(c, &readback);
}

/* -------------------------------------------------------------------- main */

static bool
context_create(struct context *c)
{
   vkCreateInstance = (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
   if (!vkCreateInstance) {
      report("vk_icdGetInstanceProcAddr has no vkCreateInstance");
      return false;
   }
   const VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "radv-smoke",
      .apiVersion = VK_API_VERSION_1_4,
   };
   const VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
   };
   VkResult result = vkCreateInstance(&instance_info, NULL, &c->instance);
   check(result == VK_SUCCESS, "vkCreateInstance");
   if (result != VK_SUCCESS || !load_instance(c->instance))
      return false;
   uint32_t count = 1;
   result = vkEnumeratePhysicalDevices(c->instance, &count, &c->physical);
   check((result == VK_SUCCESS || result == VK_INCOMPLETE) && count == 1, "one physical device");
   if (count == 0)
      return false;

   VkPhysicalDeviceDriverProperties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
   VkPhysicalDeviceProperties2 props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver};
   vkGetPhysicalDeviceProperties2(c->physical, &props);
   report("device: %s, Vulkan %u.%u.%u, %s %s", props.properties.deviceName,
          VK_API_VERSION_MAJOR(props.properties.apiVersion), VK_API_VERSION_MINOR(props.properties.apiVersion),
          VK_API_VERSION_PATCH(props.properties.apiVersion), driver.driverName, driver.driverInfo);

   VkQueueFamilyProperties families[8];
   count = 8;
   vkGetPhysicalDeviceQueueFamilyProperties(c->physical, &count, families);
   c->family = UINT32_MAX;
   for (uint32_t i = 0; i < count; i++) {
      if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
          (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
         c->family = i;
         break;
      }
   }
   if (c->family == UINT32_MAX) {
      check(false, "a graphics and compute queue family");
      return false;
   }
   vkGetPhysicalDeviceMemoryProperties(c->physical, &c->memory);

   const float priority = 1.0f;
   const VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = c->family,
      .queueCount = 1,
      .pQueuePriorities = &priority,
   };
   VkPhysicalDeviceVulkan13Features features13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .synchronization2 = VK_TRUE,
      .dynamicRendering = VK_TRUE,
   };
   const VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features13,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
   };
   result = vkCreateDevice(c->physical, &device_info, NULL, &c->device);
   check(result == VK_SUCCESS, "vkCreateDevice");
   if (result != VK_SUCCESS || !load_device(c->device))
      return false;
   vkGetDeviceQueue(c->device, c->family, 0, &c->queue);

   const VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = c->family,
   };
   const VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   if (vkCreateCommandPool(c->device, &pool_info, NULL, &c->pool) != VK_SUCCESS ||
       vkCreateFence(c->device, &fence_info, NULL, &c->fence) != VK_SUCCESS)
      return false;
   const VkCommandBufferAllocateInfo cmd_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = c->pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   return vkAllocateCommandBuffers(c->device, &cmd_info, &c->cmd) == VK_SUCCESS;
}

int
main(void)
{
   results = fopen(RESULTS_PATH, "w");
   report("RADV smoke test starts");
   struct context c = {0};
   if (context_create(&c)) {
      test_fill(&c);
      test_copy(&c);
      test_compute(&c);
      test_triangle(&c);
   }
   if (c.device) {
      vkDeviceWaitIdle(c.device);
      vkDestroyFence(c.device, c.fence, NULL);
      vkDestroyCommandPool(c.device, c.pool, NULL);
      vkDestroyDevice(c.device, NULL);
   }
   if (c.instance)
      vkDestroyInstance(c.instance, NULL);
   report("RADV smoke test ends: %u passed, %u failed", passed, failed);
   if (results)
      fclose(results);
   return failed == 0 && passed > 0 ? 0 : 1;
}
