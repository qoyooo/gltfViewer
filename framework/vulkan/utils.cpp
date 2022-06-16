#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <fstream>

#if !defined(_WIN32)
#include "dirent.h"
#endif

#include "logger.h"

VkPipelineShaderStageCreateInfo loadShader(VkDevice device, std::string filename, VkShaderStageFlagBits stage)
{
    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = stage;
    shaderStage.pName = "main";

    std::ifstream is("./../data/shaders/" + filename, std::ios::binary | std::ios::in | std::ios::ate);

    if (is.is_open()) {
        size_t size = is.tellg();
        is.seekg(0, std::ios::beg);
        char* shaderCode = new char[size];
        is.read(shaderCode, size);
        is.close();
        assert(size > 0);
        VkShaderModuleCreateInfo moduleCreateInfo{};
        moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleCreateInfo.codeSize = size;
        moduleCreateInfo.pCode = (uint32_t*)shaderCode;
        vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderStage.module);
        delete[] shaderCode;
    }
    else {
        LOGE("Error: Could not open shader file \"{}\"", filename);
        shaderStage.module = VK_NULL_HANDLE;
    }

    assert(shaderStage.module != VK_NULL_HANDLE);
    return shaderStage;
}

void readDirectory(const std::string& directory, const std::string &pattern,
    std::map<std::string, std::string> &filelist, bool recursive)
{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    std::string searchpattern(directory + "/" + pattern);
    WIN32_FIND_DATA data;
    HANDLE hFind;
    if ((hFind = FindFirstFile(searchpattern.c_str(), &data)) != INVALID_HANDLE_VALUE) {
        do {
            std::string filename(data.cFileName);
            filename.erase(filename.find_last_of("."), std::string::npos);
            filelist[filename] = directory + "/" + data.cFileName;
        } while (FindNextFile(hFind, &data) != 0);
        FindClose(hFind);
    }
    if (recursive) {
        std::string dirpattern = directory + "/*";
        if ((hFind = FindFirstFile(dirpattern.c_str(), &data)) != INVALID_HANDLE_VALUE) {
            do {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    char subdir[MAX_PATH];
                    strcpy(subdir, directory.c_str());
                    strcat(subdir, "/");
                    strcat(subdir, data.cFileName);
                    if ((strcmp(data.cFileName, ".") != 0) && (strcmp(data.cFileName, "..") != 0)) {
                        readDirectory(subdir, pattern, filelist, recursive);
                    }
                }
            } while (FindNextFile(hFind, &data) != 0);
            FindClose(hFind);
        }
    }
#else
    std::string patternExt = pattern;
    patternExt.erase(0, pattern.find_last_of("."));
    struct dirent *entry;
    DIR *dir = opendir(directory.c_str());
    if (dir == NULL) {
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            std::string filename(entry->d_name);
            if (filename.find(patternExt) != std::string::npos) {
                filename.erase(filename.find_last_of("."), std::string::npos);
                filelist[filename] = directory + "/" + entry->d_name;
            }
        }
        if (recursive && (entry->d_type == DT_DIR)) {
            std::string subdir = directory + "/" + entry->d_name;
            if ((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)) {
                readDirectory(subdir, pattern, filelist, recursive);
            }
        }
    }
    closedir(dir);
#endif
}