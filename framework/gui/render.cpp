/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      UI Render by imgui
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#include "render.h"
#include "IconsMaterialDesignIcons.h"

#include "vulkan/macros.h"
#include "vulkan/utils.h"
#include "GLFW/glfw3.h"

namespace xy
{

    const uint32_t MAX_SUPPORT_IMAGE_NUM = 32;

    UIRender::UIRender(VulkanDevice *vulkanDevice, VkRenderPass renderPass, VkQueue queue,
        VkPipelineCache pipelineCache, VkSampleCountFlagBits multiSampleCount)
    {
        this->vulkanDevice = vulkanDevice;
        this->device = vulkanDevice->logicalDevice;

        ImGui::CreateContext();

        /*
            Init ImGui Key code
        */
        initImguiKeyCode();

        /*
            Setup ImGui style
        */
        initImguiStyle();

        /*
            Font texture loading
        */
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* fontData;
        int texWidth, texHeight;

        io.Fonts->AddFontFromFileTTF("./../data/DroidSansFallback.ttf", 16.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
        AddIconFont();
        io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
        fontTexture.loadFromBuffer(fontData, texWidth * texHeight * 4 * sizeof(char), VK_FORMAT_R8G8B8A8_UNORM, texWidth, texHeight, vulkanDevice, queue);



        /*
            Descriptor pool
        */
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_SUPPORT_IMAGE_NUM }
        };
        VkDescriptorPoolCreateInfo descriptorPoolCI{};
        descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCI.poolSizeCount = 1;
        descriptorPoolCI.pPoolSizes = poolSizes.data();
        descriptorPoolCI.maxSets = MAX_SUPPORT_IMAGE_NUM;
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

