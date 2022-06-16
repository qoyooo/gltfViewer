/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Vulkan Window.
 *
 * Version:
 *      2021.02.11  initial
 *
 */

#pragma once

#include "vulkan/vulkan.h"

#include <string>
#include <vector>

#include "camera.hpp"
#include "device.h"
#include "swapchain.h"

namespace xy
{

    class XyVulkanWindow
    {
    private:
        float fpsTimer = 0.0f;
        uint32_t frameCounter = 0;
        uint32_t destWidth;
        uint32_t destHeight;
        bool resizing = false;
        PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallback;
        PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallback;
        VkDebugReportCallbackEXT debugReportCallback;
        struct MultisampleTarget {
            struct {
                VkImage image;
                VkImageView view;
                VkDeviceMemory memory;
            } color;
            struct {
                VkImage image;
                VkImageView view;
                VkDeviceMemory memory;
            } depth;
        } multisampleTarget;

        //Register the Mouse and Key press callback
        GLFWmousebuttonfun    mouseClickCallbak;
        GLFWcursorposfun      mouseMoveCallback;
        GLFWscrollfun         mouseScrollCallback;
        GLFWkeyfun            mouseKeyCallback;
        GLFWcharfun           mouseCharCallback;
        void registerGlfwCallback();
        void unregisterGlfwCallback();
    protected:
        VkInstance instance;
        VkPhysicalDevice physicalDevice;
        VkPhysicalDeviceProperties deviceProperties;
        VkPhysicalDeviceFeatures deviceFeatures;
        VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
        VkDevice device;
        VulkanDevice *vulkanDevice;
        VkQueue queue;
        VkFormat depthFormat;
        VkCommandPool cmdPool;
        VkRenderPass renderPass;
        std::vector<VkFramebuffer>frameBuffers;
        uint32_t currentBuffer = 0;
        VkDescriptorPool  descriptorPool{VK_NULL_HANDLE};
        VkPipelineCache   pipelineCache{VK_NULL_HANDLE};
        VulkanSwapChain swapChain;
        std::string title = "glTF Viewer";
        std::string name = "glTF Viewer";
        void windowResize();
        void initGlfw();
    public: 
        static std::vector<const char*> args;
        bool prepared = false;
        uint32_t width = 1280;
        uint32_t height = 720;
        float frameTimer = 1.0f;
        Camera camera;
        glm::vec2 mousePos;
        bool paused = true;
        uint32_t lastFPS = 0;

        struct Settings {
            bool validation = true;
            bool fullscreen = false;
            bool vsync = false;
            bool multiSampling = true;
            VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_4_BIT;
        } settings;
        
        struct DepthStencil {
            VkImage image;
            VkDeviceMemory mem;
            VkImageView view;
        } depthStencil;

        struct GamePadState {
            glm::vec2 axisLeft = glm::vec2(0.0f);
            glm::vec2 axisRight = glm::vec2(0.0f);
        } gamePadState;

        struct MouseButtons {
            bool left = false;
            bool right = false;
            bool middle = false;
        } mouseButtons;

        GLFWwindow* window;
        GLFWwindow* setupWindow();

        XyVulkanWindow();
        virtual ~XyVulkanWindow();
        
        void initVulkan();

        virtual VkResult createInstance(bool enableValidation);
        virtual void render() = 0;
        virtual void windowResized();
        virtual void setupFrameBuffer();
        virtual void prepare();

        void initSwapchain();
        void setupSwapChain();

        void renderLoop();
        void renderFrame();
        void handleMouseClick(int button, int action, int mods);
        void handleMouseMove(double xoffset, double yoffset);
        void handleMouseScroll(double xoffset, double yoffset);
        //void handleKeyPress(int key, int scancode, int action, int mods);
        //void handleCharInput(unsigned int c);
    };

} //namespace xy
