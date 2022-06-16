/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define Buffer of Vulkan
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#pragma once

#include <string>
#include <vulkan/vulkan.h>

#include "vulkan/device.h"

namespace xy
{

    /*
        Vulkan buffer object
    */
    struct Buffer {
        VkDevice device;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize size = VK_WHOLE_SIZE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDescriptorBufferInfo descriptor;
        int32_t count = 0;
        void *mapped = nullptr;

        void create(VulkanDevice *device, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags,
            VkDeviceSize size, bool map = true);

        void destroy();

        void map();

        void unmap();

        void flush();
    };

}  //namespace xy