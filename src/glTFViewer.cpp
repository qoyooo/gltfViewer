/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define the master class of glTFViewer
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#define TINYGLTF_IMPLEMENTATION

#include <vulkan/vulkan.h>
#include "vulkan/window.h"
#include "vulkan/macros.h"
#include "vulkan/buffer.h"
#include "gui/render.h"
#include "gui/IconsMaterialDesignIcons.h"
#include "ImGuizmo.h"
#include "ImSequencer.h"
#include "gltf/render.h"
#include "gltf/textures.h"
#include "filedialog.h"
#include "logger.h"
#include "vulkan/utils.h"
#include "skybox/skybox.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <limits.h>
#include <filesystem>

#ifdef __APPLE__
#include "CoreFoundation/CoreFoundation.h"
#elif defined(_WIN32_) || defined(_WIN64_)
#include "window.h"
#include "stdlib.h"
#endif

using namespace xy;

static const char* SequencerItemTypeNames[] = { "Translation","Rotation", "Scale", "Weight"};

// Draw Sequence
struct MySequence : public ImSequencer::SequenceInterface
{
    int mFrameMin, mFrameMax;

    // interface with sequencer
    vkglTF::Animation *animation = nullptr;

    // my datas
    MySequence() : mFrameMin(0), mFrameMax(0) {}

    virtual int GetFrameMin() const {
        return 0;
    }
    virtual int GetFrameMax() const {
        if (!animation) return 1000;
        return (int)animation->samplers[0].inputs.size() - 1;
    }
    virtual int GetItemCount() const {
        if (!animation) return 5;
        return (int)animation->channels.size();
    }

    virtual int GetItemTypeCount() const {
        return sizeof(SequencerItemTypeNames) / sizeof(char*);
    }

    virtual const char* GetItemTypeName(int index) const {
        if (index >= (sizeof(SequencerItemTypeNames) / sizeof(char*)))
            index = 0;
        return SequencerItemTypeNames[index];
    }

    virtual const char* GetItemLabel(int index) const
    {
        static char tmps[512];
        if (!animation) {
            snprintf(tmps, 512, "[%02d] None", index);
        } else {
            snprintf(tmps, 512, "[%02d] %s-%s",
                index, animation->channels[index].node->name.c_str(),
                GetItemTypeName(animation->channels[index].path));
        }
        return tmps;
    }

    virtual void Get(int index, int** start, int** end, int* type, unsigned int* color)
    {
        if (color)
            *color = 0xFFAA8080;
        if (start)
            *start = &mFrameMin;
        if (end)
            *end   = &mFrameMax;
        if (type && animation)
            *type = animation->channels[index].path;
    }
};

/*
    PBR example main class
*/
class VulkanExample : public XyVulkanWindow
{
public:
    Textures textures;

    struct shaderValuesParams {
        glm::vec4 lightDir;
        float exposure = 4.5f;
        float gamma = 2.2f;
        float prefilteredCubeMipLevels;
        float scaleIBLAmbient = 1.0f;
        float debugViewInputs = 0;
        float debugViewEquation = 0;
    } shaderValuesParams;

    std::vector<VkCommandBuffer>        commandBuffers;
    std::vector<Buffer>                 uniformBufferParams;

    std::vector<VkFence>     waitFences;
    std::vector<VkSemaphore> renderCompleteSemaphores;
    std::vector<VkSemaphore> presentCompleteSemaphores;

    const uint32_t renderAhead = 2;
    uint32_t frameIndex = 0;

    struct LightSource {
        glm::vec3 color = glm::vec3(1.0f);
        glm::vec3 rotation = glm::vec3(75.0f, 40.0f, 0.0f);
    } lightSource;

    // Skybox and Environments
    bool displayBackground = true;
    SkyboxRender*  skybox{nullptr};
    std::map<std::string, std::string> environments;
    std::string selectedEnvironment = "papermill";

    // Parameters for UI
    UIRender *ui;

    // ImGuizmo
    bool showGizmo = false;
    vkglTF::Node *selectedNode = nullptr;
    ImGuizmo::OPERATION mCurrentGizmoOperation = ImGuizmo::OPERATION::ROTATE;
    ImGuizmo::MODE mCurrentGizmoMode = ImGuizmo::MODE::WORLD;

    // ImSequencer
    MySequence mySequence;

    // Parameters for GLTF models
    GLTFRender* modelRenderer = nullptr;
    int32_t debugViewInputs = 0;
    int32_t debugViewEquation = 0;
    int32_t animationIndex = 0;
    float   animationTimer = 0.0f;
    // bool    animate = true;

    VulkanExample() : XyVulkanWindow()
    {
        title = "glTF Viewer";
#if defined(TINYGLTF_ENABLE_DRACO)
        LOGI("Draco mesh compression is enabled");
#endif

    }

    ~VulkanExample()
    {
        for (auto buffer : uniformBufferParams) {
            buffer.destroy();
        }
        for (auto fence : waitFences) {
            vkDestroyFence(device, fence, nullptr);
        }
        for (auto semaphore : renderCompleteSemaphores) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
        for (auto semaphore : presentCompleteSemaphores) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }

        textures.environmentCube.destroy();
        textures.irradianceCube.destroy();
        textures.prefilteredCube.destroy();
        textures.lutBrdf.destroy();
        textures.empty.destroy();

        // Delete Skybox
        if (skybox) {
            delete skybox;
        }

        // TODO: Models Rendering destroy
        if (modelRenderer)
        {
            delete modelRenderer;
        }

