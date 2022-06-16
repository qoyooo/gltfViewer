/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Vulkan Window.
 *
 * Version:
 *      2021.02.11  initial
 *
 */

#include "vulkan/window.h"

#include <vulkan/vulkan.h>
#include <sstream>
#include "imgui/imgui.h"
#include "macros.h"
#include "logger.h"

namespace xy
{

    XyVulkanWindow *gVulkanWindow = NULL;
    void  XyMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        if(gVulkanWindow == NULL) {
            LOGE("Vulkan Window is not initialed.");
            return;
        }
        gVulkanWindow->handleMouseClick(button, action, mods);
    }

    void  XyMouseMoveCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        if(gVulkanWindow == NULL) {
            LOGE("Vulkan Window is not initialed.");
            return;
        }
        float xscale = 1.0f, yscale = 1.0f;
#if defined(_WIN32) || defined(_WIN64)
        glfwGetWindowContentScale(window, &xscale, &yscale);
#endif
        gVulkanWindow->handleMouseMove(xoffset/xscale, yoffset/yscale);
    }

    void  XyMouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        if(gVulkanWindow == NULL) {
            LOGE("Vulkan Window is not initialed.");
            return;
        }
        float xscale = 1.0f, yscale = 1.0f;
#if defined(_WIN32) || defined(_WIN64)
        glfwGetWindowContentScale(window, &xscale, &yscale);
