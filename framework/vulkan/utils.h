
/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define Buffer of Vulkan
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#pragma once

#include <map>
#include <string>

#include <vulkan/vulkan.h>

VkPipelineShaderStageCreateInfo loadShader(VkDevice device, std::string filename, VkShaderStageFlagBits stage);

void readDirectory(const std::string& directory, const std::string &pattern,
    std::map<std::string, std::string> &filelist, bool recursive);