        delete ui;
    }

    void recordCommandBuffers()
    {
        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        VkClearValue clearValues[3];
        if (settings.multiSampling) {
            clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            clearValues[2].depthStencil = { 1.0f, 0 };
        }
        else {
            clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            clearValues[1].depthStencil = { 1.0f, 0 };
        }

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = settings.multiSampling ? 3 : 2;
        renderPassBeginInfo.pClearValues = clearValues;

        for (uint32_t i = 0; i < commandBuffers.size(); ++i) {
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            VkCommandBuffer currentCB = commandBuffers[i];

            VK_CHECK_RESULT(vkBeginCommandBuffer(currentCB, &cmdBufferBeginInfo));
            vkCmdBeginRenderPass(currentCB, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.width = (float)width;
            viewport.height = (float)height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(currentCB, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.extent = { width, height };
            vkCmdSetScissor(currentCB, 0, 1, &scissor);

            VkDeviceSize offsets[1] = { 0 };

            if (displayBackground && skybox) {
                skybox->recordCommandBuffers(currentCB, i);
            }

            // Models Rendering
            if (modelRenderer)
            {
                modelRenderer->recordCommandBuffers(currentCB, i);
            }

            ui->draw(currentCB);

            vkCmdEndRenderPass(currentCB);
            VK_CHECK_RESULT(vkEndCommandBuffer(currentCB));
        }
    }

    void loadEnvironment(std::string filename)
    {
        LOGI("Loading environment from {}", filename);
        if (textures.environmentCube.image) {
            textures.environmentCube.destroy();
        }
        textures.environmentCube.loadFromFile(filename, VK_FORMAT_R16G16B16A16_SFLOAT, vulkanDevice, queue);
        generateCubemaps();
    }

    void loadAssets()
    {
        const std::string assetpath = "./../data/";
        struct stat info;
        if (stat(assetpath.c_str(), &info) != 0) {
            LOGE("Could not locate asset path in {}.\nMake sure binary is run from correct relative directory!", assetpath);
            exit(-1);
        }
        readDirectory(assetpath + "environments", "*.ktx", environments, false);
        for (auto item : environments) {
            LOGI("Cubemap {} : {}", item.first, item.second);
        }

        textures.empty.loadFromFile(assetpath + "textures/empty.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);

        std::string envMapFile = assetpath + "environments/papermill.ktx";
        loadEnvironment(envMapFile.c_str());
    }

    void setupDescriptors()
    {
        // Setup Descriptors for Skybox
        if (skybox) {
            skybox->setupDescriptors();
        }

        // Models Rendering
        if (modelRenderer)
        {
            modelRenderer->setupDescriptors();
        }
    }

    void preparePipelines()
    {
        // Prepare Skybox pipeline
        if (skybox) {
            skybox->preparePipelines();
        }

        // Models Rendering
        if (modelRenderer)
        {
            modelRenderer->preparePipelines();
        }
    }

    /*
        Generate a BRDF integration map storing roughness/NdotV as a look-up-table
    */
    void generateBRDFLUT()
    {
        auto tStart = std::chrono::high_resolution_clock::now();

        const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
        const int32_t dim = 512;

        // Image
        VkImageCreateInfo imageCI{};
        imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCI.imageType = VK_IMAGE_TYPE_2D;
        imageCI.format = format;
        imageCI.extent.width = dim;
        imageCI.extent.height = dim;
        imageCI.extent.depth = 1;
        imageCI.mipLevels = 1;
        imageCI.arrayLayers = 1;
        imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &textures.lutBrdf.image));
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, textures.lutBrdf.image, &memReqs);
        VkMemoryAllocateInfo memAllocInfo{};
        memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &textures.lutBrdf.deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, textures.lutBrdf.image, textures.lutBrdf.deviceMemory, 0));

        // View
        VkImageViewCreateInfo viewCI{};
        viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format = format;
        viewCI.subresourceRange = {};
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.levelCount = 1;
        viewCI.subresourceRange.layerCount = 1;
        viewCI.image = textures.lutBrdf.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &textures.lutBrdf.view));

        // Sampler
        VkSamplerCreateInfo samplerCI{};
        samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCI.magFilter = VK_FILTER_LINEAR;
        samplerCI.minFilter = VK_FILTER_LINEAR;
        samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.minLod = 0.0f;
        samplerCI.maxLod = 1.0f;
        samplerCI.maxAnisotropy = 1.0f;
        samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &textures.lutBrdf.sampler));

        // FB, Att, RP, Pipe, etc.
        VkAttachmentDescription attDesc{};
        // Color attachment
        attDesc.format = format;
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &colorReference;

        // Use subpass dependencies for layout transitions
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

        // Create the actual renderpass
        VkRenderPassCreateInfo renderPassCI{};
        renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCI.attachmentCount = 1;
        renderPassCI.pAttachments = &attDesc;
        renderPassCI.subpassCount = 1;
        renderPassCI.pSubpasses = &subpassDescription;
        renderPassCI.dependencyCount = 2;
        renderPassCI.pDependencies = dependencies.data();

        VkRenderPass renderpass;
        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderpass));

        VkFramebufferCreateInfo framebufferCI{};
        framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCI.renderPass = renderpass;
        framebufferCI.attachmentCount = 1;
        framebufferCI.pAttachments = &textures.lutBrdf.view;
        framebufferCI.width = dim;
        framebufferCI.height = dim;
        framebufferCI.layers = 1;

        VkFramebuffer framebuffer;
        VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &framebuffer));

        // Desriptors
        VkDescriptorSetLayout descriptorsetlayout;
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
        descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

        // Pipeline layout
        VkPipelineLayout pipelinelayout;
        VkPipelineLayoutCreateInfo pipelineLayoutCI{};
        pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCI.setLayoutCount = 1;
        pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

        // Pipeline
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
        multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicStateCI{};
        dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
        dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
        
        VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
        emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        VkGraphicsPipelineCreateInfo pipelineCI{};
        pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCI.layout = pipelinelayout;
        pipelineCI.renderPass = renderpass;
        pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCI.pVertexInputState = &emptyInputStateCI;
        pipelineCI.pRasterizationState = &rasterizationStateCI;
        pipelineCI.pColorBlendState = &colorBlendStateCI;
        pipelineCI.pMultisampleState = &multisampleStateCI;
        pipelineCI.pViewportState = &viewportStateCI;
        pipelineCI.pDepthStencilState = &depthStencilStateCI;
        pipelineCI.pDynamicState = &dynamicStateCI;
        pipelineCI.stageCount = 2;
        pipelineCI.pStages = shaderStages.data();

        // Look-up-table (from BRDF) pipeline        
        shaderStages = {
            loadShader(device, "genbrdflut.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(device, "genbrdflut.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        VkPipeline pipeline;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
        for (auto shaderStage : shaderStages) {
            vkDestroyShaderModule(device, shaderStage.module, nullptr);
        }

        // Render
        VkClearValue clearValues[1];
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = renderpass;
        renderPassBeginInfo.renderArea.extent.width = dim;
        renderPassBeginInfo.renderArea.extent.height = dim;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = clearValues;
        renderPassBeginInfo.framebuffer = framebuffer;

        VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = (float)dim;
        viewport.height = (float)dim;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.extent.width = dim;
        scissor.extent.height = dim;

        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdDraw(cmdBuf, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmdBuf);
        vulkanDevice->flushCommandBuffer(cmdBuf, queue);

        vkQueueWaitIdle(queue);

        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelinelayout, nullptr);
        vkDestroyRenderPass(device, renderpass, nullptr);
        vkDestroyFramebuffer(device, framebuffer, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);

        textures.lutBrdf.descriptor.imageView = textures.lutBrdf.view;
        textures.lutBrdf.descriptor.sampler = textures.lutBrdf.sampler;
        textures.lutBrdf.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        textures.lutBrdf.device = vulkanDevice;

        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        LOGI("Generating BRDF LUT took {} ms", tDiff);
    }

    /*
        Offline generation for the cube maps used for PBR lighting        
        - Irradiance cube map
        - Pre-filterd environment cubemap
    */
    void generateCubemaps()
    {
        enum Target { IRRADIANCE = 0, PREFILTEREDENV = 1 };

        for (uint32_t target = 0; target < PREFILTEREDENV + 1; target++) {

            TextureCubeMap cubemap;

            auto tStart = std::chrono::high_resolution_clock::now();

            VkFormat format;
            int32_t dim;

            switch (target) {
            case IRRADIANCE:
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
                dim = 64;
                break;
            case PREFILTEREDENV:
                format = VK_FORMAT_R16G16B16A16_SFLOAT;
                dim = 512;
                break;
            };

            const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

            // Create target cubemap
            {
                // Image
                VkImageCreateInfo imageCI{};
                imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                imageCI.imageType = VK_IMAGE_TYPE_2D;
                imageCI.format = format;
                imageCI.extent.width = dim;
                imageCI.extent.height = dim;
                imageCI.extent.depth = 1;
                imageCI.mipLevels = numMips;
                imageCI.arrayLayers = 6;
                imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
                imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
                VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &cubemap.image));
                VkMemoryRequirements memReqs;
                vkGetImageMemoryRequirements(device, cubemap.image, &memReqs);
                VkMemoryAllocateInfo memAllocInfo{};
                memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                memAllocInfo.allocationSize = memReqs.size;
                memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &cubemap.deviceMemory));
                VK_CHECK_RESULT(vkBindImageMemory(device, cubemap.image, cubemap.deviceMemory, 0));

                // View
                VkImageViewCreateInfo viewCI{};
                viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
                viewCI.format = format;
                viewCI.subresourceRange = {};
                viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewCI.subresourceRange.levelCount = numMips;
                viewCI.subresourceRange.layerCount = 6;
                viewCI.image = cubemap.image;
                VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &cubemap.view));

                // Sampler
                VkSamplerCreateInfo samplerCI{};
                samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerCI.magFilter = VK_FILTER_LINEAR;
                samplerCI.minFilter = VK_FILTER_LINEAR;
                samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerCI.minLod = 0.0f;
                samplerCI.maxLod = static_cast<float>(numMips);
                samplerCI.maxAnisotropy = 1.0f;
                samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
                VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &cubemap.sampler));
            }

            // FB, Att, RP, Pipe, etc.
            VkAttachmentDescription attDesc{};
            // Color attachment
            attDesc.format = format;
            attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
            attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

            VkSubpassDescription subpassDescription{};
            subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpassDescription.colorAttachmentCount = 1;
            subpassDescription.pColorAttachments = &colorReference;

            // Use subpass dependencies for layout transitions
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

            // Renderpass
            VkRenderPassCreateInfo renderPassCI{};
            renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassCI.attachmentCount = 1;
            renderPassCI.pAttachments = &attDesc;
            renderPassCI.subpassCount = 1;
            renderPassCI.pSubpasses = &subpassDescription;
            renderPassCI.dependencyCount = 2;
            renderPassCI.pDependencies = dependencies.data();
            VkRenderPass renderpass;
            VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderpass));

            struct Offscreen {
                VkImage image;
                VkImageView view;
                VkDeviceMemory memory;
                VkFramebuffer framebuffer;
            } offscreen;

            // Create offscreen framebuffer
            {
                // Image
                VkImageCreateInfo imageCI{};
                imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                imageCI.imageType = VK_IMAGE_TYPE_2D;
                imageCI.format = format;
                imageCI.extent.width = dim;
                imageCI.extent.height = dim;
                imageCI.extent.depth = 1;
                imageCI.mipLevels = 1;
                imageCI.arrayLayers = 1;
                imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
                imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &offscreen.image));
                VkMemoryRequirements memReqs;
                vkGetImageMemoryRequirements(device, offscreen.image, &memReqs);
                VkMemoryAllocateInfo memAllocInfo{};
                memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                memAllocInfo.allocationSize = memReqs.size;
                memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &offscreen.memory));
                VK_CHECK_RESULT(vkBindImageMemory(device, offscreen.image, offscreen.memory, 0));

                // View
                VkImageViewCreateInfo viewCI{};
                viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewCI.format = format;
                viewCI.flags = 0;
                viewCI.subresourceRange = {};
                viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewCI.subresourceRange.baseMipLevel = 0;
                viewCI.subresourceRange.levelCount = 1;
                viewCI.subresourceRange.baseArrayLayer = 0;
                viewCI.subresourceRange.layerCount = 1;
                viewCI.image = offscreen.image;
                VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &offscreen.view));

                // Framebuffer
                VkFramebufferCreateInfo framebufferCI{};
                framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferCI.renderPass = renderpass;
                framebufferCI.attachmentCount = 1;
                framebufferCI.pAttachments = &offscreen.view;
                framebufferCI.width = dim;
                framebufferCI.height = dim;
                framebufferCI.layers = 1;
                VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &offscreen.framebuffer));

                VkCommandBuffer layoutCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
                VkImageMemoryBarrier imageMemoryBarrier{};
                imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                imageMemoryBarrier.image = offscreen.image;
                imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                imageMemoryBarrier.srcAccessMask = 0;
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                vkCmdPipelineBarrier(layoutCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
                vulkanDevice->flushCommandBuffer(layoutCmd, queue, true);
            }

            // Descriptors
            VkDescriptorSetLayout descriptorsetlayout;
            VkDescriptorSetLayoutBinding setLayoutBinding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
            descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCI.pBindings = &setLayoutBinding;
            descriptorSetLayoutCI.bindingCount = 1;
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

            // Descriptor Pool
            VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
            VkDescriptorPoolCreateInfo descriptorPoolCI{};
            descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolCI.poolSizeCount = 1;
            descriptorPoolCI.pPoolSizes = &poolSize;
            descriptorPoolCI.maxSets = 2;
            VkDescriptorPool descriptorpool;
            VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorpool));

            // Descriptor sets
            VkDescriptorSet descriptorset;
            VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
            descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocInfo.descriptorPool = descriptorpool;
            descriptorSetAllocInfo.pSetLayouts = &descriptorsetlayout;
            descriptorSetAllocInfo.descriptorSetCount = 1;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorset));
            VkWriteDescriptorSet writeDescriptorSet{};
            writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writeDescriptorSet.descriptorCount = 1;
            writeDescriptorSet.dstSet = descriptorset;
            writeDescriptorSet.dstBinding = 0;
            writeDescriptorSet.pImageInfo = &textures.environmentCube.descriptor;
            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

            struct PushBlockIrradiance {
                glm::mat4 mvp;
                float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
                float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
            } pushBlockIrradiance;

            struct PushBlockPrefilterEnv {
                glm::mat4 mvp;
                float roughness;
                uint32_t numSamples = 32u;
            } pushBlockPrefilterEnv;

            // Pipeline layout
            VkPipelineLayout pipelinelayout;
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            switch (target) {
                case IRRADIANCE:
                    pushConstantRange.size = sizeof(PushBlockIrradiance);
                    break;
                case PREFILTEREDENV:
                    pushConstantRange.size = sizeof(PushBlockPrefilterEnv);
                    break;
            };

            VkPipelineLayoutCreateInfo pipelineLayoutCI{};
            pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutCI.setLayoutCount = 1;
            pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
            pipelineLayoutCI.pushConstantRangeCount = 1;
            pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
            VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

            // Pipeline
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
            multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            
            std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynamicStateCI{};
            dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
            dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

            // Vertex input state
            VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
            VkVertexInputAttributeDescription vertexInputAttribute = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

            VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
            vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputStateCI.vertexBindingDescriptionCount = 1;
            vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
            vertexInputStateCI.vertexAttributeDescriptionCount = 1;
            vertexInputStateCI.pVertexAttributeDescriptions = &vertexInputAttribute;

            std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

            VkGraphicsPipelineCreateInfo pipelineCI{};
            pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineCI.layout = pipelinelayout;
            pipelineCI.renderPass = renderpass;
            pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
            pipelineCI.pVertexInputState = &vertexInputStateCI;
            pipelineCI.pRasterizationState = &rasterizationStateCI;
            pipelineCI.pColorBlendState = &colorBlendStateCI;
            pipelineCI.pMultisampleState = &multisampleStateCI;
            pipelineCI.pViewportState = &viewportStateCI;
            pipelineCI.pDepthStencilState = &depthStencilStateCI;
            pipelineCI.pDynamicState = &dynamicStateCI;
            pipelineCI.stageCount = 2;
            pipelineCI.pStages = shaderStages.data();
            pipelineCI.renderPass = renderpass;

            shaderStages[0] = loadShader(device, "filtercube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
            switch (target) {
                case IRRADIANCE:
                    shaderStages[1] = loadShader(device, "irradiancecube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
                    break;
                case PREFILTEREDENV:
                    shaderStages[1] = loadShader(device, "prefilterenvmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
                    break;
            };
            VkPipeline pipeline;
            VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
            for (auto shaderStage : shaderStages) {
                vkDestroyShaderModule(device, shaderStage.module, nullptr);
            }

            // Render cubemap
            VkClearValue clearValues[1];
            clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };

            VkRenderPassBeginInfo renderPassBeginInfo{};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.renderPass = renderpass;
            renderPassBeginInfo.framebuffer = offscreen.framebuffer;
            renderPassBeginInfo.renderArea.extent.width = dim;
            renderPassBeginInfo.renderArea.extent.height = dim;
            renderPassBeginInfo.clearValueCount = 1;
            renderPassBeginInfo.pClearValues = clearValues;

            std::vector<glm::mat4> matrices = {
                glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
                glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
                glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
                glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
                glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
                glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            };

            VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);

            VkViewport viewport{};
            viewport.width = (float)dim;
            viewport.height = (float)dim;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor{};
            scissor.extent.width = dim;
            scissor.extent.height = dim;

            VkImageSubresourceRange subresourceRange{};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = numMips;
            subresourceRange.layerCount = 6;

            // Change image layout for all cubemap faces to transfer destination
            {
                vulkanDevice->beginCommandBuffer(cmdBuf);
                VkImageMemoryBarrier imageMemoryBarrier{};
                imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                imageMemoryBarrier.image = cubemap.image;
                imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imageMemoryBarrier.srcAccessMask = 0;
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                imageMemoryBarrier.subresourceRange = subresourceRange;
                vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
                vulkanDevice->flushCommandBuffer(cmdBuf, queue, false);
            }

            for (uint32_t m = 0; m < numMips; m++) {
                for (uint32_t f = 0; f < 6; f++) {

                    vulkanDevice->beginCommandBuffer(cmdBuf);

                    viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
                    viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
                    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
                    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

                    // Render scene from cube face's point of view
                    vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                    // Pass parameters for current pass using a push constant block
                    switch (target) {
                        case IRRADIANCE:
                            pushBlockIrradiance.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
                            vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockIrradiance), &pushBlockIrradiance);
                            break;
                        case PREFILTEREDENV:
                            pushBlockPrefilterEnv.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
                            // Roughness can be adopted
                            pushBlockPrefilterEnv.roughness =  (float)m / (float)(numMips - 1);
                            vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockPrefilterEnv), &pushBlockPrefilterEnv);
                            break;
                    };

                    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

                    VkDeviceSize offsets[1] = { 0 };

                    skybox->models.skybox.draw(cmdBuf);

                    vkCmdEndRenderPass(cmdBuf);

                    VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    subresourceRange.baseMipLevel = 0;
                    subresourceRange.levelCount = numMips;
                    subresourceRange.layerCount = 6;

                    {
                        VkImageMemoryBarrier imageMemoryBarrier{};
                        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        imageMemoryBarrier.image = offscreen.image;
                        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                        imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                        imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                        vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
                    }

                    // Copy region for transfer from framebuffer to cube face
                    VkImageCopy copyRegion{};

                    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copyRegion.srcSubresource.baseArrayLayer = 0;
                    copyRegion.srcSubresource.mipLevel = 0;
                    copyRegion.srcSubresource.layerCount = 1;
                    copyRegion.srcOffset = { 0, 0, 0 };

                    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copyRegion.dstSubresource.baseArrayLayer = f;
                    copyRegion.dstSubresource.mipLevel = m;
                    copyRegion.dstSubresource.layerCount = 1;
                    copyRegion.dstOffset = { 0, 0, 0 };

                    copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
                    copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
                    copyRegion.extent.depth = 1;

                    vkCmdCopyImage(
                        cmdBuf,
                        offscreen.image,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        cubemap.image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &copyRegion);

                    {
                        VkImageMemoryBarrier imageMemoryBarrier{};
                        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        imageMemoryBarrier.image = offscreen.image;
                        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                        imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                        imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                        vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
                    }

                    vulkanDevice->flushCommandBuffer(cmdBuf, queue, false);
                }
            }

            {
                vulkanDevice->beginCommandBuffer(cmdBuf);
                VkImageMemoryBarrier imageMemoryBarrier{};
                imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                imageMemoryBarrier.image = cubemap.image;
                imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                imageMemoryBarrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                imageMemoryBarrier.subresourceRange = subresourceRange;
                vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
                vulkanDevice->flushCommandBuffer(cmdBuf, queue, false);
            }

            vkDestroyRenderPass(device, renderpass, nullptr);
            vkDestroyFramebuffer(device, offscreen.framebuffer, nullptr);
            vkFreeMemory(device, offscreen.memory, nullptr);
            vkDestroyImageView(device, offscreen.view, nullptr);
            vkDestroyImage(device, offscreen.image, nullptr);
            vkDestroyDescriptorPool(device, descriptorpool, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyPipelineLayout(device, pipelinelayout, nullptr);

            cubemap.descriptor.imageView = cubemap.view;
            cubemap.descriptor.sampler = cubemap.sampler;
            cubemap.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            cubemap.device = vulkanDevice;

            switch (target) {
                case IRRADIANCE:
                    if(textures.irradianceCube.image) {
                        textures.irradianceCube.destroy();
                    }
                    textures.irradianceCube = cubemap;
                    break;
                case PREFILTEREDENV:
                    if(textures.prefilteredCube.image) {
                        textures.prefilteredCube.destroy();
                    }
                    textures.prefilteredCube = cubemap;
                    shaderValuesParams.prefilteredCubeMipLevels = static_cast<float>(numMips);
                    break;
            };

            auto tEnd = std::chrono::high_resolution_clock::now();
            auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            LOGI("Generating cube map with {} mip levels took {} ms", numMips, tDiff);
        }
    }

    /* 
        Prepare and initialize uniform buffers containing shader parameters
    */
    void prepareUniformBuffers()
    {
        for (auto &uniformBuffer : uniformBufferParams) {
            uniformBuffer.create(vulkanDevice, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(shaderValuesParams));
        }
        updateUniformBuffers();
    }

    void updateUniformBuffers()
    {
        // Update Skybox UniformBuffers
        if (skybox) {
            skybox->updateUniformBuffers(currentBuffer);
        }

        //  Update Models UniformBuffers
        if (modelRenderer)
        {
            modelRenderer->updateUniformBuffers(currentBuffer);
        }
    }

    void updateParams()
    {
        shaderValuesParams.lightDir = glm::vec4(
            sin(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)),
            sin(glm::radians(lightSource.rotation.y)),
            cos(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)),
            0.0f);
        memcpy(uniformBufferParams[currentBuffer].mapped, &shaderValuesParams, sizeof(shaderValuesParams));
    }

    void windowResized()
    {
        recordCommandBuffers();
        vkDeviceWaitIdle(device);
        updateUniformBuffers();
        updateOverlay();
    }

    void prepare()
    {
        XyVulkanWindow::prepare();

        camera.type = Camera::CameraType::lookat;

        camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);
        camera.rotationSpeed = 0.25f;
        camera.movementSpeed = 0.1f;
        camera.setPosition({ 0.0f, 0.0f, 5.0f });
        camera.setRotation({ 0.0f, 0.0f, 0.0f });

        waitFences.resize(renderAhead);
        presentCompleteSemaphores.resize(renderAhead);
        renderCompleteSemaphores.resize(renderAhead);
        commandBuffers.resize(swapChain.imageCount);
        uniformBufferParams.resize(swapChain.imageCount);

        // Command buffer execution fences
        for (auto &waitFence : waitFences) {
            VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
            VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &waitFence));
        }
        // Queue ordering semaphores
        for (auto &semaphore : presentCompleteSemaphores) {
            VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
        }
        for (auto &semaphore : renderCompleteSemaphores) {
            VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
        }
        // Command buffers
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
            cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdBufAllocateInfo.commandPool = cmdPool;
            cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdBufAllocateInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, commandBuffers.data()));
        }

        ui = new UIRender(vulkanDevice, renderPass, queue, pipelineCache, settings.sampleCount);
        skybox = new SkyboxRender(vulkanDevice, swapChain.imageCount, renderPass, queue,
                pipelineCache, settings.sampleCount, &textures, &camera, &uniformBufferParams);

        loadAssets();
        generateBRDFLUT();
        prepareUniformBuffers();
        setupDescriptors();
        preparePipelines();

        updateOverlay();

        recordCommandBuffers();

        prepared = true;

        // Load brainstem.gltf model
        loadModel("./../data/models/BrainStem.gltf");
    }

    void onImGuizmo()
    {
        glm::mat4 view = camera.matrices.view;
        glm::mat4 proj = camera.matrices.perspective;

        if (static_cast<uint32_t>(mCurrentGizmoOperation) == 4 || selectedNode == nullptr)
            return;

        glm::mat4 matrix = selectedNode->localMatrix();
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        if (showGizmo) {
            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), mCurrentGizmoOperation, mCurrentGizmoMode, glm::value_ptr(matrix), NULL, NULL);
            if(ImGuizmo::IsUsing()) {
                glm::decompose(matrix, scale, rotation, translation, skew, perspective);
                selectedNode->translation = translation;
                selectedNode->scale = scale;
                selectedNode->rotation = rotation;

                vkglTF::Model *model = modelRenderer->getModel();
                for (auto& node : model->nodes) {
                    node->update();
                }

            }
        }
    }

    void showGltfNode(vkglTF::Node *node, std::string prefix)
    {
        if (node == selectedNode) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        }
        ImGui::Text("%s", (prefix + ICON_MDI_CUBE_OUTLINE + node->name).c_str());
        if (node == selectedNode) {
            ImGui::PopStyleColor();
        }

        if (ImGui::IsItemClicked()) {
            selectedNode = node;
            showGizmo  = true;
            LOGI("Selected Node : {}", node->name.c_str());
        }
        for (auto& child : node->children) {
            showGltfNode(child, prefix + "\t");
        }
    }

    void showModelTreePanel()
    {
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(window, &xscale, &yscale);
        vkglTF::Model *model = modelRenderer->getModel();
        ImGui::SetNextWindowPos(ImVec2(width / xscale - 200, 0));
        ImGui::SetNextWindowSize(ImVec2(200, height / yscale - 200), ImGuiCond_Always);
        ImGui::Begin(u8"glTF Model Tree View", nullptr, ImGuiWindowFlags_None);
        if (ui->header("Asset")) {
            if (!model->asset.copyright.empty()) {
                ImGui::Text("Copyright: %s", model->asset.copyright.c_str());
            }
            if (!model->asset.generator.empty()) {
                ImGui::Text("Generator: %s", model->asset.generator.c_str());
            }
            if (!model->asset.version.empty()) {
                ImGui::Text("Version: %s", model->asset.version.c_str());
            }
            if (!model->asset.minVersion.empty()) {
                ImGui::Text("MinVersion: %s", model->asset.minVersion.c_str());
            }
        }
        if (!model->extensions.empty()) {
            if (ui->header("Extension Used")) {
                for (auto extension : model->extensions) {
                    ImGui::Text("%s", extension.c_str());
                }
            }
        }
        if (!model->extensionsRequired.empty()) {
            if (ui->header("Extension Required")) {
                for (auto extension : model->extensionsRequired) {
                    ImGui::Text("%s", extension.c_str());
                }
            }
        }
        if (ui->header("Nodes")) {
            for (auto& node : model->nodes) {
                showGltfNode(node, "");
            }
        }
        ImGui::End();
    }

    void initSequencer(uint32_t index)
    {
        if (!modelRenderer) return;

        if (index >= modelRenderer->getModel()->animations.size()) {
            mySequence.animation = nullptr;
            return;
        }
        mySequence.animation = &modelRenderer->getModel()->animations[index];
        mySequence.mFrameMin = 0;
        mySequence.mFrameMax = (int)modelRenderer->getModel()->animations[index].samplers[0].inputs.size() - 1;
        modelRenderer->animationIndex = index + 1;
    }

    bool loadModel(std::string filename)
    {
        if (!filename.empty()) {
            vkDeviceWaitIdle(device);

            // TODO: Models Rendering
            GLTFRender *model = new GLTFRender(vulkanDevice, swapChain.imageCount, renderPass, queue,
                pipelineCache, settings.sampleCount, &textures, &camera, &uniformBufferParams);
            if(model->load(filename)) {
                // Caculate the distance of camera
                glm::mat4 aabb = model->getModel()->aabb;
                float bestScale = (1.0f / std::max(aabb[0][0], std::max(aabb[1][1], aabb[2][2]))) * 0.5f;
                float cameraDistance = 1.0f / bestScale;
                camera.movementSpeed = cameraDistance / 20.0f;
                camera.setPosition({ 0.0f, 0.0f, cameraDistance});
                camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, cameraDistance * 10.0f);

                // Release resource of last model
                selectedNode = nullptr; showGizmo = false;
                if (modelRenderer) {
                    delete modelRenderer;
                }

                // Add new model to modelRender
                modelRenderer = model;
                setupDescriptors();
                preparePipelines();
                initSequencer(0);
                return true;
            }
        }
        return false;
    }

    void showSequencer()
    {
        if (!modelRenderer || !mySequence.animation)
            return;
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(window, &xscale, &yscale);
        ImGui::SetNextWindowPos(ImVec2(0, height / yscale - 200));
        ImGui::SetNextWindowSize(ImVec2(width / xscale, 200));
        ImGui::Begin("Timeline");
        // let's create the sequencer
        static int selectedEntry = -1;
        static int firstFrame = 0;
        static bool expanded = true;
        static int currentFrame = 100;

        ImGui::PushItemWidth(130);

        // Animation 
        // std::vector<std::string> animationName = {};
        // for (auto& animation : modelRenderer->getModel()->animations)
        //     animationName.push_back(animation.name);
        // if (ui->combo(ICON_MDI_MOVIE "Animation", &modelRenderer->animationIndex, animationName)) {
        //     // TODO: when the animation changed, refresh the channels according to the new animation
        // }
        static std::string caption = ICON_MDI_PLAY "Play";
        if(ImGui::Button(caption.c_str(), ImVec2(80, 20))) {
            paused = !paused;
            if (paused) {
                caption = ICON_MDI_PLAY "Play";
            } else {
                caption = ICON_MDI_PAUSE "Pause";
            }
        }
        ImGui::SameLine();
        ImGui::InputInt("Frame ", &currentFrame);
        ImGui::SameLine();
        ImGui::InputInt("Frame Max", &mySequence.mFrameMax);
        ImGui::SameLine();
        ImGui::InputInt("Item", &selectedEntry);
        if (selectedEntry >= 0 && selectedEntry < mySequence.GetItemCount()) {
            uint32_t index = mySequence.animation->channels[selectedEntry].samplerIndex;
            // switch (mCurrentGizmoOperation) {
            // case ImGuizmo::OPERATION::TRANSLATE:
                ImGui::SameLine();
                ImGui::InputFloat3(ICON_MDI_ARROW_ALL "Location", glm::value_ptr(mySequence.animation->samplers[index].outputsVec4[currentFrame]));
            //     break;
            // case ImGuizmo::OPERATION::ROTATE:
                ImGui::SameLine();
                ImGui::InputFloat4(ICON_MDI_ROTATE_ORBIT "Rotation", glm::value_ptr(mySequence.animation->samplers[index].outputsVec4[currentFrame]));
            //     break;
            // case ImGuizmo::OPERATION::SCALE:
                ImGui::SameLine();
                ImGui::InputFloat3(ICON_MDI_ARROW_EXPAND_ALL "Scale", glm::value_ptr(mySequence.animation->samplers[index].outputsVec4[currentFrame]));
            //     break;
            // default:
            //     break;
            // }
        }
        ImGui::PopItemWidth();

        if (!paused) {
            if (animationTimer > mySequence.animation->samplers[0].inputs[currentFrame + 1])
                currentFrame++;
            if (currentFrame >= mySequence.mFrameMax) {
                currentFrame = 0;
            }
        }
        Sequencer(&mySequence, &currentFrame, &expanded, &selectedEntry, &firstFrame, ImSequencer::SEQUENCER_EDIT_ALL);
        // add a UI to edit that particular item
        if (selectedEntry != -1)
        {
            if (selectedEntry < mySequence.animation->channels.size()) {
                selectedNode = mySequence.animation->channels[selectedEntry].node;
                switch (mySequence.animation->channels[selectedEntry].path) {
                case vkglTF::AnimationChannel::PathType::TRANSLATION:
                    mCurrentGizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
                    break;
                case vkglTF::AnimationChannel::PathType::ROTATION:
                    mCurrentGizmoOperation = ImGuizmo::OPERATION::ROTATE;
                    break;
                case vkglTF::AnimationChannel::PathType::SCALE:
                    mCurrentGizmoOperation = ImGuizmo::OPERATION::SCALE;
                    break;
                default:
                    mCurrentGizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
                }
            }
        }
        if (paused) {
            animationTimer = mySequence.animation->samplers[0].inputs[currentFrame];
        }
        ImGui::End();
    }

    /*
        Update ImGui user interface
    */
    void updateOverlay()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 lastDisplaySize = io.DisplaySize;
        io.DisplaySize = ImVec2((float)width, (float)height);
        io.DeltaTime = frameTimer;

        io.MousePos = ImVec2(mousePos.x, mousePos.y);
        io.MouseDown[0] = mouseButtons.left;
        io.MouseDown[1] = mouseButtons.right;
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(window, &xscale, &yscale);

        ui->updateParameters();

        bool updateShaderParams = false;
        bool updateCBs = false;
        float scale = 1.0;

        ImGui::NewFrame();

        ImGuizmo::BeginFrame();
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Enable(showGizmo);
        ImGuizmo::SetRect(0, 0, width / xscale, height / yscale);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(200, height / yscale - 200), ImGuiCond_Always);
        ImGui::Begin(u8"Property Panel", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::PushItemWidth(100.0f * scale);

        ui->text("%.1d fps (%.2f ms)", lastFPS, (1000.0f / lastFPS));
        if(ImGui::Button(ICON_MDI_FOLDER_OPEN "Load glTF Model")) {
            std::string filename = "";
            std::vector<std::string> filelist = openFileDialog("Open glTF Model", "./../");
            if (!filelist.empty()) {
                updateCBs = loadModel(filelist[0]);
            }
        }

        // Environment
        if (ui->header("Skybox")) {
            if (ui->combo("Environment", selectedEnvironment, environments)) {
                vkDeviceWaitIdle(device);
                loadEnvironment(environments[selectedEnvironment]);
                setupDescriptors();
                updateCBs = true;
            }
            if (ui->checkbox("Background", &displayBackground)) {
                updateShaderParams = true;
            }
            if (ui->slider("Exposure", &shaderValuesParams.exposure, 0.1f, 10.0f)) {
                updateShaderParams = true;
            }
            if (ui->slider("Gamma", &shaderValuesParams.gamma, 0.1f, 4.0f)) {
                updateShaderParams = true;
            }
            if (ui->slider("IBL", &shaderValuesParams.scaleIBLAmbient, 0.0f, 1.0f)) {
                updateShaderParams = true;
            }
        }

        // Debug View
        if (ui->header("Debug view")) {
            const std::vector<std::string> debugNamesInputs = {
                "none", "Base color", "Normal", "Occlusion", "Emissive", "Metallic", "Roughness"
            };
            if (ui->combo("Inputs", &debugViewInputs, debugNamesInputs)) {
                shaderValuesParams.debugViewInputs = (float)debugViewInputs;
                updateShaderParams = true;
            }
            const std::vector<std::string> debugNamesEquation = {
                "none", "Diff (l,n)", "F (l,h)", "G (l,v,h)", "D (h)", "Specular"
            };
            if (ui->combo("PBR equation", &debugViewEquation, debugNamesEquation)) {
                shaderValuesParams.debugViewEquation = (float)debugViewEquation;
                updateShaderParams = true;
            }
        }

        if (modelRenderer) {
            if (ui->header("Gizmo")) {
                ImGui::Checkbox("Enable", &showGizmo);
                if (selectedNode) {
                    ImGui::Text("Selected Node: %s", selectedNode->name.c_str());
                    if (ImGui::RadioButton(ICON_MDI_ARROW_ALL, mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
                        mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
                    ImGui::SameLine();
                    if (ImGui::RadioButton(ICON_MDI_ROTATE_ORBIT, mCurrentGizmoOperation == ImGuizmo::ROTATE))
                        mCurrentGizmoOperation = ImGuizmo::ROTATE;
                    ImGui::SameLine();
                    if (ImGui::RadioButton(ICON_MDI_ARROW_EXPAND_ALL, mCurrentGizmoOperation == ImGuizmo::SCALE))
                        mCurrentGizmoOperation = ImGuizmo::SCALE;
                }
            }
            if (modelRenderer->getModel()->animations.size()>0) {
                std::vector<std::string> animationName = { "None" };
                for (auto& animation : modelRenderer->getModel()->animations)
                    animationName.push_back(animation.name);
                if (ui->header("Animations")) {
                    if(ui->combo(ICON_MDI_MOVIE "Animation", &modelRenderer->animationIndex, animationName)) {
                        if (modelRenderer->animationIndex > 0)
                            initSequencer(modelRenderer->animationIndex - 1);
                    }
                }
            }
        }


        ImGui::PopItemWidth();
        ImGui::End();

        // Show the tree of glTF model
        if (modelRenderer) {
            showModelTreePanel();
        }

        onImGuizmo();

        showSequencer();

        ImGui::Render();

        if(ui->updateBuffer(ImVec2(xscale, yscale))) {
            updateCBs = true;
        }

        if (lastDisplaySize.x != io.DisplaySize.x || lastDisplaySize.y != io.DisplaySize.y) {
            updateCBs = true;
        }

        if (updateCBs) {
            vkDeviceWaitIdle(device);
            recordCommandBuffers();
            vkDeviceWaitIdle(device);
        }

        if (updateShaderParams) {
            updateParams();
        }
    }

    virtual void render()
    {
        if (!prepared) {
            return;
        }

        updateOverlay();

        VkResult res;
        res = vkWaitForFences(device, 1, &waitFences[frameIndex], VK_TRUE, UINT64_MAX);
        if (res != VK_SUCCESS) {
            LOGE("vkWaitForFences result {}", res);
        }
        VK_CHECK_RESULT(vkResetFences(device, 1, &waitFences[frameIndex]));

        VkResult acquire = swapChain.acquireNextImage(presentCompleteSemaphores[frameIndex], &currentBuffer);
        if ((acquire == VK_ERROR_OUT_OF_DATE_KHR) || (acquire == VK_SUBOPTIMAL_KHR)) {
            windowResize();
        }
        else {
            VK_CHECK_RESULT(acquire);
        }

        // Update UBOs
        updateUniformBuffers();

        const VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pWaitDstStageMask = &waitDstStageMask;
        submitInfo.pWaitSemaphores = &presentCompleteSemaphores[frameIndex];
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderCompleteSemaphores[frameIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentBuffer];
        submitInfo.commandBufferCount = 1;
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, waitFences[frameIndex]));

        VkResult present = swapChain.queuePresent(queue, currentBuffer, renderCompleteSemaphores[frameIndex]);
        if (!((present == VK_SUCCESS) || (present == VK_SUBOPTIMAL_KHR))) {
            if (present == VK_ERROR_OUT_OF_DATE_KHR) {
                windowResize();
                return;
            } else {
                VK_CHECK_RESULT(present);
            }
        }

        frameIndex += 1;
        frameIndex %= renderAhead;

        if (!paused) {
            // TODO: Models Rendering
            animationTimer += frameTimer;
        }
        if (modelRenderer)
        {
            modelRenderer->render(animationTimer);
        }
        updateParams();
    
        if (camera.updated) {
            updateUniformBuffers();
        }
    }
};

VulkanExample *vulkanExample;

// OS specific macros for the example main entry points
#if defined(_WIN32_) || defined(_WIN64_) || defined(_WIN32)
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
#else
int main(const int argc, const char *argv[])
#endif
{
    initSpdlog();
    vulkanExample = new VulkanExample();
    vulkanExample->initVulkan();
    vulkanExample->setupWindow();
    vulkanExample->prepare();
    vulkanExample->renderLoop();
    delete(vulkanExample);
    shutdownSpdlog();
    return 0;
}

