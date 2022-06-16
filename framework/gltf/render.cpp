/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define Render of the GLTF Model
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#include "gltf/render.h"

#include "logger.h"
#include "gltf/model.h"
#include "vulkan/utils.h"
#include "vulkan/macros.h"

namespace xy
{

    GLTFRender::GLTFRender(VulkanDevice *vulkanDevice, uint32_t frameBufferCount, VkRenderPass renderPass,
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

        prepareUniformBuffers();
    }

    GLTFRender::~GLTFRender()
    {
        vkDestroyPipeline(device, pipelines.pbr, nullptr);
        vkDestroyPipeline(device, pipelines.pbrAlphaBlend, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.node, nullptr);

        scene.destroy(device);

        LOGI("Model: {}", scene.name);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);

        for (auto buffer : uniformBuffers) {
            buffer.scene.destroy();
        }

        uniformBuffers.resize(0);
        descriptorSets.resize(0);
    }

    bool GLTFRender::load(std::string uri)
    {
        LOGI("Loading scene from {}", uri);
        scene.destroy(device);
        animationIndex = 0;
        animationTimer = 0.0f;
        scene.loadFromFile(uri, vulkanDevice, queue);

        size_t left   = uri.find_last_of("/\\");
        size_t extpos   = uri.find_last_of(".");
        name = uri.substr(left + 1, extpos - left - 1);
        LOGI("Model Name: {}", name);

        // Calculating the location
        location = glm::vec3(0.0f, 0.0f, 0.0f);

        // Calculating the scale
        scale = glm::vec3(1.0f, 1.0f, 1.0f);

        loaded = true;
        return true;
    }

    /* 
        Prepare and initialize uniform buffers containing shader parameters
    */
    void GLTFRender::prepareUniformBuffers()
    {
        for (auto &uniformBuffer : uniformBuffers) {
            uniformBuffer.scene.create(vulkanDevice, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(shaderValuesScene));
        }
    }

    void GLTFRender::updateUniformBuffers(uint32_t cbIndex)
    {
        // Scene
        shaderValuesScene.projection = camera->matrices.perspective;
        shaderValuesScene.view = camera->matrices.view;
        
        // Calculating the location, rotation and scale
        shaderValuesScene.model = glm::mat4(1.0f);
        shaderValuesScene.model = glm::translate(shaderValuesScene.model, location);
        if(rotation.x != 0.0f) {
            shaderValuesScene.model = glm::rotate(shaderValuesScene.model, rotation.x, glm::vec3(1, 0, 0));
        }
        if(rotation.y != 0.0f) {
            shaderValuesScene.model = glm::rotate(shaderValuesScene.model, rotation.y, glm::vec3(0, 1, 0));
        }
        if(rotation.z != 0.0f) {
            shaderValuesScene.model = glm::rotate(shaderValuesScene.model, rotation.z, glm::vec3(0, 0, 1));
        }
        shaderValuesScene.model = glm::scale(shaderValuesScene.model, scale);

        shaderValuesScene.camPos = glm::vec3(
            -camera->position.z * sin(glm::radians(camera->rotation.y)) * cos(glm::radians(camera->rotation.x)),
            -camera->position.z * sin(glm::radians(camera->rotation.x)),
            camera->position.z * cos(glm::radians(camera->rotation.y)) * cos(glm::radians(camera->rotation.x))
        );

        memcpy(uniformBuffers[cbIndex].scene.mapped, &shaderValuesScene, sizeof(shaderValuesScene));
    }

    void GLTFRender::setupDescriptors()
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

        for (auto &material : scene.materials) {
            imageSamplerCount += 5;
            materialCount++;
        }
        for (auto node : scene.linearNodes) {
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
        LOGI("Create DescriptorPool : [{}, {}]", poolSizes.at(0).descriptorCount, poolSizes.at(1).descriptorCount);

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
            LOGI("Create DescriptorSetLayout : size({})", setLayoutBindings.size());

            for (auto i = 0; i < descriptorSets.size(); i++) {

                VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
                descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                descriptorSetAllocInfo.descriptorPool = descriptorPool;
                descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.scene;
                descriptorSetAllocInfo.descriptorSetCount = 1;
                VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets[i].scene));
                LOGI("Allocate DescriptorSets : descriptorSets[{}].scene", i);

                std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};

                writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writeDescriptorSets[0].descriptorCount = 1;
                writeDescriptorSets[0].dstSet = descriptorSets[i].scene;
                writeDescriptorSets[0].dstBinding = 0;
                writeDescriptorSets[0].pBufferInfo = &uniformBuffers[i].scene.descriptor;

                writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writeDescriptorSets[1].descriptorCount = 1;
                writeDescriptorSets[1].dstSet = descriptorSets[i].scene;
                writeDescriptorSets[1].dstBinding = 1;
                writeDescriptorSets[1].pBufferInfo = &(*uniformBufferParams)[i].descriptor;

                writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writeDescriptorSets[2].descriptorCount = 1;
                writeDescriptorSets[2].dstSet = descriptorSets[i].scene;
                writeDescriptorSets[2].dstBinding = 2;
                writeDescriptorSets[2].pImageInfo = &textures->irradianceCube.descriptor;

                writeDescriptorSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeDescriptorSets[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writeDescriptorSets[3].descriptorCount = 1;
                writeDescriptorSets[3].dstSet = descriptorSets[i].scene;
                writeDescriptorSets[3].dstBinding = 3;
                writeDescriptorSets[3].pImageInfo = &textures->prefilteredCube.descriptor;

                writeDescriptorSets[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeDescriptorSets[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writeDescriptorSets[4].descriptorCount = 1;
                writeDescriptorSets[4].dstSet = descriptorSets[i].scene;
                writeDescriptorSets[4].dstBinding = 4;
                writeDescriptorSets[4].pImageInfo = &textures->lutBrdf.descriptor;

                vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
                LOGI("Update DescriptorSets : size[{}]", writeDescriptorSets.size());
            }
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

            // Per-Material descriptor sets
            for (auto &material : scene.materials) {
                VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
                descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                descriptorSetAllocInfo.descriptorPool = descriptorPool;
                descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.material;
                descriptorSetAllocInfo.descriptorSetCount = 1;
                VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &material.descriptorSet));

                std::vector<VkDescriptorImageInfo> imageDescriptors = {
                    textures->empty.descriptor,
                    textures->empty.descriptor,
                    material.normalTexture ? material.normalTexture->descriptor : textures->empty.descriptor,
                    material.occlusionTexture ? material.occlusionTexture->descriptor : textures->empty.descriptor,
                    material.emissiveTexture ? material.emissiveTexture->descriptor : textures->empty.descriptor
                };

                // TODO: glTF specs states that metallic roughness should be preferred, even if specular glosiness is present

                if (material.pbrWorkflows.metallicRoughness) {
                    if (material.baseColorTexture) {
                        imageDescriptors[0] = material.baseColorTexture->descriptor;
                    }
                    if (material.metallicRoughnessTexture) {
                        imageDescriptors[1] = material.metallicRoughnessTexture->descriptor;
                    }
                }

                if (material.pbrWorkflows.specularGlossiness) {
                    if (material.extension.diffuseTexture) {
                        imageDescriptors[0] = material.extension.diffuseTexture->descriptor;
                    }
                    if (material.extension.specularGlossinessTexture) {
                        imageDescriptors[1] = material.extension.specularGlossinessTexture->descriptor;
                    }
                }

                std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};
                for (size_t i = 0; i < imageDescriptors.size(); i++) {
                    writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writeDescriptorSets[i].descriptorCount = 1;
                    writeDescriptorSets[i].dstSet = material.descriptorSet;
                    writeDescriptorSets[i].dstBinding = static_cast<uint32_t>(i);
                    writeDescriptorSets[i].pImageInfo = &imageDescriptors[i];
                }

                vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
            }

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

                // Per-Node descriptor set
                for (auto &node : scene.nodes) {
                    setupNodeDescriptorSet(node);
                }
            }

        }
    }

    void GLTFRender::setupNodeDescriptorSet(vkglTF::Node *node) {
        LOGI("Setup Node DescriptorSet : {}", node->name);
        if (node->mesh) {
            VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
            descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocInfo.descriptorPool = descriptorPool;
            descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.node;
            descriptorSetAllocInfo.descriptorSetCount = 1;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &node->mesh->uniformBuffer.descriptorSet));

            VkWriteDescriptorSet writeDescriptorSet{};
            writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeDescriptorSet.descriptorCount = 1;
            writeDescriptorSet.dstSet = node->mesh->uniformBuffer.descriptorSet;
            writeDescriptorSet.dstBinding = 0;
            writeDescriptorSet.pBufferInfo = &node->mesh->uniformBuffer.descriptor;

            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
        }
        for (auto& child : node->children) {
            setupNodeDescriptorSet(child);
        }
    }

    void GLTFRender::preparePipelines()
    {
        if(pipelines.pbr) {
            vkDestroyPipeline(device, pipelines.pbr, nullptr);
        }
        if(pipelines.pbrAlphaBlend) {
            vkDestroyPipeline(device, pipelines.pbrAlphaBlend, nullptr);
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

        if (multiSampleCount > VK_SAMPLE_COUNT_1_BIT) {
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
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.size = sizeof(PushConstBlockMaterial);
        pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

        // Vertex bindings an attributes
        VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
        std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 },
            { 2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6 },
            { 3, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 8 },
            { 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 10 },
            { 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 14 }
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

        if (multiSampleCount > VK_SAMPLE_COUNT_1_BIT) {
            multisampleStateCI.rasterizationSamples = multiSampleCount;
        }

        // PBR pipeline
        shaderStages = {
            loadShader(device, "pbr.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(device, "pbr_khr.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        depthStencilStateCI.depthWriteEnable = VK_TRUE;
        depthStencilStateCI.depthTestEnable = VK_TRUE;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbr));

        rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbrAlphaBlend));

        for (auto shaderStage : shaderStages) {
            vkDestroyShaderModule(device, shaderStage.module, nullptr);
        }
    }

    void GLTFRender::recordCommandBuffers(VkCommandBuffer currentCB, uint32_t frameIndex)
    {
        vkCmdBindDescriptorSets(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[frameIndex].scene, 0, nullptr);
        vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbr);

        vkglTF::Model &model = scene;
        VkDeviceSize offsets[1] = { 0 };
        vkCmdBindVertexBuffers(currentCB, 0, 1, &model.vertices.buffer, offsets);
        if (model.indices.buffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(currentCB, model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        }

        // Opaque primitives first
        for (auto node : model.nodes) {
            renderNode(currentCB, node, frameIndex, vkglTF::Material::ALPHAMODE_OPAQUE);
        }
        // Alpha masked primitives
        for (auto node : model.nodes) {
            renderNode(currentCB, node, frameIndex, vkglTF::Material::ALPHAMODE_MASK);
        }
        // Transparent primitives
        // TODO: Correct depth sorting
        vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbrAlphaBlend);
        for (auto node : model.nodes) {
            renderNode(currentCB, node, frameIndex, vkglTF::Material::ALPHAMODE_BLEND);
        }
    }

    void GLTFRender::renderNode(VkCommandBuffer currentCB, vkglTF::Node *node, uint32_t cbIndex, vkglTF::Material::AlphaMode alphaMode)
    {
        if (node->mesh) {
            // Render mesh primitives
            for (vkglTF::Primitive * primitive : node->mesh->primitives) {
                if (primitive->material.alphaMode == alphaMode) {

                    const std::vector<VkDescriptorSet> descriptorsets = {
                        descriptorSets[cbIndex].scene,
                        primitive->material.descriptorSet,
                        node->mesh->uniformBuffer.descriptorSet,
                    };
                    vkCmdBindDescriptorSets(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
                        static_cast<uint32_t>(descriptorsets.size()), descriptorsets.data(), 0, NULL);

                    // Pass material parameters as push constants
                    PushConstBlockMaterial pushConstBlockMaterial{};					
                    pushConstBlockMaterial.emissiveFactor = primitive->material.emissiveFactor;
                    // To save push constant space, availabilty and texture coordiante set are combined
                    // -1 = texture not used for this material, >= 0 texture used and index of texture coordinate set
                    pushConstBlockMaterial.colorTextureSet = primitive->material.baseColorTexture != nullptr ? primitive->material.texCoordSets.baseColor : -1;
                    pushConstBlockMaterial.normalTextureSet = primitive->material.normalTexture != nullptr ? primitive->material.texCoordSets.normal : -1;
                    pushConstBlockMaterial.occlusionTextureSet = primitive->material.occlusionTexture != nullptr ? primitive->material.texCoordSets.occlusion : -1;
                    pushConstBlockMaterial.emissiveTextureSet = primitive->material.emissiveTexture != nullptr ? primitive->material.texCoordSets.emissive : -1;
                    pushConstBlockMaterial.alphaMask = static_cast<float>(primitive->material.alphaMode == vkglTF::Material::ALPHAMODE_MASK);
                    pushConstBlockMaterial.alphaMaskCutoff = primitive->material.alphaCutoff;

                    // TODO: glTF specs states that metallic roughness should be preferred, even if specular glosiness is present

                    if (primitive->material.pbrWorkflows.metallicRoughness) {
                        // Metallic roughness workflow
                        pushConstBlockMaterial.workflow = static_cast<float>(PBR_WORKFLOW_METALLIC_ROUGHNESS);
                        pushConstBlockMaterial.baseColorFactor = primitive->material.baseColorFactor;
                        pushConstBlockMaterial.metallicFactor = primitive->material.metallicFactor;
                        pushConstBlockMaterial.roughnessFactor = primitive->material.roughnessFactor;
                        pushConstBlockMaterial.PhysicalDescriptorTextureSet = primitive->material.metallicRoughnessTexture != nullptr ?
                            primitive->material.texCoordSets.metallicRoughness : -1;
                        pushConstBlockMaterial.colorTextureSet = primitive->material.baseColorTexture != nullptr ? primitive->material.texCoordSets.baseColor : -1;
                    }

                    if (primitive->material.pbrWorkflows.specularGlossiness) {
                        // Specular glossiness workflow
                        pushConstBlockMaterial.workflow = static_cast<float>(PBR_WORKFLOW_SPECULAR_GLOSINESS);
                        pushConstBlockMaterial.PhysicalDescriptorTextureSet = primitive->material.extension.specularGlossinessTexture != nullptr ?
                            primitive->material.texCoordSets.specularGlossiness : -1;
                        pushConstBlockMaterial.colorTextureSet = primitive->material.extension.diffuseTexture != nullptr ? primitive->material.texCoordSets.baseColor : -1;
                        pushConstBlockMaterial.diffuseFactor = primitive->material.extension.diffuseFactor;
                        pushConstBlockMaterial.specularFactor = glm::vec4(primitive->material.extension.specularFactor, 1.0f);
                    }

                    vkCmdPushConstants(currentCB, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstBlockMaterial), &pushConstBlockMaterial);

                    if (primitive->hasIndices) {
                        vkCmdDrawIndexed(currentCB, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
                    } else {
                        vkCmdDraw(currentCB, primitive->vertexCount, 1, 0, 0);
                    }
                }
            }

        };
        for (auto child : node->children) {
            renderNode(currentCB, child, cbIndex, alphaMode);
        }
    }

    vkglTF::Model* GLTFRender::getModel()
    {
        return &scene;
    }

    void GLTFRender::render(float time)
    {
        if (animationIndex > 0) {
            animate = true;
        } else {
            animate = false;
        }
        if(loaded && (scene.animations.size() > 0) && animate) {
            animationTimer = time;
            int32_t i = animationIndex - 1;
            if(scene.animations[i].end > 0.0f) {
                while (animationTimer > scene.animations[i].end) {
                    animationTimer -= scene.animations[i].end;
                }
                scene.updateAnimation(i, animationTimer);
            }
        }
    }

}  //namespace xy