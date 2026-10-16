// SPDX-License-Identifier: GPL-2.0-or-later
#include "../descriptor_copy_cache.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <vector>
#include <vulkan/vulkan.h>
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        VkResult r = (expr);                                                                       \
        if (r != VK_SUCCESS) {                                                                     \
            fprintf(stderr, "%s: %d\n", #expr, r);                                                 \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define LOAD(name)                                                                                 \
    auto name = reinterpret_cast<PFN_##name>(dlsym(lib, #name));                                   \
    if (!name)                                                                                     \
    return 2
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /absolute/path/libMoltenVK.dylib [verify.spv]\n", argv[0]);
        return 2;
    }
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        puts(dlerror());
        return 2;
    }
    LOAD(vkCreateInstance);
    LOAD(vkEnumeratePhysicalDevices);
    LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD(vkCreateDevice);
    LOAD(vkGetPhysicalDeviceFeatures2);
    LOAD(vkCreateBuffer);
    LOAD(vkGetBufferMemoryRequirements);
    LOAD(vkGetPhysicalDeviceMemoryProperties);
    LOAD(vkAllocateMemory);
    LOAD(vkBindBufferMemory);
    LOAD(vkCreateDescriptorSetLayout);
    LOAD(vkCreateDescriptorPool);
    LOAD(vkAllocateDescriptorSets);
    LOAD(vkCreateDescriptorUpdateTemplate);
    LOAD(vkUpdateDescriptorSetWithTemplate);
    LOAD(vkUpdateDescriptorSets);
    LOAD(vkDestroyDescriptorUpdateTemplate);
    LOAD(vkDestroyDescriptorPool);
    LOAD(vkDestroyDescriptorSetLayout);
    LOAD(vkDestroyBuffer);
    LOAD(vkFreeMemory);
    LOAD(vkDestroyDevice);
    LOAD(vkDestroyInstance);
    LOAD(vkCreateImage);
    LOAD(vkGetImageMemoryRequirements);
    LOAD(vkBindImageMemory);
    LOAD(vkCreateImageView);
    LOAD(vkDestroyImageView);
    LOAD(vkDestroyImage);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance;
    CHECK(vkCreateInstance(&ici, nullptr, &instance));
    uint32_t n = 1;
    VkPhysicalDevice pd;
    CHECK(vkEnumeratePhysicalDevices(instance, &n, &pd));
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12};
    vkGetPhysicalDeviceFeatures2(pd, &f2);
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> qp(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qp.data());
    uint32_t qi = 0;
    while (!(qp[qi].queueFlags & VK_QUEUE_COMPUTE_BIT))
        qi++;
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qi;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f12;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.pEnabledFeatures = &f2.features;
    VkDevice device;
    CHECK(vkCreateDevice(pd, &dci, nullptr, &device));
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    auto alloc = [&](VkMemoryRequirements req) {
        uint32_t i = 0;
        while (!(req.memoryTypeBits & (1u << i)))
            i++;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = i;
        VkDeviceMemory m;
        CHECK(vkAllocateMemory(device, &ai, nullptr, &m));
        return m;
    };
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = 65536;
    bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer buf;
    CHECK(vkCreateBuffer(device, &bci, nullptr, &buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buf, &req);
    auto bm = alloc(req);
    CHECK(vkBindBufferMemory(device, buf, bm, 0));
    VkImageCreateInfo imci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imci.imageType = VK_IMAGE_TYPE_2D;
    imci.format = VK_FORMAT_R32_UINT;
    imci.extent = {4, 4, 1};
    imci.mipLevels = 1;
    imci.arrayLayers = 1;
    imci.samples = VK_SAMPLE_COUNT_1_BIT;
    imci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage img;
    CHECK(vkCreateImage(device, &imci, nullptr, &img));
    vkGetImageMemoryRequirements(device, img, &req);
    auto im = alloc(req);
    CHECK(vkBindImageMemory(device, img, im, 0));
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = imci.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view;
    CHECK(vkCreateImageView(device, &vci, nullptr, &view));
    if (argc > 2) {
        LOAD(vkCreateShaderModule);
        LOAD(vkCreatePipelineLayout);
        LOAD(vkCreateComputePipelines);
        LOAD(vkCreateCommandPool);
        LOAD(vkAllocateCommandBuffers);
        LOAD(vkBeginCommandBuffer);
        LOAD(vkEndCommandBuffer);
        LOAD(vkCmdBindPipeline);
        LOAD(vkCmdBindDescriptorSets);
        LOAD(vkCmdDispatch);
        LOAD(vkCmdPipelineBarrier);
        LOAD(vkGetDeviceQueue);
        LOAD(vkQueueSubmit);
        LOAD(vkQueueWaitIdle);
        LOAD(vkMapMemory);
        LOAD(vkUnmapMemory);
        LOAD(vkDestroyCommandPool);
        LOAD(vkDestroyPipeline);
        LOAD(vkDestroyPipelineLayout);
        LOAD(vkDestroyShaderModule);
        LOAD(vkResetCommandPool);
        FILE* sf = fopen(argv[2], "rb");
        if (!sf)
            return 4;
        fseek(sf, 0, SEEK_END);
        size_t ssize = ftell(sf);
        rewind(sf);
        std::vector<uint32_t> spv(ssize / 4);
        fread(spv.data(), 1, ssize, sf);
        fclose(sf);
        imci.extent = {8, 8, 1};
        VkImage img2;
        CHECK(vkCreateImage(device, &imci, nullptr, &img2));
        vkGetImageMemoryRequirements(device, img2, &req);
        auto im2 = alloc(req);
        CHECK(vkBindImageMemory(device, img2, im2, 0));
        vci.image = img2;
        VkImageView view2;
        CHECK(vkCreateImageView(device, &vci, nullptr, &view2));
        VkBuffer out;
        CHECK(vkCreateBuffer(device, &bci, nullptr, &out));
        vkGetBufferMemoryRequirements(device, out, &req);
        uint32_t mi = 0;
        while (!(req.memoryTypeBits & (1u << mi)) ||
               (mp.memoryTypes[mi].propertyFlags &
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) !=
                   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            mi++;
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = mi;
        VkDeviceMemory om;
        CHECK(vkAllocateMemory(device, &mai, nullptr, &om));
        CHECK(vkBindBufferMemory(device, out, om, 0));
        uint32_t* mapped;
        CHECK(vkMapMemory(device, om, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&mapped)));
        std::array<VkDescriptorSetLayoutBinding, 2> vbs{
            {{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
             {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}};
        VkDescriptorSetLayoutCreateInfo vlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        vlci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        vlci.bindingCount = 2;
        vlci.pBindings = vbs.data();
        VkDescriptorSetLayout vl;
        CHECK(vkCreateDescriptorSetLayout(device, &vlci, nullptr, &vl));
        std::array<VkDescriptorPoolSize, 2> ps{
            {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 128}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}}};
        VkDescriptorPoolCreateInfo vpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        vpci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        vpci.maxSets = 2;
        vpci.poolSizeCount = 2;
        vpci.pPoolSizes = ps.data();
        VkDescriptorPool vp;
        CHECK(vkCreateDescriptorPool(device, &vpci, nullptr, &vp));
        std::array<VkDescriptorSetLayout, 2> vls{vl, vl};
        VkDescriptorSetAllocateInfo vai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        vai.descriptorPool = vp;
        vai.descriptorSetCount = 2;
        vai.pSetLayouts = vls.data();
        std::array<VkDescriptorSet, 2> vs;
        CHECK(vkAllocateDescriptorSets(device, &vai, vs.data()));
        VkDescriptorUpdateTemplateEntry ve{
            0, 0, 64, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, sizeof(VkDescriptorImageInfo)};
        VkDescriptorUpdateTemplateCreateInfo vtci{
            VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO};
        vtci.descriptorUpdateEntryCount = 1;
        vtci.pDescriptorUpdateEntries = &ve;
        vtci.descriptorSetLayout = vl;
        VkDescriptorUpdateTemplate vt;
        CHECK(vkCreateDescriptorUpdateTemplate(device, &vtci, nullptr, &vt));
        VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smci.codeSize = ssize;
        smci.pCode = spv.data();
        VkShaderModule sm;
        CHECK(vkCreateShaderModule(device, &smci, nullptr, &sm));
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &vl;
        VkPipelineLayout pl;
        CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &pl));
        VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                      nullptr,
                      0,
                      VK_SHADER_STAGE_COMPUTE_BIT,
                      sm,
                      "main",
                      nullptr};
        cpci.layout = pl;
        VkPipeline pipeline;
        CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline));
        VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cpi.queueFamilyIndex = qi;
        VkCommandPool pool;
        CHECK(vkCreateCommandPool(device, &cpi, nullptr, &pool));
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VkCommandBuffer cmd;
        CHECK(vkAllocateCommandBuffers(device, &cai, &cmd));
        VkQueue queue;
        vkGetDeviceQueue(device, qi, 0, &queue);
        std::array<VkDescriptorImageInfo, 64> vimages;
        VkDescriptorBufferInfo outinfo{out, 0, 256};
        VkWriteDescriptorSet ow{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        ow.dstSet = vs[1];
        ow.dstBinding = 1;
        ow.descriptorCount = 1;
        ow.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ow.pBufferInfo = &outinfo;
        vkUpdateDescriptorSets(device, 1, &ow, 0, nullptr);
        for (int test = 0; test < 12; test++) {
            for (int i = 0; i < 64; i++)
                vimages[i] = {VK_NULL_HANDLE, ((i + test) % 3) ? view : view2,
                              VK_IMAGE_LAYOUT_GENERAL};
            vkUpdateDescriptorSetWithTemplate(device, vs[0], vt, vimages.data());
            if (test % 2 == 0) {
                VkCopyDescriptorSet copy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
                copy.srcSet = vs[0];
                copy.dstSet = vs[1];
                copy.descriptorCount = 64;
                vkUpdateDescriptorSets(device, 0, nullptr, 1, &copy);
            } else
                vkUpdateDescriptorSetWithTemplate(device, vs[1], vt, vimages.data());
            CHECK(vkResetCommandPool(device, pool, 0));
            VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            CHECK(vkBeginCommandBuffer(cmd, &cb));
            if (test == 0) {
                std::array<VkImageMemoryBarrier, 2> ib;
                for (int i = 0; i < 2; i++) {
                    ib[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    ib[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    ib[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
                    ib[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    ib[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    ib[i].image = i ? img2 : img;
                    ib[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    ib[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                }
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                                     nullptr, 2, ib.data());
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &vs[1], 0,
                                    nullptr);
            vkCmdDispatch(cmd, 1, 1, 1);
            VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
            CHECK(vkEndCommandBuffer(cmd));
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cmd;
            CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
            CHECK(vkQueueWaitIdle(queue));
            for (int i = 0; i < 64; i++)
                if (mapped[i] != (((i + test) % 3) ? 4u : 8u)) {
                    fprintf(stderr, "GPU mismatch test %d index %d: %u\n", test, i, mapped[i]);
                    return 5;
                }
        }
        fprintf(stderr, "GPU descriptor verification: 12 patterns x 64 images PASSED "
                        "(template/copy, recycled destinations).\n");
        vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pl, nullptr);
        vkDestroyShaderModule(device, sm, nullptr);
        vkDestroyDescriptorUpdateTemplate(device, vt, nullptr);
        vkDestroyDescriptorPool(device, vp, nullptr);
        vkDestroyDescriptorSetLayout(device, vl, nullptr);
        vkUnmapMemory(device, om);
        vkDestroyBuffer(device, out, nullptr);
        vkFreeMemory(device, om, nullptr);
        vkDestroyImageView(device, view2, nullptr);
        vkDestroyImage(device, img2, nullptr);
        vkFreeMemory(device, im2, nullptr);
    }
    puts("type,bindings,array_count,mode,median_ns_per_update");
    for (auto type : {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE})
        for (auto shape :
             std::array<std::array<uint32_t, 2>, 4>{{{16, 1}, {64, 1}, {16, 128}, {1, 2048}}}) {
            uint32_t bindings = shape[0], count = shape[1], total = bindings * count;
            std::vector<VkDescriptorSetLayoutBinding> bs;
            std::vector<VkDescriptorUpdateTemplateEntry> es;
            std::vector<VkDescriptorBufferInfo> bi(total, {buf, 0, 256});
            std::vector<VkDescriptorImageInfo> ii(total,
                                                  {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL});
            size_t stride =
                type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ? sizeof(bi[0]) : sizeof(ii[0]);
            const void* data = type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                   ? static_cast<void*>(bi.data())
                                   : static_cast<void*>(ii.data());
            for (uint32_t i = 0; i < bindings; i++) {
                bs.push_back({i, type, count, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
                es.push_back({i, 0, count, type, i * count * stride, stride});
            }
            VkDescriptorSetLayoutCreateInfo lci{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            lci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            lci.bindingCount = bindings;
            lci.pBindings = bs.data();
            VkDescriptorSetLayout layout;
            CHECK(vkCreateDescriptorSetLayout(device, &lci, nullptr, &layout));
            VkDescriptorPoolSize size{type, total * 2};
            VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            pci.maxSets = 2;
            pci.poolSizeCount = 1;
            pci.pPoolSizes = &size;
            VkDescriptorPool pool;
            CHECK(vkCreateDescriptorPool(device, &pci, nullptr, &pool));
            std::array<VkDescriptorSetLayout, 2> ls{layout, layout};
            VkDescriptorSetAllocateInfo sai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            sai.descriptorPool = pool;
            sai.descriptorSetCount = 2;
            sai.pSetLayouts = ls.data();
            std::array<VkDescriptorSet, 2> sets;
            CHECK(vkAllocateDescriptorSets(device, &sai, sets.data()));
            std::array<VkDescriptorUpdateTemplate, 2> ts;
            for (int m = 0; m < 2; m++) {
                VkDescriptorUpdateTemplateCreateInfo tci{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO};
                tci.descriptorUpdateEntryCount = m ? 1 : bindings;
                tci.pDescriptorUpdateEntries = es.data();
                tci.descriptorSetLayout = layout;
                if (m)
                    es[0].descriptorCount = total;
                CHECK(vkCreateDescriptorUpdateTemplate(device, &tci, nullptr, &ts[m]));
            }
            Vulkan::DescriptorCopyCache<VkDescriptorSet> cache;
            cache.Begin(1);
            cache.Store(type, std::span{static_cast<const std::byte*>(data), total * stride},
                        {sets[0], 0});
            for (int mode = 0; mode < 5; mode++) {
                if (mode == 4) {
                    vkUpdateDescriptorSetWithTemplate(device, sets[0], ts[1], data);
                    cache.Store(type,
                                std::span{static_cast<const std::byte*>(data), total * stride},
                                {sets[0], 0});
                }
                std::vector<double> times;
                int iterations = std::max(2000u, 200000u / total);
                for (int trial = 0; trial < 7; trial++) {
                    auto begin = std::chrono::steady_clock::now();
                    for (int i = 0; i < iterations; i++) {
                        if (mode >= 3) {
                            auto source =
                                mode == 4
                                    ? cache.Find(type,
                                                 std::span{static_cast<const std::byte*>(data),
                                                           total * stride})
                                    : std::optional<
                                          Vulkan::DescriptorCopyCache<VkDescriptorSet>::Source>{
                                          {sets[0], 0}};
                            if (!source)
                                return 6;
                            VkCopyDescriptorSet copy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
                            copy.srcSet = source->set;
                            copy.dstSet = sets[1];
                            copy.descriptorCount = total;
                            vkUpdateDescriptorSets(device, 0, nullptr, 1, &copy);
                        } else if (mode < 2)
                            vkUpdateDescriptorSetWithTemplate(device, sets[i % 2], ts[mode], data);
                        else {
                            for (uint32_t j = 0; j < total; j++)
                                if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                    bi[j].offset = (i % 8) * 256;
                            vkUpdateDescriptorSetWithTemplate(device, sets[i % 2], ts[1], data);
                        }
                    }
                    auto ns = std::chrono::duration<double, std::nano>(
                                  std::chrono::steady_clock::now() - begin)
                                  .count() /
                              iterations;
                    times.push_back(ns);
                }
                std::sort(times.begin(), times.end());
                printf("%d,%u,%u,%s,%.1f\n", type, bindings, count,
                       mode == 0   ? "separate"
                       : mode == 1 ? "coalesced"
                       : mode == 2 ? "changing"
                       : mode == 3 ? "copy"
                                   : "cached_copy",
                       times[3]);
                fflush(stdout);
            }
            for (auto t : ts)
                vkDestroyDescriptorUpdateTemplate(device, t, nullptr);
            vkDestroyDescriptorPool(device, pool, nullptr);
            vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
    vkDestroyImageView(device, view, nullptr);
    vkDestroyImage(device, img, nullptr);
    vkFreeMemory(device, im, nullptr);
    vkDestroyBuffer(device, buf, nullptr);
    vkFreeMemory(device, bm, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