        /*
            Descriptor set layout
        */
        VkDescriptorSetLayoutBinding setLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
        descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCI.pBindings = &setLayoutBinding;
        descriptorSetLayoutCI.bindingCount = 1;
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout));

        /*
            Descriptor set
        */
        VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
        descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocInfo.descriptorPool = descriptorPool;
        descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSetAllocInfo.descriptorSetCount = 1;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSet));
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.pImageInfo = &fontTexture.descriptor;
        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

        /*
            Pipeline layout
        */
        VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock) };

        VkPipelineLayoutCreateInfo pipelineLayoutCI{};
        pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
        pipelineLayoutCI.setLayoutCount = 1;
        pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

        /*
            Pipeline
        */
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
        inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
        rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
        rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationStateCI.lineWidth = 1.0f;

        VkPipelineColorBlendAttachmentState blendAttachmentState{};
        blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

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

        VkVertexInputBindingDescription vertexInputBinding = { 0, 20, VK_VERTEX_INPUT_RATE_VERTEX };
        std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2 },
            { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 4 },
        };
        VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
        vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputStateCI.vertexBindingDescriptionCount = 1;
        vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

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

        pipelineCI.layout = pipelineLayout;
        shaderStages = {
            loadShader(device, "ui.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(device, "ui.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));

        for (auto shaderStage : shaderStages) {
            vkDestroyShaderModule(device, shaderStage.module, nullptr);
        }
    }

    void UIRender::AddIconFont()
    {
        ImGuiIO& io = ImGui::GetIO();

        static const ImWchar icons_ranges[] = { ICON_MIN_MDI, ICON_MAX_MDI, 0 };
        ImFontConfig icons_config;
        // merge in icons from Font Awesome
        icons_config.MergeMode = true;
        icons_config.PixelSnapH = true;
        icons_config.GlyphOffset.y = 1.0f;
        icons_config.OversampleH = icons_config.OversampleV = 1;
        icons_config.PixelSnapH = true;
        icons_config.SizePixels = 13.0f * 1.0f;

        io.Fonts->AddFontFromMemoryCompressedTTF(MaterialDesign_compressed_data, MaterialDesign_compressed_size, 16.0f, &icons_config, icons_ranges);
    }

    void UIRender::initImguiKeyCode()
    {
        // Setup backend capabilities flags
        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
        io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)

        // Keyboard mapping. Dear ImGui will use those indices to peek into the io.KeysDown[] array.
        io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;
        io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
        io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
        io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
        io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
        io.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
        io.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
        io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
        io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
        io.KeyMap[ImGuiKey_Insert] = GLFW_KEY_INSERT;
        io.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
        io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
        io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
        io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
        io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
        io.KeyMap[ImGuiKey_KeyPadEnter] = GLFW_KEY_KP_ENTER;
        io.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
        io.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
        io.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
        io.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
        io.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
        io.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;
    }

    void UIRender::initImguiStyle()
    {
        ImGui::StyleColorsDark();
        // Borderless window
        ImGuiStyle &style = ImGui::GetStyle();
        style.FrameBorderSize = 1.0f;
        style.WindowBorderSize = 1.0f;
    }

    UIRender::~UIRender() {
        ImGui::DestroyContext();
        vertexBuffer.destroy();
        indexBuffer.destroy();
        fontTexture.destroy();

        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }

    void UIRender::draw(VkCommandBuffer cmdBuffer) {
        ImDrawData* imDrawData = ImGui::GetDrawData();

        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
        int fb_width = (int)(imDrawData->DisplaySize.x * imDrawData->FramebufferScale.x);
        int fb_height = (int)(imDrawData->DisplaySize.y * imDrawData->FramebufferScale.y);
        if (fb_width <= 0 || fb_height <= 0)
            return;

        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        const VkDeviceSize offsets[1] = { 0 };
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer.buffer, offsets);
        vkCmdBindIndexBuffer(cmdBuffer, indexBuffer.buffer, 0, sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

        // Setup viewport:
        {
            VkViewport viewport;
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = (float)fb_width;
            viewport.height = (float)fb_height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
        }

        // Setup scale and translation:
        // Our visible imgui space lies from draw_data->DisplayPps (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right).
        // DisplayPos is (0,0) for single viewport apps.
        /*
        {
            float scale[2];
            scale[0] = 2.0f / draw_data->DisplaySize.x;
            scale[1] = 2.0f / draw_data->DisplaySize.y;
            float translate[2];
            translate[0] = -1.0f - draw_data->DisplayPos.x * scale[0];
            translate[1] = -1.0f - draw_data->DisplayPos.y * scale[1];
            vkCmdPushConstants(command_buffer, g_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 0, sizeof(float) * 2, scale);
            vkCmdPushConstants(command_buffer, g_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2, sizeof(float) * 2, translate);
        }*/
        vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UIRender::PushConstBlock), &pushConstBlock);

        // Will project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_off = imDrawData->DisplayPos;         // (0,0) unless using multi-viewports
        ImVec2 clip_scale = imDrawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

        int32_t vertexOffset = 0;
        int32_t indexOffset = 0;
        for (int32_t j = 0; j < imDrawData->CmdListsCount; j++) {
            const ImDrawList* cmd_list = imDrawData->CmdLists[j];
            for (int32_t k = 0; k < cmd_list->CmdBuffer.Size; k++) {
                const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[k];

                // Project scissor/clipping rectangles into framebuffer space
                ImVec4 clip_rect;
                clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
                clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
                clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
                clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

                if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f) {
                    // Negative offsets are illegal for vkCmdSetScissor
                    if (clip_rect.x < 0.0f)
                        clip_rect.x = 0.0f;
                    if (clip_rect.y < 0.0f)
                        clip_rect.y = 0.0f;

                    // Apply scissor/clipping rectangle
                    VkRect2D scissorRect;
                    scissorRect.offset.x = (int32_t)(clip_rect.x);
                    scissorRect.offset.y = (int32_t)(clip_rect.y);
                    scissorRect.extent.width  = (uint32_t)(clip_rect.z - clip_rect.x);
                    scissorRect.extent.height = (uint32_t)(clip_rect.w - clip_rect.y);
                    vkCmdSetScissor(cmdBuffer, 0, 1, &scissorRect);

                    // Draw the Image
                    if (pcmd->TextureId) {
                        VkDescriptorSet desc_set[1] = { (VkDescriptorSet)pcmd->TextureId };
                        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, desc_set, 0, NULL);
                    } else {
                        VkDescriptorSet desc_set[1] = { descriptorSet };
                        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, desc_set, 0, NULL);
                    }

                    vkCmdDrawIndexed(cmdBuffer, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
                }
                indexOffset += pcmd->ElemCount;
            }
            vertexOffset += cmd_list->VtxBuffer.Size;
        }
    }

    bool UIRender::updateBuffer(ImVec2 scale)
    {
        ImDrawData* imDrawData = ImGui::GetDrawData();

        // Check if ui buffers need to be recreated
        if (!imDrawData) {
            return false;
        }

        // BugFix: The ImGui is not correct on MacOS retina display.
        imDrawData->FramebufferScale = scale;

        VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
        VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

        bool updateBuffers = (vertexBuffer.buffer == VK_NULL_HANDLE) ||
            (vertexBuffer.count != imDrawData->TotalVtxCount) ||
            (indexBuffer.buffer == VK_NULL_HANDLE) ||
            (indexBuffer.count != imDrawData->TotalIdxCount);

        if (updateBuffers) {
            vkDeviceWaitIdle(device);
            if (vertexBuffer.buffer) {
                vertexBuffer.destroy();
            }
            vertexBuffer.create(vulkanDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, vertexBufferSize);
            vertexBuffer.count = imDrawData->TotalVtxCount;
            if (indexBuffer.buffer) {
                indexBuffer.destroy();
            }
            indexBuffer.create(vulkanDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, indexBufferSize);
            indexBuffer.count = imDrawData->TotalIdxCount;
        }

        // Upload data
        ImDrawVert* vtxDst = (ImDrawVert*)vertexBuffer.mapped;
        ImDrawIdx* idxDst = (ImDrawIdx*)indexBuffer.mapped;
        for (int n = 0; n < imDrawData->CmdListsCount; n++) {
            const ImDrawList* cmd_list = imDrawData->CmdLists[n];
            memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
            vtxDst += cmd_list->VtxBuffer.Size;
            idxDst += cmd_list->IdxBuffer.Size;
        }

        vertexBuffer.flush();
        indexBuffer.flush();

        return true;
    }

    void UIRender::updateParameters()
    {
        ImGuiIO& io = ImGui::GetIO();
        pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
        pushConstBlock.translate = glm::vec2(-1.0f);
    }
}  //namespace xy