#endif
        gVulkanWindow->handleMouseScroll(xoffset/xscale, yoffset/yscale);
    }

    void  XyKeyPressCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (action == GLFW_PRESS)
            io.KeysDown[key] = true;
        if (action == GLFW_RELEASE)
            io.KeysDown[key] = false;

        // Modifiers are not reliable across systems
        io.KeyCtrl = io.KeysDown[GLFW_KEY_LEFT_CONTROL] || io.KeysDown[GLFW_KEY_RIGHT_CONTROL];
        io.KeyShift = io.KeysDown[GLFW_KEY_LEFT_SHIFT] || io.KeysDown[GLFW_KEY_RIGHT_SHIFT];
        io.KeyAlt = io.KeysDown[GLFW_KEY_LEFT_ALT] || io.KeysDown[GLFW_KEY_RIGHT_ALT];
    #ifdef _WIN32
        io.KeySuper = false;
    #else
        io.KeySuper = io.KeysDown[GLFW_KEY_LEFT_SUPER] || io.KeysDown[GLFW_KEY_RIGHT_SUPER];
    #endif
    }

    void  XyCharInputCallback(GLFWwindow* window, unsigned int c)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharacter(c);
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType,
        uint64_t srcObject, size_t location, int32_t msgCode, const char * pLayerPrefix, const char * pMsg,
        void * pUserData)
    {
        std::string prefix("");
        if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
            prefix += "ERROR:";
        };
        if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
            prefix += "WARNING:";
        };
        if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
            prefix += "DEBUG:";
        }
        std::stringstream debugMessage;
        debugMessage << prefix << " [" << pLayerPrefix << "] Code " << msgCode << " : " << pMsg;

        LOGE("{}", debugMessage.str());

        //fflush(stdout);
        return VK_FALSE;
    }

    static void check_vk_result(VkResult err)
    {
        if (err == 0)
            return;
        LOGE("[vulkan] Error: VkResult = {}", err);
        if (err < 0)
            abort();
    }

    static void glfw_error_callback(int error, const char* description)
    {
        LOGE("Glfw ErrorL: {}, {}", error, description);
    }

    void XyVulkanWindow::initGlfw()
    {
        // Setup GLFW window
        glfwSetErrorCallback(glfw_error_callback);
        if (!glfwInit()) {
            exit(1);
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window  = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);

        if (!glfwVulkanSupported())
        {
            LOGE("GLFW: Vulkan Not Supported");
            exit(1);
        }
    }

    VkResult XyVulkanWindow::createInstance(bool enableValidation)
    {
        uint32_t extensions_count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensions_count);
        VkInstanceCreateInfo create_info = {};
        VkResult err;

        // Setup Vulkan
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.enabledExtensionCount = extensions_count;
        create_info.ppEnabledExtensionNames = extensions;

        if (settings.validation) {
            // Enabling validation layers
            const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
            create_info.enabledLayerCount = 1;
            create_info.ppEnabledLayerNames = layers;

            // Enable debug report extension (we need additional storage, so we duplicate the user array to add our new extension to it)
            const char** extensions_ext = (const char**)malloc(sizeof(const char*) * (extensions_count + 1));
            memcpy(extensions_ext, extensions, extensions_count * sizeof(const char*));
            extensions_ext[extensions_count] = "VK_EXT_debug_report";
            create_info.enabledExtensionCount = extensions_count + 1;
            create_info.ppEnabledExtensionNames = extensions_ext;

            // Create Vulkan Instance
            err = vkCreateInstance(&create_info, nullptr, &instance);
            check_vk_result(err);
            free(extensions_ext);

            // Get the function pointer (required for any extensions)
            vkCreateDebugReportCallback = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
            vkDestroyDebugReportCallback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));

            // Setup the debug report callback
            VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
            debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
            debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
            debug_report_ci.pfnCallback = debugMessageCallback;
            debug_report_ci.pUserData = NULL;
            err = vkCreateDebugReportCallback(instance, &debug_report_ci, nullptr, &debugReportCallback);
            check_vk_result(err);
        } else {
            // Create Vulkan Instance without any debug feature
            err = vkCreateInstance(&create_info, nullptr, &instance);
            check_vk_result(err);
        }

        return VK_SUCCESS;
    }

    void XyVulkanWindow::prepare()
    {
        /*
            Swapchain
        */
        initSwapchain();
        setupSwapChain();

        /*
            Command pool
        */
        VkCommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &cmdPool));

        /*
            Render pass
        */

        if (settings.multiSampling) {
            std::array<VkAttachmentDescription, 4> attachments = {};

            // Multisampled attachment that we render to
            attachments[0].format = swapChain.colorFormat;
            attachments[0].samples = settings.sampleCount;
            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            // This is the frame buffer attachment to where the multisampled image
            // will be resolved to and which will be presented to the swapchain
            attachments[1].format = swapChain.colorFormat;
            attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            // Multisampled depth attachment we render to
            attachments[2].format = depthFormat;
            attachments[2].samples = settings.sampleCount;
            attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            // Depth resolve attachment
            attachments[3].format = depthFormat;
            attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorReference = {};
            colorReference.attachment = 0;
            colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthReference = {};
            depthReference.attachment = 2;
            depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            // Resolve attachment reference for the color attachment
            VkAttachmentReference resolveReference = {};
            resolveReference.attachment = 1;
            resolveReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass = {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorReference;
            // Pass our resolve attachments to the sub pass
            subpass.pResolveAttachments = &resolveReference;
            subpass.pDepthStencilAttachment = &depthReference;

            std::array<VkSubpassDependency, 2> dependencies;

            dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[0].dstSubpass = 0;
            dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            dependencies[1].srcSubpass = 0;
            dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            VkRenderPassCreateInfo renderPassCI = {};
            renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());
            renderPassCI.pAttachments = attachments.data();
            renderPassCI.subpassCount = 1;
            renderPassCI.pSubpasses = &subpass;
            renderPassCI.dependencyCount = 2;
            renderPassCI.pDependencies = dependencies.data();
            VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderPass));
        } else {
            std::array<VkAttachmentDescription, 2> attachments = {};
            // Color attachment
            attachments[0].format = swapChain.colorFormat;
            attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            // Depth attachment
            attachments[1].format = depthFormat;
            attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorReference = {};
            colorReference.attachment = 0;
            colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthReference = {};
            depthReference.attachment = 1;
            depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpassDescription = {};
            subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpassDescription.colorAttachmentCount = 1;
            subpassDescription.pColorAttachments = &colorReference;
            subpassDescription.pDepthStencilAttachment = &depthReference;
            subpassDescription.inputAttachmentCount = 0;
            subpassDescription.pInputAttachments = nullptr;
            subpassDescription.preserveAttachmentCount = 0;
            subpassDescription.pPreserveAttachments = nullptr;
            subpassDescription.pResolveAttachments = nullptr;

            // Subpass dependencies for layout transitions
            std::array<VkSubpassDependency, 2> dependencies;

            dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[0].dstSubpass = 0;
            dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            dependencies[1].srcSubpass = 0;
            dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            VkRenderPassCreateInfo renderPassCI{};
            renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());
            renderPassCI.pAttachments = attachments.data();
            renderPassCI.subpassCount = 1;
            renderPassCI.pSubpasses = &subpassDescription;
            renderPassCI.dependencyCount = static_cast<uint32_t>(dependencies.size());
            renderPassCI.pDependencies = dependencies.data();
            VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderPass));
        }

        /*
            Pipeline cache
        */
        VkPipelineCacheCreateInfo pipelineCacheCreateInfo{};
        pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache));

        /*
            Frame buffer
        */
        setupFrameBuffer();
        prepared = true;
    }

    void XyVulkanWindow::renderFrame()
    {
        auto tStart = std::chrono::high_resolution_clock::now();

        render();
        frameCounter++;
        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        frameTimer = (float)tDiff / 1000.0f;
        camera.update(frameTimer);
        fpsTimer += (float)tDiff;
        if (fpsTimer > 1000.0f) {
            lastFPS = static_cast<uint32_t>((float)frameCounter * (1000.0f / fpsTimer));
            fpsTimer = 0.0f;
            frameCounter = 0;
        }
    }

    void XyVulkanWindow::renderLoop()
    {
        destWidth = width;
        destHeight = height;
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            renderFrame();
        }

        // Flush device to make sure all resources can be freed 
        vkDeviceWaitIdle(device);
    }

    XyVulkanWindow::XyVulkanWindow()
    {
        initGlfw();
        registerGlfwCallback();
    }

    XyVulkanWindow::~XyVulkanWindow()
    {
        unregisterGlfwCallback();

        // Clean up Vulkan resources
        swapChain.cleanup();
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        for (uint32_t i = 0; i < frameBuffers.size(); i++) {
            vkDestroyFramebuffer(device, frameBuffers[i], nullptr);
        }
        vkDestroyImageView(device, depthStencil.view, nullptr);
        vkDestroyImage(device, depthStencil.image, nullptr);
        vkFreeMemory(device, depthStencil.mem, nullptr);
        vkDestroyPipelineCache(device, pipelineCache, nullptr);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        if (settings.multiSampling) {
            vkDestroyImage(device, multisampleTarget.color.image, nullptr);
            vkDestroyImageView(device, multisampleTarget.color.view, nullptr);
            vkFreeMemory(device, multisampleTarget.color.memory, nullptr);
            vkDestroyImage(device, multisampleTarget.depth.image, nullptr);
            vkDestroyImageView(device, multisampleTarget.depth.view, nullptr);
            vkFreeMemory(device, multisampleTarget.depth.memory, nullptr);
        }
        delete vulkanDevice;
        if (settings.validation) {
            vkDestroyDebugReportCallback(instance, debugReportCallback, nullptr);
        }
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void XyVulkanWindow::initVulkan()
    {
        VkResult err;

        /*
            Instance creation
        */
        err = createInstance(settings.validation);
        if (err) {
            LOGE("Could not create Vulkan instance!");
            exit(err);
        }

        /*
            GPU selection
        */
        uint32_t gpuCount = 0;
        VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
        assert(gpuCount > 0);
        std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
        err = vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());
        if (err) {
            LOGE("Could not enumerate physical devices!");
            exit(err);
        }
        uint32_t selectedDevice = 0;

        LOGI("Selected GPU id is {}", selectedDevice);
        physicalDevice = physicalDevices[selectedDevice];

        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);

        /*
            Device creation
        */
        vulkanDevice = new VulkanDevice(physicalDevice);
        VkPhysicalDeviceFeatures enabledFeatures{};
        if (deviceFeatures.samplerAnisotropy) {
            enabledFeatures.samplerAnisotropy = VK_TRUE;
        }
        std::vector<const char*> enabledExtensions{};
        VkResult res = vulkanDevice->createLogicalDevice(enabledFeatures, enabledExtensions);
        if (res != VK_SUCCESS) {
            LOGE("Could not create Vulkan device!");
            exit(res);
        }
        device = vulkanDevice->logicalDevice;

        /*
            Graphics queue
        */
        vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.graphics, 0, &queue);

        /*
            Suitable depth format
        */
        std::vector<VkFormat> depthFormats = {
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM
        };
        VkBool32 validDepthFormat = false;
        for (auto& format : depthFormats) {
            VkFormatProperties formatProps;
            vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
            if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depthFormat = format;
                validDepthFormat = true;
                break;
            }
        }
        assert(validDepthFormat);

        swapChain.connect(instance, physicalDevice, device);
    }

    GLFWwindow* XyVulkanWindow::setupWindow()
    {
        return window;
    }

    void XyVulkanWindow::windowResized() {}

    void XyVulkanWindow::setupFrameBuffer()
    {
        /*
        MSAA
        */
        if (settings.multiSampling) {
            // Check if device supports requested sample count for color and depth frame buffer

            VkImageCreateInfo imageCI{};
            imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageCI.imageType = VK_IMAGE_TYPE_2D;
            imageCI.format = swapChain.colorFormat;
            imageCI.extent.width = width;
            imageCI.extent.height = height;
            imageCI.extent.depth = 1;
            imageCI.mipLevels = 1;
            imageCI.arrayLayers = 1;
            imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCI.samples = settings.sampleCount;
            imageCI.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &multisampleTarget.color.image));

            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(device, multisampleTarget.color.image, &memReqs);
            VkMemoryAllocateInfo memAllocInfo{};
            memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memAllocInfo.allocationSize = memReqs.size;
            VkBool32 lazyMemTypePresent;
            memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
            if (!lazyMemTypePresent) {
                memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            }
            VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &multisampleTarget.color.memory));
            vkBindImageMemory(device, multisampleTarget.color.image, multisampleTarget.color.memory, 0);

            // Create image view for the MSAA target
            VkImageViewCreateInfo imageViewCI{};
            imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCI.image = multisampleTarget.color.image;
            imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCI.format = swapChain.colorFormat;
            imageViewCI.components.r = VK_COMPONENT_SWIZZLE_R;
            imageViewCI.components.g = VK_COMPONENT_SWIZZLE_G;
            imageViewCI.components.b = VK_COMPONENT_SWIZZLE_B;
            imageViewCI.components.a = VK_COMPONENT_SWIZZLE_A;
            imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewCI.subresourceRange.levelCount = 1;
            imageViewCI.subresourceRange.layerCount = 1;
            VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &multisampleTarget.color.view));

            // Depth target
            imageCI.imageType = VK_IMAGE_TYPE_2D;
            imageCI.format = depthFormat;
            imageCI.extent.width = width;
            imageCI.extent.height = height;
            imageCI.extent.depth = 1;
            imageCI.mipLevels = 1;
            imageCI.arrayLayers = 1;
            imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCI.samples = settings.sampleCount;
            imageCI.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &multisampleTarget.depth.image));

            vkGetImageMemoryRequirements(device, multisampleTarget.depth.image, &memReqs);
            memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memAllocInfo.allocationSize = memReqs.size;
            memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
            if (!lazyMemTypePresent) {
                memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            }
            VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &multisampleTarget.depth.memory));
            vkBindImageMemory(device, multisampleTarget.depth.image, multisampleTarget.depth.memory, 0);

            // Create image view for the MSAA target
            imageViewCI.image = multisampleTarget.depth.image;
            imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCI.format = depthFormat;
            imageViewCI.components.r = VK_COMPONENT_SWIZZLE_R;
            imageViewCI.components.g = VK_COMPONENT_SWIZZLE_G;
            imageViewCI.components.b = VK_COMPONENT_SWIZZLE_B;
            imageViewCI.components.a = VK_COMPONENT_SWIZZLE_A;
            imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            imageViewCI.subresourceRange.levelCount = 1;
            imageViewCI.subresourceRange.layerCount = 1;
            VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &multisampleTarget.depth.view));
        }


        // Depth/Stencil attachment is the same for all frame buffers

        VkImageCreateInfo image = {};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.pNext = NULL;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = depthFormat;
        image.extent = { width, height, 1 };
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image.flags = 0;

        VkMemoryAllocateInfo mem_alloc = {};
        mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mem_alloc.pNext = NULL;
        mem_alloc.allocationSize = 0;
        mem_alloc.memoryTypeIndex = 0;

        VkImageViewCreateInfo depthStencilView = {};
        depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthStencilView.pNext = NULL;
        depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthStencilView.format = depthFormat;
        depthStencilView.flags = 0;
        depthStencilView.subresourceRange = {};
        depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        depthStencilView.subresourceRange.baseMipLevel = 0;
        depthStencilView.subresourceRange.levelCount = 1;
        depthStencilView.subresourceRange.baseArrayLayer = 0;
        depthStencilView.subresourceRange.layerCount = 1;

        VkMemoryRequirements memReqs;
        VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &depthStencil.image));
        vkGetImageMemoryRequirements(device, depthStencil.image, &memReqs);
        mem_alloc.allocationSize = memReqs.size;
        mem_alloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &mem_alloc, nullptr, &depthStencil.mem));
        VK_CHECK_RESULT(vkBindImageMemory(device, depthStencil.image, depthStencil.mem, 0));

        depthStencilView.image = depthStencil.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &depthStencil.view));

        //

        VkImageView attachments[4];

        if (settings.multiSampling) {
            attachments[0] = multisampleTarget.color.view;
            attachments[2] = multisampleTarget.depth.view;
            attachments[3] = depthStencil.view;
        }
        else {
            attachments[1] = depthStencil.view;
        }

        VkFramebufferCreateInfo frameBufferCI{};
        frameBufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCI.pNext = NULL;
        frameBufferCI.renderPass = renderPass;
        frameBufferCI.attachmentCount = settings.multiSampling ? 4 :2;
        frameBufferCI.pAttachments = attachments;
        frameBufferCI.width = width;
        frameBufferCI.height = height;
        frameBufferCI.layers = 1;

        // Create frame buffers for every swap chain image
        frameBuffers.resize(swapChain.imageCount);
        for (uint32_t i = 0; i < frameBuffers.size(); i++) {
            if (settings.multiSampling) {
                attachments[1] = swapChain.buffers[i].view;
            }
            else {
                attachments[0] = swapChain.buffers[i].view;
            }
            VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCI, nullptr, &frameBuffers[i]));
        }
    }

    void XyVulkanWindow::windowResize()
    {
        if (!prepared) {
            return;
        }
        prepared = false;

        vkDeviceWaitIdle(device);
        width = destWidth;
        height = destHeight;
        setupSwapChain();
        if (settings.multiSampling) {
            vkDestroyImageView(device, multisampleTarget.color.view, nullptr);
            vkDestroyImage(device, multisampleTarget.color.image, nullptr);
            vkFreeMemory(device, multisampleTarget.color.memory, nullptr);
            vkDestroyImageView(device, multisampleTarget.depth.view, nullptr);
            vkDestroyImage(device, multisampleTarget.depth.image, nullptr);
            vkFreeMemory(device, multisampleTarget.depth.memory, nullptr);
        }
        vkDestroyImageView(device, depthStencil.view, nullptr);
        vkDestroyImage(device, depthStencil.image, nullptr);
        vkFreeMemory(device, depthStencil.mem, nullptr);
        for (uint32_t i = 0; i < frameBuffers.size(); i++) {
            vkDestroyFramebuffer(device, frameBuffers[i], nullptr);
        }
        setupFrameBuffer();
        vkDeviceWaitIdle(device);

        camera.updateAspectRatio((float)width / (float)height);
        windowResized();

        prepared = true;
    }

    void XyVulkanWindow::initSwapchain()
    {
        swapChain.initSurface(window);
    }

    void XyVulkanWindow::setupSwapChain()
    {
        swapChain.create(&width, &height, settings.vsync);
    }

    void XyVulkanWindow::handleMouseClick(int button, int action, int mods)
    {
        if (action == GLFW_RELEASE) {
            mouseButtons.left   = false;
            mouseButtons.right  = false;
            mouseButtons.middle = false;
            return;
        }
        switch (button) {
            case GLFW_MOUSE_BUTTON_LEFT:
                mouseButtons.left = true;
                break;
            case GLFW_MOUSE_BUTTON_RIGHT:
                mouseButtons.right = true;
                break;
            case GLFW_MOUSE_BUTTON_MIDDLE:
                mouseButtons.middle = true;
                break;
            default:
                break;
        }
    }

    void XyVulkanWindow::handleMouseMove(double x, double y)
    {
        float dx = mousePos.x - (float)x;
        float dy = mousePos.y - (float)y;

        ImGuiIO& io = ImGui::GetIO();
        bool handled = io.WantCaptureMouse;

        if (handled) {
            mousePos = glm::vec2((float)x, (float)y);
            return;
        }

        if (mouseButtons.left) {
            camera.rotate(glm::vec3(dy * camera.rotationSpeed, -dx * camera.rotationSpeed, 0.0f));
        }
        if (mouseButtons.right) {
            camera.translate(glm::vec3(-0.0f, 0.0f, dy * .005f * camera.movementSpeed));
        }
        if (mouseButtons.middle) {
            camera.translate(glm::vec3(-dx * 0.01f, -dy * 0.01f, 0.0f));
        }
        mousePos = glm::vec2((float)x, (float)y);
    }

    void XyVulkanWindow::handleMouseScroll(double x, double y)
    {
        camera.translate(glm::vec3(0.0f, 0.0f, -(float)y * 0.05f * camera.movementSpeed));
    }

    void XyVulkanWindow::registerGlfwCallback()
    {
        gVulkanWindow = this;

        mouseClickCallbak   = glfwSetMouseButtonCallback(window, XyMouseButtonCallback);
        mouseMoveCallback   = glfwSetCursorPosCallback(window, XyMouseMoveCallback);
        mouseScrollCallback = glfwSetScrollCallback(window, XyMouseScrollCallback);
        mouseKeyCallback    = glfwSetKeyCallback(window, XyKeyPressCallback);
        mouseCharCallback   = glfwSetCharCallback(window, XyCharInputCallback);
    }

    void XyVulkanWindow::unregisterGlfwCallback()
    {
        glfwSetMouseButtonCallback(window, nullptr);
        glfwSetCursorPosCallback(window, nullptr);
        glfwSetScrollCallback(window, nullptr);
        glfwSetKeyCallback(window, nullptr);
        glfwSetCharCallback(window, nullptr);
    }

} //namespace xy