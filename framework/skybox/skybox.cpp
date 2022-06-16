/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Render Skybox
 *
 * Version:
 *      2021.06.08  initial
 *
 */

#include "skybox.h"

#include "vulkan/utils.h"
#include "vulkan/macros.h"

namespace xy
{

    SkyboxRender::SkyboxRender(VulkanDevice *vulkanDevice, uint32_t frameBufferCount, VkRenderPass renderPass,
            VkQueue queue, VkPipelineCache pipelineCache, VkSampleCountFlagBits multiSampleCount, Textures *textures,
            Camera *camera, std::vector<Buffer> *uniformBufferParams)
    {
        this->vulkanDevice          = vulkanDevice;
        this->device                = vulkanDevice->logicalDevice;
        this->frameBufferCount      = frameBufferCount;
        this->renderPass            = renderPass;
        this->queue                 = queue;
        this->pipelineCache         = pipelineCache;
        this->multiSampleCount      = multiSampleCount;
        this->textures              = textures;
        this->camera                = camera;
        this->uniformBufferParams   = uniformBufferParams;

        uniformBuffers.resize(frameBufferCount);
        descriptorSets.resize(frameBufferCount);

        this->models.skybox.loadFromFile("./../data/models/Box/glTF-Embedded/Box.gltf", vulkanDevice, queue);

        prepareUniformBuffers();
    }

    SkyboxRender::~SkyboxRender()
    {
        vkDestroyPipeline(device, pipelines.skybox, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.node, nullptr);

        models.skybox.destroy(device);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);

        for (auto buffer : uniformBuffers) {
            buffer.skybox.destroy();
        }

