/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define texture for Vulkan
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#pragma once

#include <string>

#include "vulkan/vulkan.h"
#include "device.h"

namespace xy
{

    class Texture {
    public:
        VulkanDevice *device;
        VkImage image = VK_NULL_HANDLE;
        VkImageLayout imageLayout;
        VkDeviceMemory deviceMemory;
        VkImageView view;
        uint32_t width, height;
        uint32_t mipLevels;
        uint32_t layerCount;
        VkDescriptorImageInfo descriptor;
        VkSampler sampler;

        void updateDescriptor();

        void destroy();
    };

}
