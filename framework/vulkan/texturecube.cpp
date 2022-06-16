/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define Cubemap texture for Vulkan
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#include "texturecube.h"

#include "macros.h"
#include "logger.h"

#include <gli/gli.hpp>

namespace xy
{

    void TextureCubeMap::loadFromFile(
        std::string filename,
        VkFormat format,
        VulkanDevice *device,
        VkQueue copyQueue,
        VkImageUsageFlags imageUsageFlags,
        VkImageLayout imageLayout)
    {
        LOGI("TextureCubeMap[{}] loading...", filename);
        gli::texture_cube texCube(gli::load(filename));
        if (texCube.empty()) {
            LOGE("TextureCubeMap[{}] load error.", filename);
        }
        assert(!texCube.empty());

        if (texCube.format() != gli::FORMAT_RGBA16_SFLOAT_PACK16) {
            texCube = gli::convert(texCube, gli::FORMAT_RGBA16_SFLOAT_PACK16);
        }

        this->device = device;
        width = static_cast<uint32_t>(texCube.extent().x);
        height = static_cast<uint32_t>(texCube.extent().y);
        mipLevels = static_cast<uint32_t>(texCube.levels());

        VkMemoryAllocateInfo memAllocInfo{};
        memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        // Create a host-visible staging buffer that contains the raw image data
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = texCube.size();
        // This buffer is used as a transfer source for the buffer copy
        bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

        // Get memory requirements for the staging buffer (alignment, memory type bits)
        vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);

        memAllocInfo.allocationSize = memReqs.size;
        // Get memory type index for a host visible buffer
        memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
        VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

        // Copy texture data into staging buffer
        uint8_t *data;
        VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
        memcpy(data, texCube.data(), texCube.size());
        vkUnmapMemory(device->logicalDevice, stagingMemory);

        // Setup buffer copy regions for each face including all of it's miplevels
        std::vector<VkBufferImageCopy> bufferCopyRegions;
        size_t offset = 0;

        for (uint32_t face = 0; face < 6; face++) {
            for (uint32_t level = 0; level < mipLevels; level++) {
                VkBufferImageCopy bufferCopyRegion = {};
                bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                bufferCopyRegion.imageSubresource.mipLevel = level;
                bufferCopyRegion.imageSubresource.baseArrayLayer = face;
                bufferCopyRegion.imageSubresource.layerCount = 1;
                bufferCopyRegion.imageExtent.width = static_cast<uint32_t>(texCube[face][level].extent().x);
                bufferCopyRegion.imageExtent.height = static_cast<uint32_t>(texCube[face][level].extent().y);
                bufferCopyRegion.imageExtent.depth = 1;
                bufferCopyRegion.bufferOffset = offset;

                bufferCopyRegions.push_back(bufferCopyRegion);

                // Increase offset into staging buffer for next level / face
                offset += texCube[face][level].size();
            }
        }

        // Create optimal tiled target image
        VkImageCreateInfo imageCreateInfo{};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.mipLevels = mipLevels;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.extent = { width, height, 1 };
        imageCreateInfo.usage = imageUsageFlags;
        // Ensure that the TRANSFER_DST bit is set for staging
        if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        // Cube faces count as array layers in Vulkan
        imageCreateInfo.arrayLayers = 6;
        // This flag is required for cube map images
        imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;


        VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

        vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);

        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

        // Use a separate command buffer for texture loading
        VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        // Image barrier for optimal image (target)
        // Set initial layout for all array layers (faces) of the optimal (target) tiled texture
        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = mipLevels;
        subresourceRange.layerCount = 6;

        {
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemoryBarrier.srcAccessMask = 0;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.image = image;
            imageMemoryBarrier.subresourceRange = subresourceRange;
            vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
        }

        // Copy the cube map faces from the staging buffer to the optimal tiled image
        vkCmdCopyBufferToImage(
            copyCmd,
            stagingBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(bufferCopyRegions.size()),
            bufferCopyRegions.data());

        // Change texture image layout to shader read after all faces have been copied
        this->imageLayout = imageLayout;
        {
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemoryBarrier.newLayout = imageLayout;
            imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            imageMemoryBarrier.image = image;
            imageMemoryBarrier.subresourceRange = subresourceRange;
            vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
        }

        device->flushCommandBuffer(copyCmd, copyQueue);

        // Create sampler
        VkSamplerCreateInfo samplerCreateInfo{};
        samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
        samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
        samplerCreateInfo.mipLodBias = 0.0f;
        samplerCreateInfo.maxAnisotropy = device->enabledFeatures.samplerAnisotropy ?
            device->properties.limits.maxSamplerAnisotropy : 1.0f;
        samplerCreateInfo.anisotropyEnable = device->enabledFeatures.samplerAnisotropy;
        samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
        samplerCreateInfo.minLod = 0.0f;
        samplerCreateInfo.maxLod = (float)mipLevels;
        samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));

        // Create image view
        VkImageViewCreateInfo viewCreateInfo{};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewCreateInfo.format = format;
        viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
            VK_COMPONENT_SWIZZLE_A };
        viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        viewCreateInfo.subresourceRange.layerCount = 6;
        viewCreateInfo.subresourceRange.levelCount = mipLevels;
        viewCreateInfo.image = image;
        VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &view));

        // Clean up staging resources
        vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
        vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

        // Update descriptor image info member that can be used for setting up descriptor sets
        updateDescriptor();
    }

}
