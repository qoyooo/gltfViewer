/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Render Skybox
 *
 * Version:
 *      2021.06.08  initial
 *
 */

#pragma once 

#include <vulkan/vulkan.h>

#include "vulkan/device.h"
#include "vulkan/buffer.h"

#include "gltf/textures.h"
#include "gltf/model.h"
#include "camera.hpp"

namespace xy
{

    class SkyboxRender
    {
    private:
        struct UniformBufferSet {
            Buffer skybox;
        };

        struct UBOMatrices {
            glm::mat4 projection;
            glm::mat4 model;
            glm::mat4 view;
            glm::vec3 camPos;
        } shaderValuesSkybox;

        struct Pipelines {
            VkPipeline skybox{VK_NULL_HANDLE};
        } pipelines;

        struct DescriptorSetLayouts {
            VkDescriptorSetLayout scene{VK_NULL_HANDLE};
            VkDescriptorSetLayout material{VK_NULL_HANDLE};
            VkDescriptorSetLayout node{VK_NULL_HANDLE};
        } descriptorSetLayouts;

        struct DescriptorSets {
            VkDescriptorSet skybox;
        };
        std::vector<DescriptorSets>     descriptorSets;
        std::vector<UniformBufferSet>   uniformBuffers;
        std::vector<Buffer>*            uniformBufferParams;

        Camera             *camera{nullptr};
        VulkanDevice  *vulkanDevice{nullptr};
        Textures           *textures{nullptr};

        VkDevice            device;
        VkQueue             queue;
        VkRenderPass        renderPass;
        VkPipelineLayout    pipelineLayout{VK_NULL_HANDLE};
        VkDescriptorPool    descriptorPool{VK_NULL_HANDLE};
        VkPipelineCache     pipelineCache;
        VkSampleCountFlagBits multiSampleCount;

        uint32_t    frameBufferCount = 3;

        void prepareUniformBuffers();

    public:
        struct Models {
            vkglTF::Model skybox;
        } models;

        SkyboxRender(VulkanDevice *vulkanDevice, uint32_t frameBufferCount, VkRenderPass renderPass,
            VkQueue queue, VkPipelineCache pipelineCache, VkSampleCountFlagBits multiSampleCount, Textures *textures,
            Camera *camera, std::vector<Buffer> *uniformBufferParams);
        ~SkyboxRender();
        void updateUniformBuffers(uint32_t cbIndex);
        void setupDescriptors();
        void preparePipelines();
        void recordCommandBuffers(VkCommandBuffer currentCB, uint32_t frameIndex);
    };

} // namespace xy