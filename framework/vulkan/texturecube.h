/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define Cubemap texture for Vulkan
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

    class TextureCubeMap : public Texture {
    public:
        void loadFromFile(
            std::string filename,
            VkFormat format,
            VulkanDevice *device,
            VkQueue copyQueue,
            VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };

}
