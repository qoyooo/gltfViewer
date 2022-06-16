/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define the common macro
 * Version:
 *      2021.02.10  initial
 *
 */

#pragma once

#include "vulkan/vulkan.h"
#include "logger.h"

#define VK_CHECK_RESULT(f)                                                              \
{                                                                                       \
    VkResult res = (f);                                                                 \
    if (res != VK_SUCCESS)                                                              \
    {                                                                                   \
        LOGE("Fatal : VkResult is \"{}\" in {} at line {}.", res, __FILE__, __LINE__);  \
        assert(res == VK_SUCCESS);                                                      \
    }                                                                                   \
}

#define GET_INSTANCE_PROC_ADDR(inst, entrypoint)                        \
{                                                                       \
    fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetInstanceProcAddr(inst, "vk"#entrypoint)); \
    if (fp##entrypoint == NULL)                                         \
    {                                                                    \
        exit(1);                                                        \
    }                                                                   \
}

#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                           \
{                                                                       \
    fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetDeviceProcAddr(dev, "vk"#entrypoint));   \
    if (fp##entrypoint == NULL)                                         \
    {                                                                    \
        exit(1);                                                        \
    }                                                                   \
}
