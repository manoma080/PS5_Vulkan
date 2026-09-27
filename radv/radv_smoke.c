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
#include <pthread.h>
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
/* ps5platform/klog.h: the driver's messages on standard error, in klog. */
int ps5_klog_capture_stderr(const char *prefix);
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
   X(EnumerateDeviceExtensionProperties)                                                           \
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
   /* VK_KHR_fragment_shader_barycentric is reported and enabled. */
   bool barycentric;
   /* VK_KHR_fragment_shading_rate is reported, and its pipeline rate enabled. */
   bool shading_rate;
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

enum { TARGET_SIZE = 256 };

/* Draws vertex_count vertices with the stages into a cleared blue 256-square
 * RGBA8 target and reads the target back into readback. */
static bool
render_readback_with(struct context *c, const char *what, const VkPipelineShaderStageCreateInfo *stages,
                     uint32_t stage_count, VkPrimitiveTopology topology, uint32_t patch_points,
                     uint32_t vertex_count, struct buffer *readback, VkBuffer storage, const void *pipeline_next)
{
   const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
   VkImage image = VK_NULL_HANDLE;
   VkDeviceMemory image_memory = VK_NULL_HANDLE;
   VkImageView view = VK_NULL_HANDLE;
   VkPipelineLayout layout = VK_NULL_HANDLE;
   VkPipeline pipeline = VK_NULL_HANDLE;
   VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
   VkDescriptorPool pool = VK_NULL_HANDLE;
   VkDescriptorSet set = VK_NULL_HANDLE;
   memset(readback->map, 0, TARGET_SIZE * TARGET_SIZE * 4);
   const VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {TARGET_SIZE, TARGET_SIZE, 1},
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
   if (storage) {
      /* One storage buffer at set 0, binding 0, for every graphics stage. */
      const VkDescriptorSetLayoutBinding binding = {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
      };
      const VkDescriptorSetLayoutCreateInfo set_info = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .bindingCount = 1,
         .pBindings = &binding,
      };
      const VkDescriptorPoolSize size = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1};
      const VkDescriptorPoolCreateInfo pool_info = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
         .maxSets = 1,
         .poolSizeCount = 1,
         .pPoolSizes = &size,
      };
      ok = ok && vkCreateDescriptorSetLayout(c->device, &set_info, NULL, &set_layout) == VK_SUCCESS &&
           vkCreateDescriptorPool(c->device, &pool_info, NULL, &pool) == VK_SUCCESS;
      const VkDescriptorSetAllocateInfo set_alloc = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool = pool,
         .descriptorSetCount = 1,
         .pSetLayouts = &set_layout,
      };
      ok = ok && vkAllocateDescriptorSets(c->device, &set_alloc, &set) == VK_SUCCESS;
      if (ok) {
         const VkDescriptorBufferInfo buffer_info = {.buffer = storage, .offset = 0, .range = VK_WHOLE_SIZE};
         const VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &buffer_info,
         };
         vkUpdateDescriptorSets(c->device, 1, &write, 0, NULL);
      }
   }
   const VkPipelineLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = storage ? 1 : 0,
      .pSetLayouts = &set_layout,
   };
   ok = ok && vkCreatePipelineLayout(c->device, &layout_info, NULL, &layout) == VK_SUCCESS;
   const VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
   const VkPipelineInputAssemblyStateCreateInfo assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = topology,
   };
   const VkPipelineTessellationStateCreateInfo tessellation = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
      .patchControlPoints = patch_points,
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
      .pNext = pipeline_next,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &format,
   };
   const VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = stage_count,
      .pStages = stages,
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &assembly,
      .pTessellationState = patch_points ? &tessellation : NULL,
      .pViewportState = &viewport_state,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pColorBlendState = &blend,
      .pDynamicState = &dynamic,
      .layout = layout,
   };
   ok = ok && vkCreateGraphicsPipelines(c->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline) == VK_SUCCESS;
   char label[96];
   snprintf(label, sizeof(label), "%s: a graphics pipeline compiles on the console", what);
   check(ok, label);

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
         .renderArea = {{0, 0}, {TARGET_SIZE, TARGET_SIZE}},
         .layerCount = 1,
         .colorAttachmentCount = 1,
         .pColorAttachments = &attachment,
      };
      vkCmdBeginRendering(c->cmd, &rendering);
      vkCmdBindPipeline(c->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      if (set)
         vkCmdBindDescriptorSets(c->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
      const VkViewport viewport = {0.0f, 0.0f, (float)TARGET_SIZE, (float)TARGET_SIZE, 0.0f, 1.0f};
      const VkRect2D scissor = {{0, 0}, {TARGET_SIZE, TARGET_SIZE}};
      vkCmdSetViewport(c->cmd, 0, 1, &viewport);
      vkCmdSetScissor(c->cmd, 0, 1, &scissor);
      vkCmdDraw(c->cmd, vertex_count, 1, 0, 0);
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
         .imageExtent = {TARGET_SIZE, TARGET_SIZE, 1},
      };
      vkCmdCopyImageToBuffer(c->cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->buffer, 1, &copy);
      ok = ok && submit_and_wait(c, what);
   }

   vkDestroyPipeline(c->device, pipeline, NULL);
   vkDestroyPipelineLayout(c->device, layout, NULL);
   vkDestroyDescriptorPool(c->device, pool, NULL);
   vkDestroyDescriptorSetLayout(c->device, set_layout, NULL);
   vkDestroyImageView(c->device, view, NULL);
   vkDestroyImage(c->device, image, NULL);
   vkFreeMemory(c->device, image_memory, NULL);
   return ok;
}

