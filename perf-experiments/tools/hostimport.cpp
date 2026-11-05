// Feasibility test: can the NVIDIA Vulkan driver import guest-like memory
// (a pagefile-backed section mapped with MapViewOfFile, like nxemu's backing memory)
// with VK_EXT_external_memory_host, and how fast does the GPU read it?
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s -> %d (line %d)\n", #x, r_, __LINE__); return 1; } } while (0)

static VkDevice dev;
static VkPhysicalDevice phys;
static VkQueue queue;
static uint32_t qfam;
static VkCommandPool pool;
static PFN_vkGetMemoryHostPointerPropertiesEXT pGetHostProps;
static VkPhysicalDeviceMemoryProperties memprops;

static uint32_t FindType(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < memprops.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & want) == want) return i;
    return ~0u;
}

struct Imported { VkDeviceMemory mem{}; VkBuffer buf{}; };

static VkResult Import(void* ptr, VkDeviceSize size, Imported& out, uint32_t& type_bits) {
    VkMemoryHostPointerPropertiesEXT hp{VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    VkResult r = pGetHostProps(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, ptr, &hp);
    if (r != VK_SUCCESS) return r;
    type_bits = hp.memoryTypeBits;
    VkExternalMemoryBufferCreateInfo ext{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    ext.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, &ext};
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    r = vkCreateBuffer(dev, &bci, nullptr, &out.buf);
    if (r != VK_SUCCESS) return r;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, out.buf, &req);
    const uint32_t type = FindType(req.memoryTypeBits & hp.memoryTypeBits, 0);
    if (type == ~0u) return VK_ERROR_FEATURE_NOT_PRESENT;
    VkImportMemoryHostPointerInfoEXT imp{VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT};
    imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    imp.pHostPointer = ptr;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &imp};
    ai.allocationSize = size;
    ai.memoryTypeIndex = type;
    r = vkAllocateMemory(dev, &ai, nullptr, &out.mem);
    if (r != VK_SUCCESS) return r;
    return vkBindBufferMemory(dev, out.buf, out.mem, 0);
}

