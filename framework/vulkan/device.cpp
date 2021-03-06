/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define Device of Vulkan
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#include "vulkan/device.h"

#include "macros.h"
#include "logger.h"

namespace xy
{

    VulkanDevice::VulkanDevice(VkPhysicalDevice physicalDevice)
    {
        assert(physicalDevice);
        this->physicalDevice = physicalDevice;

        // Store Properties features, limits and properties of the physical device for later use
        // Device properties also contain limits and sparse properties
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        // Features should be checked by the examples before using them
        vkGetPhysicalDeviceFeatures(physicalDevice, &features);
        // Memory properties are used regularly for creating all kinds of buffers
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        // Queue family properties, used for setting up requested queues upon device creation
        uint32_t queueFamilyCount;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        assert(queueFamilyCount > 0);
        queueFamilyProperties.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
    }

    VulkanDevice::~VulkanDevice()
    {
        if (commandPool) {
            vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
        }
        if (logicalDevice) {
            vkDestroyDevice(logicalDevice, nullptr);
        }
    }

    uint32_t VulkanDevice::getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound)
    {
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
            if ((typeBits & 1) == 1) {
                if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    if (memTypeFound) {
                        *memTypeFound = true;
                    }
                    return i;
                }
            }
            typeBits >>= 1;
        }

        if (memTypeFound) {
            *memTypeFound = false;
            return 0;
        } else {
            LOGE("Could not find a matching memory type");
            throw std::runtime_error("Could not find a matching memory type");
        }
    }


    uint32_t VulkanDevice::getQueueFamilyIndex(VkQueueFlagBits queueFlags)
    {
        // Dedicated queue for compute
        // Try to find a queue family index that supports compute but not graphics
        if (queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
                if ((queueFamilyProperties[i].queueFlags & queueFlags) && ((queueFamilyProperties[i].queueFlags
                    & VK_QUEUE_GRAPHICS_BIT) == 0))
                {
                    return i;
                    break;
                }
            }
        }

        // For other queue types or if no separate compute queue is present, return the first one to
        // support the requested flags
        for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
            if (queueFamilyProperties[i].queueFlags & queueFlags) {
                return i;
                break;
            }
        }
        LOGE("Could not find a matching queue family index");
        throw std::runtime_error("Could not find a matching queue family index");
    }

    VkResult VulkanDevice::createLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures,
        std::vector<const char*> enabledExtensions, VkQueueFlags requestedQueueTypes)
    {            
        // Desired queues need to be requested upon logical device creation
        // Due to differing queue family configurations of Vulkan implementations this can be a bit tricky,
        // especially if the applicationrequests different queue types

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

        // Get queue family indices for the requested queue family types
        // Note that the indices may overlap depending on the implementation

        const float defaultQueuePriority(0.0f);

        // Graphics queue
        if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT) {
            queueFamilyIndices.graphics = getQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamilyIndices.graphics;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos.push_back(queueInfo);
        } else {
            queueFamilyIndices.graphics = VK_NULL_HANDLE;
        }

        // Dedicated compute queue
        if (requestedQueueTypes & VK_QUEUE_COMPUTE_BIT) {
            queueFamilyIndices.compute = getQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
            if (queueFamilyIndices.compute != queueFamilyIndices.graphics) {
                // If compute family index differs, we need an additional queue create info for the compute queue
                VkDeviceQueueCreateInfo queueInfo{};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = queueFamilyIndices.compute;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &defaultQueuePriority;
                queueCreateInfos.push_back(queueInfo);
            }
        } else {
            // Else we use the same queue
            queueFamilyIndices.compute = queueFamilyIndices.graphics;
        }

        // Create the logical device representation
        std::vector<const char*> deviceExtensions(enabledExtensions);
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());;
        deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceCreateInfo.pEnabledFeatures = &enabledFeatures;

        if (deviceExtensions.size() > 0) {
            deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
            deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        }

        VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &logicalDevice);

        if (result == VK_SUCCESS) {
            commandPool = createCommandPool(queueFamilyIndices.graphics);
        }

        this->enabledFeatures = enabledFeatures;

        return result;
    }

    VkResult VulkanDevice::createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags,
        VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory, void *data)
    {
        // Create the buffer handle
        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.usage = usageFlags;
        bufferCreateInfo.size = size;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK_RESULT(vkCreateBuffer(logicalDevice, &bufferCreateInfo, nullptr, buffer));

        // Create the memory backing up the buffer handle
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo memAlloc{};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        vkGetBufferMemoryRequirements(logicalDevice, *buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        // Find a memory type index that fits the properties of the buffer
        memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);
        VK_CHECK_RESULT(vkAllocateMemory(logicalDevice, &memAlloc, nullptr, memory));
        
        // If a pointer to the buffer data has been passed, map the buffer and copy over the data
        if (data != nullptr)
        {
            void *mapped;
            VK_CHECK_RESULT(vkMapMemory(logicalDevice, *memory, 0, size, 0, &mapped));
            memcpy(mapped, data, size);
            // If host coherency hasn't been requested, do a manual flush to make writes visible
            if ((memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
            {
                VkMappedMemoryRange mappedRange{};
                mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                mappedRange.memory = *memory;
                mappedRange.offset = 0;
                mappedRange.size = size;
                vkFlushMappedMemoryRanges(logicalDevice, 1, &mappedRange);
            }
            vkUnmapMemory(logicalDevice, *memory);
        }

        // Attach the memory to the buffer object
        VK_CHECK_RESULT(vkBindBufferMemory(logicalDevice, *buffer, *memory, 0));

        return VK_SUCCESS;
    }

    VkCommandPool VulkanDevice::createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags)
    {
        VkCommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
        cmdPoolInfo.flags = createFlags;
        VkCommandPool cmdPool;
        VK_CHECK_RESULT(vkCreateCommandPool(logicalDevice, &cmdPoolInfo, nullptr, &cmdPool));
        return cmdPool;
    }

    VkCommandBuffer VulkanDevice::createCommandBuffer(VkCommandBufferLevel level, bool begin)
    {
        VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = commandPool;
        cmdBufAllocateInfo.level = level;
        cmdBufAllocateInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(logicalDevice, &cmdBufAllocateInfo, &cmdBuffer));

        // If requested, also start recording for the new command buffer
        if (begin) {
            VkCommandBufferBeginInfo commandBufferBI{};
            commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &commandBufferBI));
        }

        return cmdBuffer;
    }

    void VulkanDevice::beginCommandBuffer(VkCommandBuffer commandBuffer)
    {
        VkCommandBufferBeginInfo commandBufferBI{};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBI));
    }


    void VulkanDevice::flushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free)
    {            
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        // Create fence to ensure that the command buffer has finished executing
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence));
        
        // Submit to the queue
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
        // Wait for the fence to signal that command buffer has finished executing
        VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, 100000000000));

        vkDestroyFence(logicalDevice, fence, nullptr);

        if (free) {
            vkFreeCommandBuffers(logicalDevice, commandPool, 1, &commandBuffer);
        }
    }

}  //namespace xy
