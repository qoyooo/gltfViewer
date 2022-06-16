/*
* glTFViewer - File Open Dialog
*
* Copyright (C) 2021-2021 by Xuanyi Technology Corp.
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "filedialog.h"
#include "tinyfiledialogs/tinyfiledialogs.h"

std::vector<std::string> openFileDialog(char const * const aTitle, char const * const aDefaultPathAndFile)
{
    std::vector<std::string> fileList;
    char const * lFilterPatterns[2] = { "*.gltf", "*.glb" };
    char const * lTheOpenFileName = tinyfd_openFileDialog(aTitle, aDefaultPathAndFile, 2, lFilterPatterns, NULL, 0);
    if(lTheOpenFileName) {
        fileList.push_back(std::string(lTheOpenFileName));
    }
    return fileList;
}