        uniformBuffers.resize(0);
        descriptorSets.resize(0);
    }

    void SkyboxRender::prepareUniformBuffers()
    {
        for (auto &uniformBuffer : uniformBuffers) {
            uniformBuffer.skybox.create(vulkanDevice, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(shaderValuesSkybox));
        }
    }

    void SkyboxRender::updateUniformBuffers(uint32_t cbIndex)
    {
        shaderValuesSkybox.projection = camera->matrices.perspective;
        shaderValuesSkybox.view       = camera->matrices.view;
        shaderValuesSkybox.model      = glm::mat4(glm::mat3(camera->matrices.view));
        memcpy(uniformBuffers[cbIndex].skybox.mapped, &shaderValuesSkybox, sizeof(shaderValuesSkybox));
    }

    void SkyboxRender::setupDescriptors()
    {
        if(descriptorPool) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        }

        /*
            Descriptor Pool
        */
        uint32_t imageSamplerCount = 0;
        uint32_t materialCount = 0;
        uint32_t meshCount = 0;

        // Environment samplers (radiance, irradiance, brdf lut)
        imageSamplerCount += 3;

        for (auto &material : models.skybox.materials) {
            imageSamplerCount += 5;
            materialCount++;
        }
        for (auto node : models.skybox.linearNodes) {
            if (node->mesh) {
                meshCount++;
            }
        }

        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (4 + meshCount) * frameBufferCount },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageSamplerCount * frameBufferCount }
        };
        VkDescriptorPoolCreateInfo descriptorPoolCI{};
        descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCI.poolSizeCount = 2;
        descriptorPoolCI.pPoolSizes = poolSizes.data();
        descriptorPoolCI.maxSets = (2 + materialCount + meshCount) * frameBufferCount;
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

        /*
            Descriptor sets
        */

        // Scene (matrices and environment maps)
        {
            if(descriptorSetLayouts.scene) {
                vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
            }
            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            };
            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
            descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
            descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.scene));
        }

        // Material (samplers)
        {
            if(descriptorSetLayouts.material) {
                vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material, nullptr);
            }
            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            };
            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
            descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
            descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.material));

            // Model node (matrices)
            {
                if(descriptorSetLayouts.node) {
                    vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.node, nullptr);
                }
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
                };
                VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
                descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
                descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
                VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.node));
            }

        }

        // Skybox (fixed set)
        for (auto i = 0; i < uniformBuffers.size(); i++) {
            VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
            descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocInfo.descriptorPool = descriptorPool;
            descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.scene;
            descriptorSetAllocInfo.descriptorSetCount = 1;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets[i].skybox));

            std::array<VkWriteDescriptorSet, 3> writeDescriptorSets{};

            writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeDescriptorSets[0].descriptorCount = 1;
            writeDescriptorSets[0].dstSet = descriptorSets[i].skybox;
            writeDescriptorSets[0].dstBinding = 0;
            writeDescriptorSets[0].pBufferInfo = &uniformBuffers[i].skybox.descriptor;

            writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeDescriptorSets[1].descriptorCount = 1;
            writeDescriptorSets[1].dstSet = descriptorSets[i].skybox;
            writeDescriptorSets[1].dstBinding = 1;
            writeDescriptorSets[1].pBufferInfo = &(*uniformBufferParams)[i].descriptor;

            writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writeDescriptorSets[2].descriptorCount = 1;
            writeDescriptorSets[2].dstSet = descriptorSets[i].skybox;
            writeDescriptorSets[2].dstBinding = 2;
            writeDescriptorSets[2].pImageInfo = &textures->prefilteredCube.descriptor;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
        }
    }

    void SkyboxRender::preparePipelines()
    {
        if(pipelines.skybox) {
            vkDestroyPipeline(device, pipelines.skybox, nullptr);
        }
        if(pipelineLayout) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        }
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
        inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
        rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationStateCI.lineWidth = 1.0f;

        VkPipelineColorBlendAttachmentState blendAttachmentState{};
        blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachmentState.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
        colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendStateCI.attachmentCount = 1;
        colorBlendStateCI.pAttachments = &blendAttachmentState;

        VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
        depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilStateCI.depthTestEnable = VK_FALSE;
        depthStencilStateCI.depthWriteEnable = VK_FALSE;
        depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilStateCI.front = depthStencilStateCI.back;
        depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineViewportStateCreateInfo viewportStateCI{};
        viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateCI.viewportCount = 1;
        viewportStateCI.scissorCount = 1;

        VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
        multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

        if (multiSampleCount > 0) {
            multisampleStateCI.rasterizationSamples = multiSampleCount;
        }

        std::vector<VkDynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicStateCI{};
        dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
        dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

        // Pipeline layout
        const std::vector<VkDescriptorSetLayout> setLayouts = {
            descriptorSetLayouts.scene, descriptorSetLayouts.material, descriptorSetLayouts.node
        };
        VkPipelineLayoutCreateInfo pipelineLayoutCI{};
        pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCI.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutCI.pSetLayouts = setLayouts.data();
        // VkPushConstantRange pushConstantRange{};
        // pushConstantRange.size = 0;     //sizeof(PushConstBlockMaterial);
        // pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pipelineLayoutCI.pushConstantRangeCount = 0;        //1;
        pipelineLayoutCI.pPushConstantRanges = nullptr;     //&pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

        // Vertex bindings an attributes
        VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
        std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 },
            { 2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6 },
        };
        VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
        vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputStateCI.vertexBindingDescriptionCount = 1;
        vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

        // Pipelines
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        VkGraphicsPipelineCreateInfo pipelineCI{};
        pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCI.layout = pipelineLayout;
        pipelineCI.renderPass = renderPass;
        pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCI.pVertexInputState = &vertexInputStateCI;
        pipelineCI.pRasterizationState = &rasterizationStateCI;
        pipelineCI.pColorBlendState = &colorBlendStateCI;
        pipelineCI.pMultisampleState = &multisampleStateCI;
        pipelineCI.pViewportState = &viewportStateCI;
        pipelineCI.pDepthStencilState = &depthStencilStateCI;
        pipelineCI.pDynamicState = &dynamicStateCI;
        pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCI.pStages = shaderStages.data();

        if (multiSampleCount > 0) {
            multisampleStateCI.rasterizationSamples = multiSampleCount;
        }

        // Skybox pipeline (background cube)
        shaderStages = {
            loadShader(device, "skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(device, "skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skybox));

        for (auto shaderStage : shaderStages) {
            vkDestroyShaderModule(device, shaderStage.module, nullptr);
        }
    }

    void SkyboxRender::recordCommandBuffers(VkCommandBuffer currentCB, uint32_t frameIndex)
    {
        vkCmdBindDescriptorSets(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[frameIndex].skybox, 0, nullptr);
        vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skybox);
        models.skybox.draw(currentCB);
    }

} // namespace xy