static bool
render_readback(struct context *c, const char *what, const VkPipelineShaderStageCreateInfo *stages,
                uint32_t stage_count, VkPrimitiveTopology topology, uint32_t patch_points, uint32_t vertex_count,
                struct buffer *readback)
{
   return render_readback_with(c, what, stages, stage_count, topology, patch_points, vertex_count, readback,
                               VK_NULL_HANDLE, NULL);
}

static VkPipelineShaderStageCreateInfo
stage_info(VkShaderStageFlagBits stage, VkShaderModule module)
{
   return (VkPipelineShaderStageCreateInfo){
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = stage,
      .module = module,
      .pName = "main",
   };
}

static void
test_triangle(struct context *c)
{
   struct buffer readback;
   if (!buffer_create(c, TARGET_SIZE * TARGET_SIZE * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback)) {
      check(false, "triangle: readback buffer");
      return;
   }
   VkShaderModule vert = shader(c, radv_smoke_vert, sizeof(radv_smoke_vert));
   VkShaderModule frag = shader(c, radv_smoke_frag, sizeof(radv_smoke_frag));
   const VkPipelineShaderStageCreateInfo stages[2] = {
      stage_info(VK_SHADER_STAGE_VERTEX_BIT, vert),
      stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, frag),
   };
   bool ok = vert && frag &&
             render_readback(c, "triangle", stages, 2, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, 3, &readback);

   /* The triangle covers x/128 + y/256 < 1 (Vulkan's y runs down the target);
    * every other texel keeps the clear. */
   uint32_t wrong = 0;
   const uint32_t red = 0xff0000ffu, blue = 0xffff0000u;
   const uint32_t *const texels = readback.map;
   for (uint32_t y = 0; ok && y < TARGET_SIZE; y++) {
      for (uint32_t x = 0; x < TARGET_SIZE; x++) {
         const double edge = (x + 0.5) / 128.0 + (y + 0.5) / 256.0;
         /* Texels whose centre is within half a texel of the edge may go
          * either way. */
         if (edge > 0.995 && edge < 1.005)
            continue;
         const uint32_t expected = edge < 1.0 ? red : blue;
         if (texels[y * TARGET_SIZE + x] != expected) {
            if (wrong < 4)
               report("triangle: texel (%u, %u) reads 0x%08x, not 0x%08x", x, y, texels[y * TARGET_SIZE + x],
                      expected);
            wrong++;
         }
      }
   }
   if (wrong)
      report("triangle: %u texels wrong", wrong);
   check(ok && wrong == 0, "triangle: a clear and a draw read back texel for texel");

   vkDestroyShaderModule(c->device, vert, NULL);
   vkDestroyShaderModule(c->device, frag, NULL);
   buffer_destroy(c, &readback);
}

/* The triangle of test_triangle, shaded by a fragment shader whose private
 * array of n floats (a specialisation constant) is written and read at
 * indices that depend on the texel: past a few dozen floats the compiler keeps
 * it in scratch memory. Each texel's colour is the array's sum. */
