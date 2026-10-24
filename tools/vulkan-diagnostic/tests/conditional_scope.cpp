// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal Vulkan scope test. Invalid command buffers are NEVER submitted.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned errors{};
unsigned scope_errors{};
VKAPI_ATTR VkBool32 VKAPI_CALL Message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++errors;
        if (data->pMessageIdName && (std::strstr(data->pMessageIdName, "01986") ||
                                  std::strstr(data->pMessageIdName, "01987"))) ++scope_errors;
        std::printf("  validation: %s\n", data->pMessageIdName ? data->pMessageIdName : data->pMessage);
    }
    return VK_FALSE;
}
void Check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan call failed: " + std::to_string(result));
}
}

int main() try {
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const char* instance_extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "NXEmu conditional rendering scope reproduction";
    application.apiVersion = VK_API_VERSION_1_1;
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    debug.pfnUserCallback = Message;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pNext = &debug;
    instance_info.pApplicationInfo = &application;
    instance_info.enabledLayerCount = 1;
    instance_info.ppEnabledLayerNames = &layer;
    instance_info.enabledExtensionCount = 1;
    instance_info.ppEnabledExtensionNames = &instance_extension;
    VkInstance instance;
    Check(vkCreateInstance(&instance_info, nullptr, &instance));
    auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    VkDebugUtilsMessengerEXT messenger;
    Check(create_debug(instance, &debug, nullptr, &messenger));
    uint32_t count;
    Check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    std::vector<VkPhysicalDevice> physicals(count);
    Check(vkEnumeratePhysicalDevices(instance, &count, physicals.data()));
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t family = 0;
    for (auto candidate : physicals) {
        VkPhysicalDeviceConditionalRenderingFeaturesEXT conditional{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &conditional;
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (!conditional.conditionalRendering) continue;
        uint32_t families;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, queues.data());
        for (uint32_t i = 0; i < families; ++i) {
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { physical = candidate; family = i; break; }
        }
        if (physical) break;
    }
    if (!physical) throw std::runtime_error("Conditional rendering not supported");
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    std::printf("Device: %s\n", properties.deviceName);
    float priority = 1;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkPhysicalDeviceConditionalRenderingFeaturesEXT conditional{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT};
    conditional.conditionalRendering = VK_TRUE;
    const char* device_extension = VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext = &conditional;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = &device_extension;
    VkDevice device;
    Check(vkCreateDevice(physical, &device_info, nullptr, &device));
    auto begin_conditional = reinterpret_cast<PFN_vkCmdBeginConditionalRenderingEXT>(vkGetDeviceProcAddr(device, "vkCmdBeginConditionalRenderingEXT"));
    auto end_conditional = reinterpret_cast<PFN_vkCmdEndConditionalRenderingEXT>(vkGetDeviceProcAddr(device, "vkCmdEndConditionalRenderingEXT"));
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = 4;
    buffer_info.usage = VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
    VkBuffer buffer;
    Check(vkCreateBuffer(device, &buffer_info, nullptr, &buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    uint32_t type = ~0U;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        constexpr auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((requirements.memoryTypeBits & (1U << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & flags) == flags) { type = i; break; }
    }
    if (type == ~0U) throw std::runtime_error("No coherent predicate memory");
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    VkDeviceMemory memory;
    Check(vkAllocateMemory(device, &allocate, nullptr, &memory));
    Check(vkBindBufferMemory(device, buffer, memory, 0));
    void* mapped;
    Check(vkMapMemory(device, memory, 0, 4, 0, &mapped));
    *static_cast<uint32_t*>(mapped) = 1;
    vkUnmapMemory(device, memory);
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkRenderPassCreateInfo pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &subpass;
    VkRenderPass pass;
    Check(vkCreateRenderPass(device, &pass_info, nullptr, &pass));
    VkFramebufferCreateInfo framebuffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer_info.renderPass = pass;
    framebuffer_info.width = framebuffer_info.height = framebuffer_info.layers = 1;
    VkFramebuffer framebuffer;
    Check(vkCreateFramebuffer(device, &framebuffer_info, nullptr, &framebuffer));
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = family;
    VkCommandPool pool;
    Check(vkCreateCommandPool(device, &pool_info, nullptr, &pool));
    VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1;
    VkCommandBuffer command;
    Check(vkAllocateCommandBuffers(device, &command_info, &command));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    bool passed = errors == 0;
    for (unsigned test = 0; test < 4; ++test) {
        const bool fixed = test >= 2;
        const bool begins_inside = (test % 2) != 0;
        errors = scope_errors = 0;
        Check(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Check(vkBeginCommandBuffer(command, &begin));
        VkConditionalRenderingBeginInfoEXT predicate{VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT};
        predicate.buffer = buffer;
        VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        render.renderPass = pass;
        render.framebuffer = framebuffer;
        render.renderArea.extent = {1, 1};
        const char* names[] = {"original outside -> inside (invalid)",
                               "inside -> inside (legal control)",
                               "fixed outside -> outside",
                               "fixed predicate change closes current pass first"};
        std::printf("Case %u: %s\n", test, names[test]);
        if (!begins_inside) {
            begin_conditional(command, &predicate);
            vkCmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
            if (fixed) vkCmdEndRenderPass(command);
            end_conditional(command);
            if (!fixed) vkCmdEndRenderPass(command);
        } else {
            vkCmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
            if (fixed) vkCmdEndRenderPass(command);
            begin_conditional(command, &predicate);
            end_conditional(command);
            if (!fixed) vkCmdEndRenderPass(command);
        }
        Check(vkEndCommandBuffer(command));
        if (test != 0 && errors == 0) {
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            VkFence fence;
            Check(vkCreateFence(device, &fence_info, nullptr, &fence));
            Check(vkQueueSubmit(queue, 1, &submit, fence));
            Check(vkWaitForFences(device, 1, &fence, VK_TRUE, 2000000000ULL));
            vkDestroyFence(device, fence, nullptr);
        }
        const bool ok = test == 0 ? scope_errors > 0 : errors == 0;
        passed &= ok;
        std::printf("  %s errors=%u scope_errors=%u submitted=%s\n", ok ? "PASS" : "FAIL",
                    errors, scope_errors, test != 0 && errors == 0 ? "yes" : "no");
    }
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyRenderPass(device, pass, nullptr);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDevice(device, nullptr);
    destroy_debug(instance, messenger, nullptr);
    vkDestroyInstance(instance, nullptr);
    std::puts(passed ? "ALL PASS" : "FAILED");
    return passed ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 2;
}
