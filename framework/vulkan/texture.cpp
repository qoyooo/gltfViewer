/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define texture of Vulkan
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#include "texture.h"

#include "macros.h"
#include "logger.h"

namespace xy
{
    void Texture::updateDescriptor()
    {
        descriptor.sampler = sampler;
        descriptor.imageView = view;
        descriptor.imageLayout = imageLayout;
    }

    void Texture::destroy()
    {
        vkDestroyImageView(device->logicalDevice, view, nullptr);
        vkDestroyImage(device->logicalDevice, image, nullptr);
        if (sampler)
        {
            vkDestroySampler(device->logicalDevice, sampler, nullptr);
        }
        vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
    }

}