static double CopyMs(VkBuffer src, VkDeviceSize src_off, VkBuffer dst, VkDeviceSize size, int reps) {
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &cai, &cb);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cb, &bi);
    VkBufferCopy c{src_off, 0, size};
    for (int i = 0; i < reps; ++i) vkCmdCopyBuffer(cb, src, dst, 1, &c);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    auto t0 = std::chrono::high_resolution_clock::now();
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    auto t1 = std::chrono::high_resolution_clock::now();
    vkFreeCommandBuffers(dev, pool, 1, &cb);
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
    VkInstance inst; CHECK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr);
    std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data());
    for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr);
        if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { phys = p; printf("GPU: %s driver %u\n", pr.deviceName, pr.driverVersion); } }
    if (!phys) phys = pds[0];
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT hostp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &hostp};
    vkGetPhysicalDeviceProperties2(phys, &p2);
    printf("minImportedHostPointerAlignment %llu\n", (unsigned long long)hostp.minImportedHostPointerAlignment);
    vkGetPhysicalDeviceMemoryProperties(phys, &memprops);
    uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn); vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf.data());
    for (uint32_t i = 0; i < qn; ++i) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qfam = i; break; }
    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = qfam; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    const char* exts[] = {VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = exts;
    CHECK(vkCreateDevice(phys, &dci, nullptr, &dev));
    vkGetDeviceQueue(dev, qfam, 0, &queue);
    pGetHostProps = (PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(dev, "vkGetMemoryHostPointerPropertiesEXT");
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pci.queueFamilyIndex = qfam;
    CHECK(vkCreateCommandPool(dev, &pci, nullptr, &pool));

    // Device local destination for bandwidth tests
    const VkDeviceSize chunk = 64ull << 20;
    VkBufferCreateInfo dbci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; dbci.size = chunk;
    dbci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer dbuf; CHECK(vkCreateBuffer(dev, &dbci, nullptr, &dbuf));
    VkMemoryRequirements dreq; vkGetBufferMemoryRequirements(dev, dbuf, &dreq);
    VkMemoryAllocateInfo dai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; dai.allocationSize = dreq.size;
    dai.memoryTypeIndex = FindType(dreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory dmem; CHECK(vkAllocateMemory(dev, &dai, nullptr, &dmem)); vkBindBufferMemory(dev, dbuf, dmem, 0);

    // Guest-like backing memory: pagefile-backed section mapped into the process, as in host_memory.cpp
    for (unsigned long long size_gb : {1ull, 4ull, 8ull}) {
        const unsigned long long size = size_gb << 30;
        HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT,
                                        DWORD(size >> 32), DWORD(size), nullptr);
        if (!sec) { printf("%llu GiB: CreateFileMapping failed %lu\n", size_gb, GetLastError()); continue; }
        u_char* base = (u_char*)MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (!base) { printf("%llu GiB: MapViewOfFile failed %lu\n", size_gb, GetLastError()); CloseHandle(sec); continue; }
        Imported im; uint32_t bits = 0;
        auto t0 = std::chrono::high_resolution_clock::now();
        VkResult r = Import(base, size, im, bits);
        auto t1 = std::chrono::high_resolution_clock::now();
        printf("%llu GiB section view: import %s (%d), type bits 0x%x, %.1f ms\n", size_gb,
               r == VK_SUCCESS ? "OK" : "FAILED", r, bits, std::chrono::duration<double, std::milli>(t1 - t0).count());
        if (r == VK_SUCCESS) {
            // CPU writes after import must be visible to the GPU without any flush
            for (VkDeviceSize i = 0; i < chunk; i += 4) *(uint32_t*)(base + size - chunk + i) = uint32_t(i * 2654435761u);
            CopyMs(im.buf, size - chunk, dbuf, chunk, 1);
            // read back through a second imported range
            VkBufferCopy back{0, 0, chunk};
            VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool; cai.commandBufferCount = 1;
            VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &cai, &cb);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cb, &bi);
            vkCmdCopyBuffer(cb, dbuf, im.buf, 1, &back); vkEndCommandBuffer(cb);
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
            vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE); vkQueueWaitIdle(queue);
            const bool ok = std::memcmp(base, base + size - chunk, chunk) == 0;
            printf("  data round trip CPU->GPU->CPU: %s\n", ok ? "OK" : "MISMATCH");
            const int reps = 8;
            const double ms = CopyMs(im.buf, size - chunk, dbuf, chunk, reps);
            printf("  GPU read bandwidth from imported memory: %.1f GB/s\n", (chunk * reps) / (ms * 1e6));
            vkDestroyBuffer(dev, im.buf, nullptr); vkFreeMemory(dev, im.mem, nullptr);
        }
        UnmapViewOfFile(base); CloseHandle(sec);
    }
    // Sub-range import (a 64 MiB window inside a big section), in case whole imports fail
    {
        const unsigned long long size = 4ull << 30;
        HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, DWORD(size >> 32), DWORD(size), nullptr);
        u_char* base = (u_char*)MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, size);
        Imported im; uint32_t bits = 0;
        VkResult r = Import(base + (1ull << 30), chunk, im, bits);
        printf("64 MiB window inside 4 GiB section: import %s (%d)\n", r == VK_SUCCESS ? "OK" : "FAILED", r);
        if (r == VK_SUCCESS) { vkDestroyBuffer(dev, im.buf, nullptr); vkFreeMemory(dev, im.mem, nullptr); }
        UnmapViewOfFile(base); CloseHandle(sec);
    }
    // Reference: device local to device local
    printf("reference VRAM->VRAM copy: %.1f GB/s\n", (chunk * 8.0) / (CopyMs(dbuf, 0, dbuf, chunk / 2, 8) * 1e6) / 2);
    return 0;
}