static void
test_scratch(struct context *c)
{
   struct buffer readback;
   if (!buffer_create(c, TARGET_SIZE * TARGET_SIZE * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback)) {
      check(false, "scratch: readback buffer");
      return;
   }
   VkShaderModule vert = shader(c, radv_smoke_vert, sizeof(radv_smoke_vert));
   VkShaderModule frag = shader(c, radv_smoke_scratch_frag, sizeof(radv_smoke_scratch_frag));
   static const int32_t sizes[] = {16, 64, 256, 1024};
   for (unsigned v = 0; v < sizeof(sizes) / sizeof(sizes[0]); v++) {
      const int32_t n = sizes[v];
      const VkSpecializationMapEntry entry = {.constantID = 0, .offset = 0, .size = sizeof(n)};
      const VkSpecializationInfo specialisation = {
         .mapEntryCount = 1,
         .pMapEntries = &entry,
         .dataSize = sizeof(n),
         .pData = &n,
      };
      VkPipelineShaderStageCreateInfo stages[2] = {
         stage_info(VK_SHADER_STAGE_VERTEX_BIT, vert),
         stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, frag),
      };
      stages[1].pSpecializationInfo = &specialisation;
      char what[64];
      snprintf(what, sizeof(what), "scratch with %d floats", (int)n);
      bool ok = vert && frag &&
                render_readback(c, what, stages, 2, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, 3, &readback);
      uint32_t wrong = 0;
      const uint32_t blue = 0xffff0000u;
      const uint32_t *const texels = readback.map;
      for (uint32_t y = 0; ok && y < TARGET_SIZE; y++) {
         for (uint32_t x = 0; x < TARGET_SIZE; x++) {
            const double edge = (x + 0.5) / 128.0 + (y + 0.5) / 256.0;
            if (edge > 0.995 && edge < 1.005)
               continue;
            uint32_t sum = 0;
            for (int32_t i = 0; i < n; i++)
               sum += (uint32_t)((i * 7 + (int32_t)x) & 15);
            const uint32_t expected =
               edge < 1.0 ? (sum & 0xffu) | (((sum >> 8) & 0xffu) << 8) | 0xff000000u : blue;
            if (texels[y * TARGET_SIZE + x] != expected) {
               if (wrong < 4)
                  report("%s: texel (%u, %u) reads 0x%08x, not 0x%08x", what, x, y, texels[y * TARGET_SIZE + x],
                         expected);
               wrong++;
            }
         }
      }
      if (wrong)
         report("%s: %u texels wrong", what, wrong);
      char description[96];
      snprintf(description, sizeof(description), "%s: every texel has its array's sum", what);
      check(ok && wrong == 0, description);
   }
   vkDestroyShaderModule(c->device, vert, NULL);
   vkDestroyShaderModule(c->device, frag, NULL);
   buffer_destroy(c, &readback);
}

/* test_triangle's triangle, whose vertices name themselves 1, 2 and 3; the
 * fragment shader writes the three values it reads per vertex as its red,
 * green and blue bytes. With the first vertex provoking, vertex_id[i] is
 * vertex i's. */
static void
test_barycentric(struct context *c)
{
   struct buffer readback;
   if (!buffer_create(c, TARGET_SIZE * TARGET_SIZE * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback)) {
      check(false, "barycentric: readback buffer");
      return;
   }
   VkShaderModule vert = shader(c, radv_smoke_bary_vert, sizeof(radv_smoke_bary_vert));
   VkShaderModule frag = shader(c, radv_smoke_bary_frag, sizeof(radv_smoke_bary_frag));
   for (uint32_t winding = 0; winding < 2; winding++) {
   const VkBool32 other_winding = winding;
   const VkSpecializationMapEntry entry = {.constantID = 0, .offset = 0, .size = sizeof(other_winding)};
   const VkSpecializationInfo specialisation = {
      .mapEntryCount = 1, .pMapEntries = &entry, .dataSize = sizeof(other_winding), .pData = &other_winding};
   VkPipelineShaderStageCreateInfo stages[2] = {
      stage_info(VK_SHADER_STAGE_VERTEX_BIT, vert),
      stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, frag),
   };
   stages[0].pSpecializationInfo = &specialisation;
   const char *const what = winding ? "barycentric, other winding" : "barycentric";
   bool ok = vert && frag &&
             render_readback(c, what, stages, 2, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, 3, &readback);
   uint32_t wrong = 0;
   const uint32_t expected_inside = 0xff030201u, blue = 0xffff0000u;
   const uint32_t *const texels = readback.map;
   for (uint32_t y = 0; ok && y < TARGET_SIZE; y++) {
      for (uint32_t x = 0; x < TARGET_SIZE; x++) {
         const double edge = (x + 0.5) / 128.0 + (y + 0.5) / 256.0;
         if (edge > 0.995 && edge < 1.005)
            continue;
         const uint32_t expected = edge < 1.0 ? expected_inside : blue;
         if (texels[y * TARGET_SIZE + x] != expected) {
            if (wrong < 4)
               report("%s: texel (%u, %u) reads 0x%08x, not 0x%08x", what, x, y, texels[y * TARGET_SIZE + x],
                      expected);
            wrong++;
         }
      }
   }
   if (wrong)
      report("%s: %u texels wrong", what, wrong);
   char description[128];
   snprintf(description, sizeof(description), "%s: each vertex's value read per vertex, in the triangle's vertex order",
            what);
   check(ok && wrong == 0, description);
   }
   vkDestroyShaderModule(c->device, vert, NULL);
   vkDestroyShaderModule(c->device, frag, NULL);
   buffer_destroy(c, &readback);
}

