// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yuzu_common/math_util.h"
#include "yuzu_video_core/vulkan_common/vulkan_memory_allocator.h"
#include "yuzu_video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
class Device;
class Scheduler;
// Reconstruct once, including a one-output-pixel halo consumed by the CAS cross.
// Per-swapchain images/descriptor sets are reused after Layer's resource tick wait.
class CatmullPass {
  public:
    CatmullPass(const Device &device, MemoryAllocator &allocator, size_t image_count,
                VkExtent2D output_extent);
    VkImageView Draw(Scheduler &scheduler, size_t image_index, VkImage source,
                     VkImageView source_view, const Common::Rectangle<f32> &crop, bool fast);
    Common::Rectangle<f32> OutputCrop() const;

  private:
    const Device &device;
    VkExtent2D extent;
    vk::Sampler sampler;
    vk::DescriptorSetLayout descriptor_layout;
    vk::DescriptorPool descriptor_pool;
    vk::PipelineLayout pipeline_layout;
    vk::ShaderModule vertex, fragment;
    vk::RenderPass renderpass;
    vk::Pipeline pipeline;
    struct Image {
        vk::Image image;
        vk::ImageView view;
        vk::Framebuffer framebuffer;
        vk::DescriptorSets sets;
        bool initialized{};
    };
    std::vector<Image> images;
};
} // namespace Vulkan
