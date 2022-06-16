/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define Render of the GLTF Model
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#pragma once

#include <vulkan/vulkan.h>

#include "camera.hpp"
#include "vulkan/buffer.h"
#include "vulkan/device.h"
#include "model.h"
#include "textures.h"

namespace xy
{

    class GLTFRender
    {
    private:
        enum PBRWorkflows{ PBR_WORKFLOW_METALLIC_ROUGHNESS = 0, PBR_WORKFLOW_SPECULAR_GLOSINESS = 1 };

        struct UniformBufferSet {
            Buffer scene;
        };

        struct UBOMatrices {
            glm::mat4 projection;
            glm::mat4 model;
            glm::mat4 view;
            glm::vec3 camPos;
        } shaderValuesScene;

        struct PushConstBlockMaterial {
            glm::vec4 baseColorFactor;
            glm::vec4 emissiveFactor;
            glm::vec4 diffuseFactor;
            glm::vec4 specularFactor;
            float workflow;
            int colorTextureSet;
            int PhysicalDescriptorTextureSet;
            int normalTextureSet;
            int occlusionTextureSet;
            int emissiveTextureSet;
            float metallicFactor;
            float roughnessFactor;
            float alphaMask;
            float alphaMaskCutoff;
        } pushConstBlockMaterial;

        struct Pipelines {
            VkPipeline pbr{VK_NULL_HANDLE};
            VkPipeline pbrAlphaBlend{VK_NULL_HANDLE};
        } pipelines;

        struct DescriptorSetLayouts {
            VkDescriptorSetLayout scene{VK_NULL_HANDLE};
            VkDescriptorSetLayout material{VK_NULL_HANDLE};
            VkDescriptorSetLayout node{VK_NULL_HANDLE};
        } descriptorSetLayouts;

        struct DescriptorSets {
            VkDescriptorSet scene;
        };
        std::vector<DescriptorSets>     descriptorSets;
        std::vector<UniformBufferSet>   uniformBuffers;
        std::vector<Buffer> *uniformBufferParams;

        vkglTF::Model       scene;
        Camera             *camera;
        VulkanDevice       *vulkanDevice;
        Textures           *textures;

        VkDevice              device;
        VkQueue               queue;
        VkRenderPass          renderPass;
        VkPipelineLayout      pipelineLayout{VK_NULL_HANDLE};
        VkDescriptorPool      descriptorPool{VK_NULL_HANDLE};
        VkPipelineCache       pipelineCache;
        VkSampleCountFlagBits multiSampleCount;

        uint32_t    frameBufferCount = 3;
        float       animationTimer   = 0.0f;
        bool        animate          = true;

        void prepareUniformBuffers();
        void setupNodeDescriptorSet(vkglTF::Node *node);
        void renderNode(VkCommandBuffer currentCB, vkglTF::Node *node, uint32_t cbIndex, vkglTF::Material::AlphaMode alphaMode);

    public:

        std::string name;
        int32_t     animationIndex   = 0;
        bool                loaded{false};

        glm::vec3 location = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 rotation = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 scale    = glm::vec3(0.3f, 0.3f, 0.3f);

        GLTFRender(VulkanDevice *vulkanDevice, uint32_t frameBufferCount, VkRenderPass renderPass,
            VkQueue queue, VkPipelineCache pipelineCache, VkSampleCountFlagBits multiSampleCount, Textures *textures,
            Camera *camera, std::vector<Buffer> *uniformBufferParams);
        ~GLTFRender();
        void updateUniformBuffers(uint32_t cbIndex);
        bool load(std::string uri);
        vkglTF::Model *getModel();
        void setupDescriptors();
        void preparePipelines();
        void recordCommandBuffers(VkCommandBuffer currentCB, uint32_t frameIndex);
        void render(float time);
    };

} //namespace xy