/* dEQP-VK.fragment_shading_barycentric's triangle list: two triangles over
 * the whole target, split along the diagonal, the right-hand corners at
 * w = 16; vertex i names itself i + 1 and the fragment shader writes the
 * three values it reads per vertex as its red, green and blue bytes. */
static void
test_barycentric_pair(struct context *c)
{
   struct buffer readback;
   if (!buffer_create(c, TARGET_SIZE * TARGET_SIZE * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback)) {
      check(false, "barycentric pair: readback buffer");
      return;
   }
   VkShaderModule vert = shader(c, radv_smoke_bary6_vert, sizeof(radv_smoke_bary6_vert));
   VkShaderModule frag = shader(c, radv_smoke_bary_frag, sizeof(radv_smoke_bary_frag));
   const VkPipelineShaderStageCreateInfo stages[2] = {
      stage_info(VK_SHADER_STAGE_VERTEX_BIT, vert),
      stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, frag),
   };
   bool ok = vert && frag &&
             render_readback(c, "barycentric pair", stages, 2, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, 6, &readback);
   uint32_t wrong = 0;
   const uint32_t *const texels = readback.map;
   for (uint32_t y = 0; ok && y < TARGET_SIZE; y++) {
      for (uint32_t x = 0; x < TARGET_SIZE; x++) {
         if (x == y || x + 1 == y || y + 1 == x)
            continue;
         /* Below the diagonal: the first triangle (vertices 1, 2, 3). */
         const uint32_t expected = x < y ? 0xff030201u : 0xff060504u;
         if (texels[y * TARGET_SIZE + x] != expected) {
            if (wrong < 4)
               report("barycentric pair: texel (%u, %u) reads 0x%08x, not 0x%08x", x, y,
                      texels[y * TARGET_SIZE + x], expected);
            wrong++;
         }
      }
   }
   if (wrong)
      report("barycentric pair: %u texels wrong", wrong);
   check(ok && wrong == 0, "barycentric pair: each triangle's values read per vertex, in its vertex order");
   vkDestroyShaderModule(c->device, vert, NULL);
   vkDestroyShaderModule(c->device, frag, NULL);
   buffer_destroy(c, &readback);
}

/* A pipeline fragment shading rate over a triangle covering the target: at
 * 2x2 the fragment shader runs once for each 2x2 block of texels and reports
 * the rate (gl_ShadingRateEXT 5, 2 pixels each way), at 1x1 once for each
 * texel. Every texel is written either way. */
static void
test_shading_rate(struct context *c)
{
   struct buffer readback, counts;
   if (!buffer_create(c, TARGET_SIZE * TARGET_SIZE * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback)) {
      check(false, "shading rate: readback buffer");
      return;
   }
   if (!buffer_create(c, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &counts)) {
      check(false, "shading rate: count buffer");
      buffer_destroy(c, &readback);
      return;
   }
   VkShaderModule vert = shader(c, radv_smoke_full_vert, sizeof(radv_smoke_full_vert));
   VkShaderModule frag = shader(c, radv_smoke_rate_frag, sizeof(radv_smoke_rate_frag));
   const VkPipelineShaderStageCreateInfo stages[2] = {
      stage_info(VK_SHADER_STAGE_VERTEX_BIT, vert),
      stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, frag),
   };
   static const struct {
      uint32_t size, rate;
   } rates[2] = {{2, 5}, {1, 0}};
   for (unsigned r = 0; r < 2; r++) {
      const uint32_t size = rates[r].size;
      const VkPipelineFragmentShadingRateStateCreateInfoKHR rate_state = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR,
         .fragmentSize = {size, size},
         .combinerOps = {VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR},
      };
      char what[64];
      snprintf(what, sizeof(what), "shading rate %ux%u", size, size);
      memset(counts.map, 0, 4096);
      const bool ok = vert && frag &&
                      render_readback_with(c, what, stages, 2, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, 3, &readback,
                                           counts.buffer, &rate_state);
      const uint32_t *const texels = readback.map;
      const uint32_t *const words = counts.map;
      unsigned wrong = 0;
      for (unsigned i = 0; ok && i < TARGET_SIZE * TARGET_SIZE; i++)
         wrong += texels[i] != 0xff0000ffu;
      const uint32_t expected = TARGET_SIZE * TARGET_SIZE / (size * size);
      report("%s: %u invocations (%u expected), rates seen 0x%x (0x%x expected), %u texels wrong", what,
             ok ? words[0] : 0, expected, ok ? words[1] : 0, 1u << rates[r].rate, wrong);
      char description[128];
      snprintf(description, sizeof(description),
               "%s: the fragment shader runs once for each %ux%u block and reports the rate", what, size, size);
      check(ok && wrong == 0 && words[0] == expected && words[1] == 1u << rates[r].rate, description);
   }
   vkDestroyShaderModule(c->device, vert, NULL);
   vkDestroyShaderModule(c->device, frag, NULL);
   buffer_destroy(c, &counts);
   buffer_destroy(c, &readback);
}

