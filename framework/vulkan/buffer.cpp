/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define Buffer of Vulkan
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#include "buffer.h"

#include "macros.h"

namespace xy
{

    void Buffer::create(VulkanDevice *device, VkBufferUsageFlags usageFlags,
        VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, bool map)
    {
        this->device = device->logicalDevice;
        this->size = size;
        device->createBuffer(usageFlags, memoryPropertyFlags, size, &buffer, &memory);
        descriptor = { buffer, 0, size };
        if (map) {
            VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, memory, 0, size, 0, &mapped));
        }
    }

    void Buffer::destroy()
    {
        if (mapped) {
            unmap();
        }
        vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, memory, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    }

    void Buffer::map()
    {
        VK_CHECK_RESULT(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    }

    void Buffer::unmap()
    {
        if (mapped) {
            vkUnmapMemory(device, memory);
            mapped = nullptr;
        }
    }

    void Buffer::flush()
    {
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.size = size;
        VK_CHECK_RESULT(vkFlushMappedMemoryRanges(device, 1, &mappedRange));
    }

} //namespace xy