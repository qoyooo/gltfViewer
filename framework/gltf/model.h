/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Load the glTF model
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#pragma once

#include <string>
#include <vector>

#include "vulkan/vulkan.h"
#include "vulkan/device.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <gli/gli.hpp>

// ERROR is already defined in wingdi.h and collides with a define in the Draco headers
#if defined(_WIN32) && defined(ERROR) && defined(TINYGLTF_ENABLE_DRACO) 
#undef ERROR
#endif

// #define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include "tiny_gltf.h"

// Changing this value here also requires changing it in the vertex shader
#define MAX_NUM_JOINTS 128u

namespace vkglTF
{

    struct Node;

    struct BoundingBox {
        glm::vec3 min;
        glm::vec3 max;
        bool valid = false;
        BoundingBox();
        BoundingBox(glm::vec3 min, glm::vec3 max);
        BoundingBox getAABB(glm::mat4 m);
    };

    /*
        glTF texture sampler
    */
    struct TextureSampler {
        VkFilter magFilter;
        VkFilter minFilter;
        VkSamplerAddressMode addressModeU;
        VkSamplerAddressMode addressModeV;
        VkSamplerAddressMode addressModeW;
    };

    /*
        glTF texture loading class
    */
    struct Texture {
        xy::VulkanDevice *device;
        VkImage image;
        VkImageLayout imageLayout;
        VkDeviceMemory deviceMemory;
        VkImageView view;
        uint32_t width, height;
        uint32_t mipLevels;
        uint32_t layerCount;
        VkDescriptorImageInfo descriptor;
        VkSampler sampler;

        void updateDescriptor();

        void destroy();

        /*
            Load a texture from a glTF image (stored as vector of chars loaded via stb_image)
            Also generates the mip chain as glTF images are stored as jpg or png without any mips
        */
        void fromglTfImage(tinygltf::Image &gltfimage, TextureSampler textureSampler,
            xy::VulkanDevice *device, VkQueue copyQueue);
    };

    /*
        glTF material class
    */
    struct Material {        
        enum AlphaMode{ ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
        AlphaMode alphaMode = ALPHAMODE_OPAQUE;
        float alphaCutoff = 1.0f;
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        glm::vec4 baseColorFactor = glm::vec4(1.0f);
        glm::vec4 emissiveFactor = glm::vec4(1.0f);
        vkglTF::Texture *baseColorTexture;
        vkglTF::Texture *metallicRoughnessTexture;
        vkglTF::Texture *normalTexture;
        vkglTF::Texture *occlusionTexture;
        vkglTF::Texture *emissiveTexture;
        struct TexCoordSets {
            uint8_t baseColor = 0;
            uint8_t metallicRoughness = 0;
            uint8_t specularGlossiness = 0;
            uint8_t normal = 0;
            uint8_t occlusion = 0;
            uint8_t emissive = 0;
        } texCoordSets;
        struct Extension {
            vkglTF::Texture *specularGlossinessTexture;
            vkglTF::Texture *diffuseTexture;
            glm::vec4 diffuseFactor = glm::vec4(1.0f);
            glm::vec3 specularFactor = glm::vec3(0.0f);
        } extension;
        struct PbrWorkflows {
            bool metallicRoughness = true;
            bool specularGlossiness = false;
        } pbrWorkflows;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    /*
        glTF primitive
    */
    struct Primitive {
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t vertexCount;
        Material &material;
        bool hasIndices;

        BoundingBox bb;

        Primitive(uint32_t firstIndex, uint32_t indexCount, uint32_t vertexCount,
            Material &material);

        void setBoundingBox(glm::vec3 min, glm::vec3 max);
    };

    /*
        glTF mesh
    */
    struct Mesh {
        xy::VulkanDevice *device;

        std::vector<Primitive*> primitives;

        BoundingBox bb;
        BoundingBox aabb;

        struct UniformBuffer {
            VkBuffer buffer;
            VkDeviceMemory memory;
            VkDescriptorBufferInfo descriptor;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
            void *mapped;
        } uniformBuffer;

        struct UniformBlock {
            glm::mat4 matrix;
            glm::mat4 jointMatrix[MAX_NUM_JOINTS]{};
            float jointcount { 0 };
        } uniformBlock;