static void *
thread_mxcsr(void *out)
{
   unsigned mxcsr;
   __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
   *(unsigned *)out = mxcsr;
   return NULL;
}

/* The title's startup sets the IEEE state (the console starts a title with
 * flush-to-zero and denormals-are-zero), and a new thread starts with its
 * creator's: exception flags aside, MXCSR reads 0x1f80 in both. */
static void
test_fp_state(void)
{
   unsigned mxcsr = 0, thread_value = 0;
   __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
   pthread_t thread;
   const bool made = pthread_create(&thread, NULL, thread_mxcsr, &thread_value) == 0 &&
                     pthread_join(thread, NULL) == 0;
   report("fp state: main 0x%x, a new thread 0x%x", mxcsr, thread_value);
   check((mxcsr & ~0x3fu) == 0x1f80 && made && (thread_value & ~0x3fu) == 0x1f80,
         "fp state: main and a new thread run with IEEE denormals (MXCSR 0x1f80)");
}

/* One quad patch over the whole target, its evaluation positions taken from
 * the tessellation coordinates alone or from the control points the control
 * shader wrote (which travel through the off-chip ring). */
static void
test_tessellation(struct context *c)
{
   struct buffer readback;
   if (!buffer_create(c, TARGET_SIZE * TARGET_SIZE * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback)) {
      check(false, "tessellation: readback buffer");
      return;
   }
   VkShaderModule vert = shader(c, radv_smoke_tess_vert, sizeof(radv_smoke_tess_vert));
   VkShaderModule tesc = shader(c, radv_smoke_tess_tesc, sizeof(radv_smoke_tess_tesc));
   VkShaderModule from_coord = shader(c, radv_smoke_tess_coord_tese, sizeof(radv_smoke_tess_coord_tese));
   VkShaderModule from_patch = shader(c, radv_smoke_tess_patch_tese, sizeof(radv_smoke_tess_patch_tese));
   VkShaderModule frag = shader(c, radv_smoke_frag, sizeof(radv_smoke_frag));
   const struct {
      const char *what;
      VkShaderModule tese;
   } variants[2] = {
      {"tessellation from coordinates", from_coord},
      {"tessellation from control points", from_patch},
   };
   for (unsigned v = 0; v < 2; v++) {
      const VkPipelineShaderStageCreateInfo stages[4] = {
         stage_info(VK_SHADER_STAGE_VERTEX_BIT, vert),
         stage_info(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, tesc),
         stage_info(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, variants[v].tese),
         stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, frag),
      };
      const bool ok = vert && tesc && variants[v].tese && frag &&
                      render_readback(c, variants[v].what, stages, 4, VK_PRIMITIVE_TOPOLOGY_PATCH_LIST, 4, 4,
                                      &readback);
      uint32_t red = 0;
      const uint32_t *const texels = readback.map;
      for (uint32_t i = 0; ok && i < TARGET_SIZE * TARGET_SIZE; i++)
         red += texels[i] == 0xff0000ffu;
      char label[96];
      snprintf(label, sizeof(label), "%s: the patch covers the target (%u of %u texels)", variants[v].what, red,
               TARGET_SIZE * TARGET_SIZE);
      check(ok && red == TARGET_SIZE * TARGET_SIZE, label);
   }

   /* The CTS's shape: generic control points, nine-by-nine, and a colour from
    * the evaluation shader. */
   VkShaderModule varying_vert = shader(c, radv_smoke_tess_varying_vert, sizeof(radv_smoke_tess_varying_vert));
   VkShaderModule varying_tesc = shader(c, radv_smoke_tess_varying_tesc, sizeof(radv_smoke_tess_varying_tesc));
   VkShaderModule varying_tese = shader(c, radv_smoke_tess_varying_tese, sizeof(radv_smoke_tess_varying_tese));
   VkShaderModule colour_frag = shader(c, radv_smoke_colour_frag, sizeof(radv_smoke_colour_frag));
   const struct {
      const char *what;
      VkShaderModule tesc, frag;
   } shapes[1] = {
      {"tessellation with varyings", varying_tesc, colour_frag},
   };
   for (unsigned v = 0; v < 1; v++) {
      const VkPipelineShaderStageCreateInfo stages[4] = {
         stage_info(VK_SHADER_STAGE_VERTEX_BIT, varying_vert),
         stage_info(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, shapes[v].tesc),
         stage_info(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, varying_tese),
         stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, shapes[v].frag),
      };
      const char *const what = shapes[v].what;
      const bool ok = varying_vert && shapes[v].tesc && varying_tese && shapes[v].frag &&
                      render_readback(c, what, stages, 4, VK_PRIMITIVE_TOPOLOGY_PATCH_LIST, 4, 4, &readback);
      uint32_t red = 0, blue = 0;
      const uint32_t *const texels = readback.map;
      for (uint32_t i = 0; ok && i < TARGET_SIZE * TARGET_SIZE; i++) {
         red += texels[i] == 0xff0000ffu;
         blue += texels[i] == 0xffff0000u;
      }
      if (ok && red != TARGET_SIZE * TARGET_SIZE)
         report("%s: %u red, %u blue, texel (128, 128) reads 0x%08x", what, red, blue,
                texels[128 * TARGET_SIZE + 128]);
      char label[96];
      snprintf(label, sizeof(label), "%s: the patch covers the target in its colour", what);
      check(ok && red == TARGET_SIZE * TARGET_SIZE, label);
   }
   /* Levels 2 to 9: NGG culling once dropped every patch above level 2. */
   VkShaderModule level_tesc = shader(c, radv_smoke_tess_level_tesc, sizeof(radv_smoke_tess_level_tesc));
   uint32_t levels_covered = 0;
   for (unsigned level = 2; level <= 9; level++) {
      const float value = (float)level;
      const VkSpecializationMapEntry entry = {.constantID = 0, .offset = 0, .size = sizeof(float)};
      const VkSpecializationInfo special = {
         .mapEntryCount = 1, .pMapEntries = &entry, .dataSize = sizeof(float), .pData = &value};
      VkPipelineShaderStageCreateInfo stages[4] = {
         stage_info(VK_SHADER_STAGE_VERTEX_BIT, varying_vert),
         stage_info(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, level_tesc),
         stage_info(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, varying_tese),
         stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, frag),
      };
      stages[1].pSpecializationInfo = &special;
      const bool ok =
         render_readback(c, "tessellation levels", stages, 4, VK_PRIMITIVE_TOPOLOGY_PATCH_LIST, 4, 4, &readback);
      uint32_t red = 0;
      const uint32_t *const texels = readback.map;
      for (uint32_t i = 0; ok && i < TARGET_SIZE * TARGET_SIZE; i++)
         red += texels[i] == 0xff0000ffu;
      if (red == TARGET_SIZE * TARGET_SIZE)
         levels_covered++;
      else
         report("tessellation level %u: %u of %u texels red", level, red, TARGET_SIZE * TARGET_SIZE);
   }
   check(levels_covered == 8, "tessellation levels 2 to 9: each patch covers the target");
   vkDestroyShaderModule(c->device, level_tesc, NULL);
   vkDestroyShaderModule(c->device, varying_vert, NULL);
   vkDestroyShaderModule(c->device, varying_tesc, NULL);
   vkDestroyShaderModule(c->device, varying_tese, NULL);
   vkDestroyShaderModule(c->device, colour_frag, NULL);
   vkDestroyShaderModule(c->device, vert, NULL);
   vkDestroyShaderModule(c->device, tesc, NULL);
   vkDestroyShaderModule(c->device, from_coord, NULL);
   vkDestroyShaderModule(c->device, from_patch, NULL);
   vkDestroyShaderModule(c->device, frag, NULL);
   buffer_destroy(c, &readback);
}

