/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define 2D texture for Vulkan
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#pragma once

#include <string>

#include "vulkan/vulkan.h"
#include "device.h"
#include "texture.h"

namespace xy
{

    class Texture2D : public Texture {
    public:
        void loadFromFile(
            std::string filename, 
            VkFormat format,
            VulkanDevice *device,
            VkQueue copyQueue,
            VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        void loadFromBuffer(
            void* buffer,
            VkDeviceSize bufferSize,
            VkFormat format,
            uint32_t width,
            uint32_t height,
            VulkanDevice *device,
            VkQueue copyQueue,
            VkFilter filter = VK_FILTER_LINEAR,
            VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };

}