        Mesh(xy::VulkanDevice *device, glm::mat4 matrix);

        ~Mesh();

        void setBoundingBox(glm::vec3 min, glm::vec3 max);
    };

    /*
        glTF skin
    */
    struct Skin {
        std::string name;
        Node *skeletonRoot = nullptr;
        std::vector<glm::mat4> inverseBindMatrices;
        std::vector<Node*> joints;
    };

    /*
        glTF node
    */
    struct Node {
        Node *parent;
        uint32_t index;
        bool visible{false};
        std::vector<Node*> children;
        glm::mat4 matrix;
        std::string name;
        Mesh *mesh;
        Skin *skin;
        int32_t skinIndex = -1;
        glm::vec3 translation{};
        glm::vec3 scale{ 1.0f };
        glm::quat rotation{};
        BoundingBox bvh;
        BoundingBox aabb;

        glm::mat4 localMatrix();

        glm::mat4 getMatrix();

        void update();

        ~Node();
    };

    /*
        glTF animation channel
    */
    struct AnimationChannel {
        enum PathType { TRANSLATION, ROTATION, SCALE };
        PathType path;
        Node *node;
        uint32_t samplerIndex;
    };

    /*
        glTF animation sampler
    */
    struct AnimationSampler {
        enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
        InterpolationType interpolation;
        std::vector<float> inputs;
        std::vector<glm::vec4> outputsVec4;
    };

    /*
        glTF animation
    */
    struct Animation {
        std::string name;
        std::vector<AnimationSampler> samplers;
        std::vector<AnimationChannel> channels;
        float start = std::numeric_limits<float>::max();
        float end = std::numeric_limits<float>::min();
    };

    /*
        glTF model loading and rendering class
    */
    struct Model {

        std::string name;

        xy::VulkanDevice *device;

        struct Vertex {
            glm::vec3 pos;
            glm::vec3 normal;
            glm::vec2 uv0;
            glm::vec2 uv1;
            glm::vec4 joint0;
            glm::vec4 weight0;
        };

        struct {
            std::string copyright;
            std::string generator;
            std::string version;
            std::string minVersion;
        } asset;

        struct Vertices {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory;
        } vertices;

        struct Indices {
            int count;
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory;
        } indices;

        glm::mat4 aabb;

        std::vector<Node*> nodes;
        std::vector<Node*> linearNodes;

        std::vector<Skin*> skins;

        std::vector<Texture> textures;
        std::vector<TextureSampler> textureSamplers;
        std::vector<Material> materials;
        std::vector<Animation> animations;

        std::vector<std::string> extensions;
        std::vector<std::string> extensionsRequired;

        struct Dimensions {
            glm::vec3 min = glm::vec3(FLT_MAX);
            glm::vec3 max = glm::vec3(-FLT_MAX);
        } dimensions;

        void destroy(VkDevice device);

        void loadNode(vkglTF::Node *parent, const tinygltf::Node &node, uint32_t nodeIndex,
            const tinygltf::Model &model, std::vector<uint32_t>& indexBuffer,
            std::vector<Vertex>& vertexBuffer, float globalscale);

        void loadSkins(tinygltf::Model &gltfModel);

        void loadTextures(tinygltf::Model &gltfModel, xy::VulkanDevice *device, VkQueue transferQueue);

        VkSamplerAddressMode getVkWrapMode(int32_t wrapMode);

        VkFilter getVkFilterMode(int32_t filterMode);

        void loadTextureSamplers(tinygltf::Model &gltfModel);

        void loadMaterials(tinygltf::Model &gltfModel);

        void loadAnimations(tinygltf::Model &gltfModel);

        void loadFromFile(std::string filename, xy::VulkanDevice *device, VkQueue transferQueue, float scale = 1.0f);

        void drawNode(Node *node, VkCommandBuffer commandBuffer);

        void draw(VkCommandBuffer commandBuffer);

        void calculateBoundingBox(Node *node, Node *parent);

        void getSceneDimensions();

        void updateAnimation(uint32_t index, float time);

        /*
            Helper functions
        */
        Node* findNode(Node *parent, uint32_t index);

        Node* nodeFromIndex(uint32_t index);
    };
}