/* Points drawn into strips by geometry shaders: every output size from 16 to
 * 128 vertices at 1, 2 and 4 points, a vertex count chosen from
 * gl_PrimitiveIDIn, a colour varying, and the IDs the invocations get.
 * Counts that are not constant go through NGG's workgroup repack. */
static void
test_geometry(struct context *c)
{
   struct buffer readback;
   if (!buffer_create(c, TARGET_SIZE * TARGET_SIZE * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback)) {
      check(false, "geometry: readback buffer");
      return;
   }
   enum { ROW = TARGET_SIZE * TARGET_SIZE / 4 };
   VkShaderModule vert = shader(c, radv_smoke_gs_points_vert, sizeof(radv_smoke_gs_points_vert));
   VkShaderModule frag = shader(c, radv_smoke_frag, sizeof(radv_smoke_frag));

   /* Draws count points with the geometry shader and counts red texels. */
   #define DRAW_POINTS(vs, gs, fs, count, red)                                                                     \
      do {                                                                                                     \
         const VkPipelineShaderStageCreateInfo stages_[3] = {                                                   \
            stage_info(VK_SHADER_STAGE_VERTEX_BIT, vs),                                                         \
            stage_info(VK_SHADER_STAGE_GEOMETRY_BIT, gs),                                                       \
            stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, fs),                                                       \
         };                                                                                                     \
         const bool ok_ = render_readback(c, "geometry", stages_, 3, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, 0, count, \
                                          &readback);                                                           \
         const uint32_t *const texels_ = readback.map;                                                          \
         red = 0;                                                                                               \
         for (uint32_t i_ = 0; ok_ && i_ < TARGET_SIZE * TARGET_SIZE; i_++)                                     \
            red += texels_[i_] == 0xff0000ffu;                                                                  \
      } while (0)

   const struct {
      unsigned vertices;
      const uint32_t *code;
      size_t bytes;
   } shapes[5] = {
      {16, radv_smoke_gs_16_geom, sizeof(radv_smoke_gs_16_geom)},
      {32, radv_smoke_gs_32_geom, sizeof(radv_smoke_gs_32_geom)},
      {64, radv_smoke_gs_64_geom, sizeof(radv_smoke_gs_64_geom)},
      {100, radv_smoke_gs_100_geom, sizeof(radv_smoke_gs_100_geom)},
      {128, radv_smoke_gs_128_geom, sizeof(radv_smoke_gs_128_geom)},
   };
   unsigned sizes_right = 0;
   for (unsigned s = 0; s < 5; s++) {
      VkShaderModule geom = shader(c, shapes[s].code, shapes[s].bytes);
      const unsigned counts[3] = {1, 2, 4};
      for (unsigned n = 0; n < 3; n++) {
         uint32_t red;
         DRAW_POINTS(vert, geom, frag, counts[n], red);
         if (red == counts[n] * ROW)
            sizes_right++;
         else
            report("geometry: %u vertices from %u points drew %u texels, not %u", shapes[s].vertices, counts[n], red,
                   counts[n] * ROW);
      }
      vkDestroyShaderModule(c->device, geom, NULL);
   }
   check(sizes_right == 15, "geometry: strips of 16 to 128 vertices from 1, 2 and 4 points cover their rows");

   VkShaderModule by_id = shader(c, radv_smoke_gs_primid_geom, sizeof(radv_smoke_gs_primid_geom));
   VkShaderModule reads_id = shader(c, radv_smoke_gs_primid_fixed_geom, sizeof(radv_smoke_gs_primid_fixed_geom));
   uint32_t one, two, three, four;
   DRAW_POINTS(vert, by_id, frag, 1, one);
   DRAW_POINTS(vert, by_id, frag, 2, two);
   DRAW_POINTS(vert, reads_id, frag, 1, three);
   DRAW_POINTS(vert, reads_id, frag, 2, four);
   if (one != ROW || two != 2 * ROW)
      report("geometry: a count from gl_PrimitiveIDIn drew %u and %u texels", one, two);
   check(one == ROW && two == 2 * ROW, "geometry: a vertex count chosen from gl_PrimitiveIDIn draws every strip");
   check(three == ROW && four == 2 * ROW, "geometry: a shader that may return early draws every strip");
   vkDestroyShaderModule(c->device, by_id, NULL);
   vkDestroyShaderModule(c->device, reads_id, NULL);

   VkShaderModule colour_vert = shader(c, radv_smoke_gs_colour_vert, sizeof(radv_smoke_gs_colour_vert));
   VkShaderModule colour_geom = shader(c, radv_smoke_gs_colour_geom, sizeof(radv_smoke_gs_colour_geom));
   VkShaderModule colour_frag = shader(c, radv_smoke_colour_frag, sizeof(radv_smoke_colour_frag));
   uint32_t coloured;
   DRAW_POINTS(colour_vert, colour_geom, colour_frag, 2, coloured);
   check(coloured == 2 * ROW, "geometry: a colour passes from the vertex shader to the fragment shader");
   vkDestroyShaderModule(c->device, colour_vert, NULL);
   vkDestroyShaderModule(c->device, colour_geom, NULL);
   vkDestroyShaderModule(c->device, colour_frag, NULL);
   #undef DRAW_POINTS

   /* The IDs four points' invocations record. */
   struct buffer record;
   VkShaderModule record_geom = shader(c, radv_smoke_gs_record_geom, sizeof(radv_smoke_gs_record_geom));
   bool ids_right = false;
   if (record_geom && buffer_create(c, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &record)) {
      memset(record.map, 0, 4096);
      const VkPipelineShaderStageCreateInfo stages[3] = {
         stage_info(VK_SHADER_STAGE_VERTEX_BIT, vert),
         stage_info(VK_SHADER_STAGE_GEOMETRY_BIT, record_geom),
         stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, frag),
      };
      const bool ok = render_readback_with(c, "geometry record", stages, 3, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, 0, 4,
                                           &readback, record.buffer, NULL);
      const uint32_t *const words = record.map;
      unsigned seen = 0;
      for (unsigned i = 0; ok && words[0] == 4 && i < 4; i++) {
         if (words[1 + 2 * i] < 4 && words[2 + 2 * i] == 0)
            seen |= 1u << words[1 + 2 * i];
      }
      ids_right = seen == 0xf;
      if (!ids_right)
         report("geometry: %u invocations recorded (%u %u) (%u %u) (%u %u) (%u %u)", words[0], words[1], words[2],
                words[3], words[4], words[5], words[6], words[7], words[8]);
      buffer_destroy(c, &record);
   }
   check(ids_right, "geometry: four points' invocations get primitive IDs 0 to 3");
   vkDestroyShaderModule(c->device, record_geom, NULL);
   vkDestroyShaderModule(c->device, vert, NULL);
   vkDestroyShaderModule(c->device, frag, NULL);
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
   VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentric = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR,
      .fragmentShaderBarycentric = VK_TRUE,
   };
   VkPhysicalDeviceFragmentShadingRateFeaturesKHR shading_rate = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR,
      .pipelineFragmentShadingRate = VK_TRUE,
   };
   VkPhysicalDeviceVulkan13Features features13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .synchronization2 = VK_TRUE,
      .dynamicRendering = VK_TRUE,
   };
   const char *device_extensions[2];
   uint32_t enabled_extensions = 0;
   VkExtensionProperties extensions[512];
   uint32_t extension_count = sizeof(extensions) / sizeof(extensions[0]);
   c->barycentric = false;
   c->shading_rate = false;
   if (vkEnumerateDeviceExtensionProperties(c->physical, NULL, &extension_count, extensions) >= VK_SUCCESS) {
      for (uint32_t i = 0; i < extension_count; i++) {
         c->barycentric |= strcmp(extensions[i].extensionName, VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME) == 0;
         c->shading_rate |= strcmp(extensions[i].extensionName, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) == 0;
      }
   }
   if (c->barycentric) {
      device_extensions[enabled_extensions++] = VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME;
      barycentric.pNext = features13.pNext;
      features13.pNext = &barycentric;
   }
   if (c->shading_rate) {
      device_extensions[enabled_extensions++] = VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME;
      shading_rate.pNext = features13.pNext;
      features13.pNext = &shading_rate;
   }
   const VkPhysicalDeviceFeatures features = {
      .geometryShader = VK_TRUE,
      .tessellationShader = VK_TRUE,
      .vertexPipelineStoresAndAtomics = VK_TRUE,
      .fragmentStoresAndAtomics = VK_TRUE,
   };
   const VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features13,
      .pEnabledFeatures = &features,
      .enabledExtensionCount = enabled_extensions,
      .ppEnabledExtensionNames = device_extensions,
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
   const int captured = ps5_klog_capture_stderr("[radv-smoke:stderr] ");
   /* Optional NAME=VALUE lines for the driver's environment (RADV_DEBUG,
    * ACO_DEBUG), read before the driver is. */
   FILE *const env = fopen("/app0/radv-smoke-env.txt", "r");
   if (env) {
      char line[256];
      while (fgets(line, sizeof(line), env)) {
         line[strcspn(line, "\r\n")] = '\0';
         char *const equals = strchr(line, '=');
         if (equals && line[0] != '#') {
            *equals = '\0';
            setenv(line, equals + 1, 1);
         }
      }
      fclose(env);
   }
   results = fopen(RESULTS_PATH, "w");
   report("RADV smoke test starts");
   /* The line below arrives in klog prefixed "[radv-smoke:stderr]" when the
    * capture works; the driver's own messages take the same way. */
   report("standard error to klog: %s", captured == 0 ? "captured" : "not captured");
   fprintf(stderr, "standard error reaches klog\n");
  test_fp_state();
  struct context c = {0};
   if (context_create(&c)) {
      test_fill(&c);
      test_copy(&c);
      test_compute(&c);
      test_triangle(&c);
      test_tessellation(&c);
      test_geometry(&c);
      test_scratch(&c);
      if (c.barycentric) {
         test_barycentric(&c);
         test_barycentric_pair(&c);
      } else {
         report("barycentric: VK_KHR_fragment_shader_barycentric is not reported; its checks are skipped");
      }
      if (c.shading_rate)
         test_shading_rate(&c);
      else
         report("shading rate: VK_KHR_fragment_shading_rate is not reported; its checks are skipped");
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
