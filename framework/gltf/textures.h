

/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define texture for Vulkan
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#pragma once

#include "vulkan/texture2d.h"
#include "vulkan/texturecube.h"

namespace xy
{

    struct Textures {
        TextureCubeMap environmentCube;
        Texture2D empty;
        Texture2D lutBrdf;
        TextureCubeMap irradianceCube;
        TextureCubeMap prefilteredCube;
    };

}