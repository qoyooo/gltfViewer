/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      UI Render by imgui
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#pragma once

#include <vector>
#include <map>

#include "imgui/imgui.h"
#include "vulkan/vulkan.h"
#include "vulkan/device.h"
#include "vulkan/buffer.h"
#include "vulkan/texture.h"
#include "gltf/render.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "MaterialDesign.inl"

namespace xy
{

    class UIRender {
    private:
        VkDevice                device;
        Buffer                  vertexBuffer, indexBuffer;
        Texture2D               fontTexture;
        VulkanDevice*           vulkanDevice{nullptr};
        VkPipelineLayout        pipelineLayout{VK_NULL_HANDLE};
        VkPipeline              pipeline{VK_NULL_HANDLE};
        VkDescriptorPool        descriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout   descriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorSet         descriptorSet{VK_NULL_HANDLE};

        struct PushConstBlock {
            glm::vec2 scale = glm::vec2(1.0f, 1.0f);
            glm::vec2 translate;
        } pushConstBlock;

        void AddIconFont();
        void initImguiKeyCode();
        void initImguiStyle();

    public:

        UIRender(VulkanDevice *vulkanDevice, VkRenderPass renderPass, VkQueue queue,
            VkPipelineCache pipelineCache, VkSampleCountFlagBits multiSampleCount);

        ~UIRender();

        void draw(VkCommandBuffer cmdBuffer);

        bool updateBuffer(ImVec2 scale);

        void updateParameters();

        template<typename T>
        bool checkbox(const char* caption, T *value)
        {
            bool val = (*value == 1);
            bool res = ImGui::Checkbox(caption, &val);
            *value = val;
            return res;
        }

        bool header(const char *caption)
        {
            return ImGui::CollapsingHeader(caption, ImGuiTreeNodeFlags_DefaultOpen);
        }

        bool slider(const char* caption, float* value, float min, float max)
        {
            return ImGui::SliderFloat(caption, value, min, max);
        }

        bool combo(const char *caption, int32_t *itemindex, std::vector<std::string> items)
        {
            if (items.empty()) {
                return false;
            }
            std::vector<const char*> charitems;
            charitems.reserve(items.size());
            for (size_t i = 0; i < items.size(); i++) {
                charitems.push_back(items[i].c_str());
            }
            uint32_t itemCount = static_cast<uint32_t>(charitems.size());
            return ImGui::Combo(caption, itemindex, &charitems[0], itemCount, itemCount);
        }

        bool combo(const char *caption, std::string &selectedkey, std::map<std::string, std::string> items)
        {
            bool selectionChanged = false;
            if (ImGui::BeginCombo(caption, selectedkey.c_str())) {
                for (auto it = items.begin(); it != items.end(); ++it) {
                    const bool isSelected = it->first == selectedkey;
                    if (ImGui::Selectable(it->first.c_str(), isSelected)) {
                        selectionChanged = it->first != selectedkey;
                        selectedkey = it->first;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            return selectionChanged;
        }

        bool button(const char *caption)
        {
            return ImGui::Button(caption);
        }

        void text(const char *formatstr, ...)
        {
            va_list args;
            va_start(args, formatstr);
            ImGui::TextV(formatstr, args);
            va_end(args);
        }

        void vec3edit(const char* label, glm::vec3& value, float v_speed, float v_min, float v_max)
        {
            ImGui::DragFloat3(label, glm::value_ptr(value), v_speed, v_min, v_max);
        }
    };

}  //namespace xy