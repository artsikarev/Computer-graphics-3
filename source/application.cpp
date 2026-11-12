#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <algorithm>
#include <imgui.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "application.hpp"

namespace application {
    namespace {

        float rotation_angle = 0.0f;
        float rotation_speed = 1.0f;
        float cube_position_x = 0.0f;
        float cube_position_y = 0.0f;
        float cube_position_z = 0.0f;
        float cube_scale = 1.0f;
        bool use_perspective = true;

        float camera_x = 0.0f;
        float camera_y = 0.0f;
        float camera_z = 0.0f;
        GLFWwindow* app_window = nullptr;
        float camera_move_speed = 2.5f;
        double previous_frame_time = 0.0;
        float camera_yaw = -1.5707963f;
        float camera_pitch = 0.0f;
        float camera_fov = 1.0471976f;

        struct Mat4 { float m[16]{}; };

        Mat4 identityMatrix() { Mat4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }
        Mat4 scaleMatrix(float s) { Mat4 r{}; r.m[0] = r.m[5] = r.m[10] = s; r.m[15] = 1.0f; return r; }

        Mat4 multiplyMatrices(const Mat4& a, const Mat4& b) {
            Mat4 r{};
            for (int c = 0; c < 4; ++c) for (int row = 0; row < 4; ++row) for (int k = 0; k < 4; ++k)
                r.m[c * 4 + row] += a.m[k * 4 + row] * b.m[c * 4 + k];
            return r;
        }
        Mat4 rotationY(float a) {
            Mat4 r = identityMatrix(); float c = std::cos(a), s = std::sin(a);
            r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r;
        }
        Mat4 translationMatrix(float x, float y, float z) { Mat4 r = identityMatrix(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }

        Mat4 perspectiveMatrix(float fov, float aspect, float n, float f) {
            Mat4 r{}; float t = 1.0f / std::tan(fov * 0.5f);
            r.m[0] = t / aspect; r.m[5] = -t; r.m[10] = f / (n - f); r.m[11] = -1.0f; r.m[14] = (n * f) / (n - f); return r;
        }
        Mat4 orthographicMatrix(float l, float r_, float b, float t, float n, float f) {
            Mat4 r = identityMatrix(); r.m[0] = 2 / (r_ - l); r.m[5] = -2 / (t - b); r.m[10] = 1 / (n - f);
            r.m[12] = -(r_ + l) / (r_ - l); r.m[13] = (t + b) / (t - b); r.m[14] = n / (n - f); return r;
        }

        struct Vec3 { float x, y, z; };
        Vec3 subtractVec3(Vec3 a, Vec3 b) { return { a.x - b.x,a.y - b.y,a.z - b.z }; }
        Vec3 normalizeVec3(Vec3 v) {
            float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (l < 1e-6f) return { 0,0,0 }; return { v.x / l,v.y / l,v.z / l };
        }
        Vec3 crossVec3(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y,a.z * b.x - a.x * b.z,a.x * b.y - a.y * b.x }; }
        float dotVec3(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

        Mat4 lookAtMatrix(Vec3 eye, Vec3 target, Vec3 up) {
            Vec3 f = normalizeVec3(subtractVec3(target, eye));
            Vec3 r = normalizeVec3(crossVec3(f, up));
            Vec3 u = crossVec3(r, f);
            Mat4 m = identityMatrix();
            m.m[0] = r.x; m.m[1] = u.x; m.m[2] = -f.x;
            m.m[4] = r.y; m.m[5] = u.y; m.m[6] = -f.y;
            m.m[8] = r.z; m.m[9] = u.z; m.m[10] = -f.z;
            m.m[12] = -dotVec3(r, eye); m.m[13] = -dotVec3(u, eye); m.m[14] = dotVec3(f, eye);
            return m;
        }

        Mat4 createModel(float angle) {
            return multiplyMatrices(
                translationMatrix(cube_position_x, cube_position_y, cube_position_z - 2.0f),
                multiplyMatrices(rotationY(angle), scaleMatrix(cube_scale)));
        }
        Mat4 createView() {
            Vec3 pos{ camera_x,camera_y,camera_z };
            Vec3 dir{ std::cos(camera_yaw) * std::cos(camera_pitch),
                      std::sin(camera_pitch),
                      std::sin(camera_yaw) * std::cos(camera_pitch) };
            Vec3 target{ pos.x + dir.x,pos.y + dir.y,pos.z + dir.z };
            return lookAtMatrix(pos, target, { 0,1,0 });
        }
        Mat4 createProjection(float aspect, bool perspective) {
            if (perspective) return perspectiveMatrix(camera_fov, aspect, 0.1f, 100.0f);
            float hh = 1.5f, hw = hh * aspect;
            return orthographicMatrix(-hw, hw, -hh, hh, 0.1f, 100.0f);
        }

        struct LightingSettings {
            float light_direction[3] = { -1.0f, 1.0f, 1.0f };
            float light_color[3] = { 1.0f, 1.0f, 1.0f };
            float light_intensity = 1.0f;
            float point_position[3] = { 2.0f, 2.0f, 2.0f };
            float point_color[3] = { 1.0f, 0.4f, 0.2f };
            float point_intensity = 5.0f;
            bool point_enabled = true;
            float spot_position[3] = { 0.0f, 2.0f, 2.0f };
            float spot_direction[3] = { 0.0f, -1.0f, -1.0f };
            float spot_color[3] = { 0.3f, 0.6f, 1.0f };
            float spot_intensity = 8.0f;
            float spot_inner_angle = 15.0f;
            float spot_outer_angle = 25.0f;
            bool spot_enabled = true;
            float ambient_color[3] = { 1.0f, 1.0f, 1.0f };
            float ambient_intensity = 0.15f;
            float albedo[3] = { 0.2f, 0.7f, 1.0f };
            float specular_color[3] = { 1.0f, 1.0f, 1.0f };
            float shininess = 32.0f;
        };
        LightingSettings lighting;

        constexpr uint32_t MAX_POINT_LIGHTS = 8;
        constexpr uint32_t MAX_SPOT_LIGHTS = 8;

        struct alignas(16) PointLightGPU { alignas(16) float position[4]; alignas(16) float color_intensity[4]; };
        struct alignas(16) SpotLightGPU {
            alignas(16) float position[4];
            alignas(16) float direction[4];
            alignas(16) float color_intensity[4];
            alignas(16) float angles[4];
        };
        struct alignas(16) PointLightsBuffer { alignas(16) uint32_t count; uint32_t padding[3]{}; PointLightGPU lights[MAX_POINT_LIGHTS]{}; };
        struct alignas(16) SpotLightsBuffer { alignas(16) uint32_t count; uint32_t padding[3]{}; SpotLightGPU  lights[MAX_SPOT_LIGHTS]{}; };

        struct alignas(16) LightingUniform {
            alignas(16) float light_direction[4];
            alignas(16) float light_color_intensity[4];
            alignas(16) float ambient_color_intensity[4];
            alignas(16) float specular_color_shininess[4];
            alignas(16) float camera_position[4];
            alignas(16) float point_position[4];
            alignas(16) float point_color_intensity[4];
        };


        struct alignas(16) ModelUBO {
            Mat4 model;
            Mat4 mvp;
        };

        VkShaderModule vertex_shader = VK_NULL_HANDLE;
        VkShaderModule fragment_shader = VK_NULL_HANDLE;
        VkPipeline graphics_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

        VkBuffer lighting_buffer = VK_NULL_HANDLE;
        VmaAllocation lighting_buffer_allocation = VK_NULL_HANDLE;
        void* lighting_buffer_mapped = nullptr;

        VkBuffer point_lights_buffer = VK_NULL_HANDLE;
        VmaAllocation point_lights_buffer_allocation = VK_NULL_HANDLE;
        void* point_lights_buffer_mapped = nullptr;

        VkBuffer spot_lights_buffer = VK_NULL_HANDLE;
        VmaAllocation spot_lights_buffer_allocation = VK_NULL_HANDLE;
        void* spot_lights_buffer_mapped = nullptr;

        // model UBO (dynamic)
        constexpr uint32_t MAX_MODELS = 64;
        VkBuffer model_ubo_buffer = VK_NULL_HANDLE;
        VmaAllocation model_ubo_allocation = VK_NULL_HANDLE;
        void* model_ubo_mapped = nullptr;
        VkDeviceSize model_ubo_stride = 0;

        // descriptor sets
        VkDescriptorSetLayout lighting_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool lighting_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet lighting_descriptor_set = VK_NULL_HANDLE;

        VkDescriptorSetLayout point_lights_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool point_lights_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet point_lights_descriptor_set = VK_NULL_HANDLE;

        VkDescriptorSetLayout spot_lights_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool spot_lights_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet spot_lights_descriptor_set = VK_NULL_HANDLE;

        VkDescriptorSetLayout model_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool model_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet model_descriptor_set = VK_NULL_HANDLE;

        VkDescriptorSetLayout texture_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool texture_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet texture_descriptor_set = VK_NULL_HANDLE;


        struct Texture {
            VkImage image = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkSampler sampler = VK_NULL_HANDLE;
            std::string name;
        };
        std::vector<Texture> g_textures;
        int g_selected_texture = 0;

        struct Vertex {
            float position[3];
            float normal[3];
            float uv[2];
        };

        const VkVertexInputBindingDescription vertex_binding = {
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        const VkVertexInputAttributeDescription vertex_attributes[] = {
            {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
            {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal)   },
            {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex, uv)       },
        };


        const std::array<uint32_t, 36> cube_indices = { {
            0,2,1, 0,3,2,       // задняя
            4,5,6, 4,6,7,       // передняя
            8,10,9, 8,11,10,    // нижняя
            12,13,14, 12,14,15, // верхняя
            16,17,18, 16,18,19, // левая
            20,21,22, 20,22,23  // правая
        } };

        std::array<Vertex, 24> cube_vertices = [] {
            std::array<Vertex, 24> v{};
            const float pos[24][3] = {
                {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
                {-0.5f,-0.5f, 0.5f},{0.5f,-0.5f, 0.5f},{0.5f,0.5f, 0.5f},{-0.5f,0.5f, 0.5f},
                {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f},
                {-0.5f, 0.5f,-0.5f},{0.5f, 0.5f,-0.5f},{0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},
                {-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},
                { 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},
            };

            const float uv_default[4][2] = { {0,0},{1,0},{1,1},{0,1} };

            const float uv_rotated[4][2] = { {1,0},{1,1},{0,1},{0,0} };

            for (int f = 0; f < 6; ++f)
                for (int k = 0; k < 4; ++k) {
                    v[f * 4 + k].position[0] = pos[f * 4 + k][0];
                    v[f * 4 + k].position[1] = pos[f * 4 + k][1];
                    v[f * 4 + k].position[2] = pos[f * 4 + k][2];
                    v[f * 4 + k].normal[0] = v[f * 4 + k].normal[1] = v[f * 4 + k].normal[2] = 0.0f;

                    const float (*uvsrc)[2] = (f == 5) ? uv_rotated : uv_default;
                    v[f * 4 + k].uv[0] = uvsrc[k][0];
                    v[f * 4 + k].uv[1] = uvsrc[k][1];
                }
            // нормали по треугольникам
            for (size_t face = 0; face < 6; ++face) {
                const size_t base = face * 6;
                uint32_t i0 = cube_indices[base], i1 = cube_indices[base + 1], i2 = cube_indices[base + 2];
                Vec3 p0{ v[i0].position[0],v[i0].position[1],v[i0].position[2] };
                Vec3 p1{ v[i1].position[0],v[i1].position[1],v[i1].position[2] };
                Vec3 p2{ v[i2].position[0],v[i2].position[1],v[i2].position[2] };
                Vec3 n = normalizeVec3(crossVec3(subtractVec3(p1, p0), subtractVec3(p2, p0)));
                for (size_t j = 0; j < 6; ++j) {
                    uint32_t idx = cube_indices[base + j];
                    v[idx].normal[0] = n.x; v[idx].normal[1] = n.y; v[idx].normal[2] = n.z;
                }
            }
            return v;
            }();

        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VmaAllocation vertex_buffer_allocation = VK_NULL_HANDLE;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        VmaAllocation index_buffer_allocation = VK_NULL_HANDLE;


        std::vector<char> readShaderFile(const char* filename) {
            std::ifstream file(filename, std::ios::ate | std::ios::binary);
            if (!file.is_open()) return {};
            size_t size = (size_t)file.tellg();
            std::vector<char> buf(size);
            file.seekg(0); file.read(buf.data(), size);
            return buf;
        }
        VkShaderModule createShaderModule(const std::vector<char>& code) {
            if (code.empty() || code.size() % 4 != 0) return VK_NULL_HANDLE;
            VkShaderModuleCreateInfo ci{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t*>(code.data()) };
            VkShaderModule mod = VK_NULL_HANDLE;
            if (vkCreateShaderModule(graphics::internal::context.device, &ci, nullptr, &mod) != VK_SUCCESS)
                return VK_NULL_HANDLE;
            return mod;
        }
        // ram
        bool createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
            VkBuffer& buffer, VmaAllocation& alloc, void** mapped = nullptr) {
            auto& ctx = graphics::internal::context;
            VkBufferCreateInfo bi{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
            VmaAllocationCreateInfo ai{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                                VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                        .usage = VMA_MEMORY_USAGE_AUTO };
            VmaAllocationInfo outInfo{};
            VkResult r = vmaCreateBuffer(ctx.allocator, &bi, &ai, &buffer, &alloc, &outInfo);
            if (r != VK_SUCCESS) return false;
            if (mapped) *mapped = outInfo.pMappedData;
            return true;
        }

        // vram
        bool createDeviceLocalBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
            VkBuffer& buffer, VmaAllocation& alloc) {
            auto& ctx = graphics::internal::context;
            VkBufferCreateInfo bi{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
            VmaAllocationCreateInfo ai{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
            return vmaCreateBuffer(ctx.allocator, &bi, &ai, &buffer, &alloc, nullptr) == VK_SUCCESS;
        }

        VkCommandBuffer beginOneTimeCommands(VkCommandPool& outPool) {
            auto& ctx = graphics::internal::context;

            VkCommandPoolCreateInfo pci{
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                .queueFamilyIndex = ctx.graphics_queue_index
            };
            if (vkCreateCommandPool(ctx.device, &pci, nullptr, &outPool) != VK_SUCCESS)
                return VK_NULL_HANDLE;

            VkCommandBufferAllocateInfo ai{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = outPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1
            };
            VkCommandBuffer cb = VK_NULL_HANDLE;
            vkAllocateCommandBuffers(ctx.device, &ai, &cb);

            VkCommandBufferBeginInfo bi{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
            };
            vkBeginCommandBuffer(cb, &bi);
            return cb;
        }

        void endOneTimeCommands(VkCommandBuffer cb, VkCommandPool pool) {
            auto& ctx = graphics::internal::context;
            vkEndCommandBuffer(cb);

            VkSubmitInfo si{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &cb
            };
            vkQueueSubmit(ctx.graphics_queue, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(ctx.graphics_queue);

            vkFreeCommandBuffers(ctx.device, pool, 1, &cb);
            vkDestroyCommandPool(ctx.device, pool, nullptr);
        }

        // ram - vram
        void uploadBufferViaStaging(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
            VkBuffer& dst, VmaAllocation& dstAlloc) {
            auto& ctx = graphics::internal::context;

            VkBuffer staging = VK_NULL_HANDLE;
            VmaAllocation stagingAlloc = VK_NULL_HANDLE;
            void* mapped = nullptr;
            createHostBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, stagingAlloc, &mapped);
            std::memcpy(mapped, data, (size_t)size);

            createDeviceLocalBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dst, dstAlloc);

            VkCommandPool pool = VK_NULL_HANDLE;
            VkCommandBuffer cb = beginOneTimeCommands(pool);

            VkBufferCopy copy{ .srcOffset = 0, .dstOffset = 0, .size = size };
            vkCmdCopyBuffer(cb, staging, dst, 1, &copy);

            VkBufferMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = dst,
                .offset = 0,
                .size = size
            };
            vkCmdPipelineBarrier(cb,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                0, 0, nullptr, 1, &barrier, 0, nullptr);

            endOneTimeCommands(cb, pool);
            vmaDestroyBuffer(ctx.allocator, staging, stagingAlloc);
        }

        // загрузка текстуры
        bool createTextureFromFile(const char* path, Texture& tex) {
            auto& ctx = graphics::internal::context;

            int w = 0, h = 0, ch = 0;
            stbi_set_flip_vertically_on_load(1);
            stbi_uc* pixels = stbi_load(path, &w, &h, &ch, STBI_rgb_alpha);
            if (!pixels) {
                std::cerr << "stbi_load failed: " << path << "\n";
                return false;
            }
            VkDeviceSize size = (VkDeviceSize)w * h * 4;

            // staging buffer (RAM) + copy пикселей
            VkBuffer staging = VK_NULL_HANDLE;
            VmaAllocation stagingAlloc = VK_NULL_HANDLE;
            void* mapped = nullptr;
            createHostBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, stagingAlloc, &mapped);
            std::memcpy(mapped, pixels, (size_t)size);
            stbi_image_free(pixels);

            // image (VRAM)
            VkImageCreateInfo imgInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = VK_FORMAT_R8G8B8A8_SRGB,
                .extent = { (uint32_t)w, (uint32_t)h, 1u },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
            };
            VmaAllocationCreateInfo ai{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
            if (vmaCreateImage(ctx.allocator, &imgInfo, &ai, &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
                vmaDestroyBuffer(ctx.allocator, staging, stagingAlloc);
                return false;
            }

            VkCommandPool pool = VK_NULL_HANDLE;
            VkCommandBuffer cb = beginOneTimeCommands(pool);

            VkImageMemoryBarrier b1{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = tex.image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            vkCmdPipelineBarrier(cb,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b1);

            VkBufferImageCopy region{
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .imageOffset = { 0, 0, 0 },
                .imageExtent = { (uint32_t)w, (uint32_t)h, 1u }
            };
            vkCmdCopyBufferToImage(cb, staging, tex.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            VkImageMemoryBarrier b2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = tex.image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            vkCmdPipelineBarrier(cb,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b2);

            endOneTimeCommands(cb, pool);
            vmaDestroyBuffer(ctx.allocator, staging, stagingAlloc);

            // image view
            VkImageViewCreateInfo vInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = tex.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VK_FORMAT_R8G8B8A8_SRGB,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            vkCreateImageView(ctx.device, &vInfo, nullptr, &tex.view);

            // sampler
            VkSamplerCreateInfo sInfo{
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .anisotropyEnable = VK_FALSE,
                .maxLod = 1.0f
            };
            vkCreateSampler(ctx.device, &sInfo, nullptr, &tex.sampler);

            return true;
        }

        bool createModelResources() {
            auto& ctx = graphics::internal::context;

            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(ctx.physical_device, &props);
            VkDeviceSize align = props.limits.minUniformBufferOffsetAlignment;
            model_ubo_stride = (sizeof(ModelUBO) + align - 1) & ~(align - 1);
            VkDeviceSize total = model_ubo_stride * MAX_MODELS;

            if (!createHostBuffer(total, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                model_ubo_buffer, model_ubo_allocation, &model_ubo_mapped)) {
                return false;
            }

            // layout
            VkDescriptorSetLayoutBinding b{ .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT };
            VkDescriptorSetLayoutCreateInfo li{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1, .pBindings = &b };
            if (vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &model_descriptor_set_layout) != VK_SUCCESS)
                return false;

            VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1 };
            VkDescriptorPoolCreateInfo pi{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
            if (vkCreateDescriptorPool(ctx.device, &pi, nullptr, &model_descriptor_pool) != VK_SUCCESS)
                return false;

            VkDescriptorSetAllocateInfo ai{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = model_descriptor_pool, .descriptorSetCount = 1,
                .pSetLayouts = &model_descriptor_set_layout };
            if (vkAllocateDescriptorSets(ctx.device, &ai, &model_descriptor_set) != VK_SUCCESS)
                return false;

            VkDescriptorBufferInfo bi{ .buffer = model_ubo_buffer, .offset = 0, .range = sizeof(ModelUBO) };
            VkWriteDescriptorSet w{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = model_descriptor_set, .dstBinding = 0, .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .pBufferInfo = &bi };
            vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
            return true;
        }

        bool createTextureResources() {
            auto& ctx = graphics::internal::context;

            VkDescriptorSetLayoutBinding b{ .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
            VkDescriptorSetLayoutCreateInfo li{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1, .pBindings = &b };
            if (vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &texture_descriptor_set_layout) != VK_SUCCESS)
                return false;

            VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
            VkDescriptorPoolCreateInfo pi{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
            if (vkCreateDescriptorPool(ctx.device, &pi, nullptr, &texture_descriptor_pool) != VK_SUCCESS)
                return false;

            VkDescriptorSetAllocateInfo ai{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = texture_descriptor_pool, .descriptorSetCount = 1,
                .pSetLayouts = &texture_descriptor_set_layout };
            return vkAllocateDescriptorSets(ctx.device, &ai, &texture_descriptor_set) == VK_SUCCESS;
        }

        void updateTextureDescriptor() {
            if (g_textures.empty()) return;
            auto& ctx = graphics::internal::context;
            Texture& t = g_textures[g_selected_texture];

            VkDescriptorImageInfo ii{ .sampler = t.sampler, .imageView = t.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkWriteDescriptorSet w{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = texture_descriptor_set, .dstBinding = 0, .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ii };
            vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
        }

        bool createGraphicsPipeline() {
            auto& ctx = graphics::internal::context;
            if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) return false;

            VkPipelineShaderStageCreateInfo stages[2] = {
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_shader, .pName = "main" },
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment_shader, .pName = "main" },
            };

            VkPipelineVertexInputStateCreateInfo vi{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vertex_binding,
                .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = vertex_attributes };

            VkPipelineInputAssemblyStateCreateInfo ia{ .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };

            VkPipelineViewportStateCreateInfo vs{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1, .scissorCount = 1 };

            VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo ds{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = 2, .pDynamicStates = dyn };

            VkDescriptorSetLayout setLayouts[] = {
                lighting_descriptor_set_layout,
                model_descriptor_set_layout,
                point_lights_descriptor_set_layout,
                spot_lights_descriptor_set_layout,
                texture_descriptor_set_layout,
            };
            VkPipelineLayoutCreateInfo li{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 5,
    .pSetLayouts = setLayouts,
    .pushConstantRangeCount = 0,
    .pPushConstantRanges = nullptr,
            };

            if (vkCreatePipelineLayout(ctx.device, &li, nullptr, &pipeline_layout) != VK_SUCCESS) return false;

            VkPipelineRasterizationStateCreateInfo rs{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };

            VkPipelineMultisampleStateCreateInfo ms{ .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };

            VkPipelineDepthStencilStateCreateInfo dss{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS };

            VkPipelineColorBlendAttachmentState cba{ .blendEnable = VK_FALSE,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
            VkPipelineColorBlendStateCreateInfo cb{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1, .pAttachments = &cba };

            VkGraphicsPipelineCreateInfo pi{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2, .pStages = stages,
                .pVertexInputState = &vi, .pInputAssemblyState = &ia, .pViewportState = &vs,
                .pRasterizationState = &rs, .pMultisampleState = &ms, .pDepthStencilState = &dss,
                .pColorBlendState = &cb, .pDynamicState = &ds,
                .layout = pipeline_layout, .renderPass = ctx.render_pass, .subpass = 0 };

            return vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pi, nullptr, &graphics_pipeline) == VK_SUCCESS;
        }

        bool createLightingResources() {
            auto& ctx = graphics::internal::context;

            if (!createHostBuffer(sizeof(LightingUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                lighting_buffer, lighting_buffer_allocation, &lighting_buffer_mapped))
                return false;

            if (!createHostBuffer(sizeof(PointLightsBuffer), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                point_lights_buffer, point_lights_buffer_allocation, &point_lights_buffer_mapped))
                return false;

            if (!createHostBuffer(sizeof(SpotLightsBuffer), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                spot_lights_buffer, spot_lights_buffer_allocation, &spot_lights_buffer_mapped))
                return false;


            {
                VkDescriptorSetLayoutBinding b{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
                VkDescriptorSetLayoutCreateInfo li{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = 1, .pBindings = &b };
                vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &lighting_descriptor_set_layout);

                VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
                VkDescriptorPoolCreateInfo pi{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                    .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
                vkCreateDescriptorPool(ctx.device, &pi, nullptr, &lighting_descriptor_pool);

                VkDescriptorSetAllocateInfo ai{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = lighting_descriptor_pool, .descriptorSetCount = 1,
                    .pSetLayouts = &lighting_descriptor_set_layout };
                vkAllocateDescriptorSets(ctx.device, &ai, &lighting_descriptor_set);

                VkDescriptorBufferInfo bi{ .buffer = lighting_buffer, .offset = 0, .range = sizeof(LightingUniform) };
                VkWriteDescriptorSet w{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = lighting_descriptor_set, .dstBinding = 0, .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bi };
                vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
            }

            {
                VkDescriptorSetLayoutBinding b{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
                VkDescriptorSetLayoutCreateInfo li{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = 1, .pBindings = &b };
                vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &point_lights_descriptor_set_layout);

                VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
                VkDescriptorPoolCreateInfo pi{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                    .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
                vkCreateDescriptorPool(ctx.device, &pi, nullptr, &point_lights_descriptor_pool);

                VkDescriptorSetAllocateInfo ai{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = point_lights_descriptor_pool, .descriptorSetCount = 1,
                    .pSetLayouts = &point_lights_descriptor_set_layout };
                vkAllocateDescriptorSets(ctx.device, &ai, &point_lights_descriptor_set);

                VkDescriptorBufferInfo bi{ .buffer = point_lights_buffer, .offset = 0, .range = sizeof(PointLightsBuffer) };
                VkWriteDescriptorSet w{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = point_lights_descriptor_set, .dstBinding = 0, .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi };
                vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
            }

            {
                VkDescriptorSetLayoutBinding b{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
                VkDescriptorSetLayoutCreateInfo li{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = 1, .pBindings = &b };
                vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &spot_lights_descriptor_set_layout);

                VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
                VkDescriptorPoolCreateInfo pi{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                    .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
                vkCreateDescriptorPool(ctx.device, &pi, nullptr, &spot_lights_descriptor_pool);

                VkDescriptorSetAllocateInfo ai{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = spot_lights_descriptor_pool, .descriptorSetCount = 1,
                    .pSetLayouts = &spot_lights_descriptor_set_layout };
                vkAllocateDescriptorSets(ctx.device, &ai, &spot_lights_descriptor_set);

                VkDescriptorBufferInfo bi{ .buffer = spot_lights_buffer, .offset = 0, .range = sizeof(SpotLightsBuffer) };
                VkWriteDescriptorSet w{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = spot_lights_descriptor_set, .dstBinding = 0, .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi };
                vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
            }

            return true;
        }

    } // namespace

    void setWindow(GLFWwindow* window) { app_window = window; }
    bool initialize() {
        // Вершины и индексы - VRAM через staging
        uploadBufferViaStaging(cube_vertices.data(), sizeof(cube_vertices),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            vertex_buffer, vertex_buffer_allocation);
        uploadBufferViaStaging(cube_indices.data(), sizeof(cube_indices),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            index_buffer, index_buffer_allocation);

        // Шейдеры
        auto vcode = readShaderFile("shaders/cube.vert.spv");
        auto fcode = readShaderFile("shaders/cube.frag.spv");
        vertex_shader = createShaderModule(vcode);
        fragment_shader = createShaderModule(fcode);
        if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) return false;

        if (!createLightingResources()) return false;
        if (!createModelResources()) return false;
        if (!createTextureResources()) return false;
        const char* texPaths[] = { "C:/projects/vulkan-starter-app/textures/cube.png" };
        for (auto* p : texPaths) {
            Texture t{}; t.name = p;
            if (createTextureFromFile(p, t)) g_textures.push_back(t);
            else std::cerr << "Texture skipped: " << p << "\n";
        }
        if (g_textures.empty()) {
            std::cerr << "WARNING: no textures loaded, using dummy path\n";
        }
        if (!g_textures.empty()) updateTextureDescriptor();

        if (!createGraphicsPipeline()) return false;
        std::cout << "Pipeline created\n";
        return true;
    }
    void shutdown() {
        auto& ctx = graphics::internal::context;
        vkQueueWaitIdle(ctx.graphics_queue);

        // текстуры
        for (auto& t : g_textures) {
            if (t.sampler) vkDestroySampler(ctx.device, t.sampler, nullptr);
            if (t.view)    vkDestroyImageView(ctx.device, t.view, nullptr);
            if (t.image)   vmaDestroyImage(ctx.allocator, t.image, t.allocation);
        }
        g_textures.clear();

        // дескрипторы
        auto destroyPool = [&](VkDescriptorPool& p) { if (p) { vkDestroyDescriptorPool(ctx.device, p, nullptr); p = VK_NULL_HANDLE; } };
        auto destroyLayout = [&](VkDescriptorSetLayout& l) { if (l) { vkDestroyDescriptorSetLayout(ctx.device, l, nullptr); l = VK_NULL_HANDLE; } };

        if (graphics_pipeline) { vkDestroyPipeline(ctx.device, graphics_pipeline, nullptr); graphics_pipeline = VK_NULL_HANDLE; }
        if (pipeline_layout) { vkDestroyPipelineLayout(ctx.device, pipeline_layout, nullptr); pipeline_layout = VK_NULL_HANDLE; }

        destroyPool(lighting_descriptor_pool);
        destroyPool(point_lights_descriptor_pool);
        destroyPool(spot_lights_descriptor_pool);
        destroyPool(model_descriptor_pool);
        destroyPool(texture_descriptor_pool);

        destroyLayout(lighting_descriptor_set_layout);
        destroyLayout(point_lights_descriptor_set_layout);
        destroyLayout(spot_lights_descriptor_set_layout);
        destroyLayout(model_descriptor_set_layout);
        destroyLayout(texture_descriptor_set_layout);

        auto destroyBuf = [&](VkBuffer& b, VmaAllocation& a) { if (b) { vmaDestroyBuffer(ctx.allocator, b, a); b = VK_NULL_HANDLE; a = VK_NULL_HANDLE; } };
        destroyBuf(lighting_buffer, lighting_buffer_allocation);
        destroyBuf(point_lights_buffer, point_lights_buffer_allocation);
        destroyBuf(spot_lights_buffer, spot_lights_buffer_allocation);
        destroyBuf(model_ubo_buffer, model_ubo_allocation);
        destroyBuf(vertex_buffer, vertex_buffer_allocation);
        destroyBuf(index_buffer, index_buffer_allocation);

        if (vertex_shader) { vkDestroyShaderModule(ctx.device, vertex_shader, nullptr); vertex_shader = VK_NULL_HANDLE; }
        if (fragment_shader) { vkDestroyShaderModule(ctx.device, fragment_shader, nullptr); fragment_shader = VK_NULL_HANDLE; }
    }


    void updateCameraMovement(double delta_time) {
        if (!app_window) return;
        float speed = camera_move_speed * (float)delta_time;
        float fx = std::cos(camera_yaw) * std::cos(camera_pitch);
        float fy = std::sin(camera_pitch);
        float fz = std::sin(camera_yaw) * std::cos(camera_pitch);
        float rx = -std::sin(camera_yaw), rz = std::cos(camera_yaw);
        if (glfwGetKey(app_window, GLFW_KEY_W) == GLFW_PRESS) { camera_x += fx * speed; camera_y += fy * speed; camera_z += fz * speed; }
        if (glfwGetKey(app_window, GLFW_KEY_S) == GLFW_PRESS) { camera_x -= fx * speed; camera_y -= fy * speed; camera_z -= fz * speed; }
        if (glfwGetKey(app_window, GLFW_KEY_A) == GLFW_PRESS) { camera_x -= rx * speed; camera_z -= rz * speed; }
        if (glfwGetKey(app_window, GLFW_KEY_D) == GLFW_PRESS) { camera_x += rx * speed; camera_z += rz * speed; }
        if (glfwGetKey(app_window, GLFW_KEY_SPACE) == GLFW_PRESS) camera_y += speed;
        if (glfwGetKey(app_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera_y -= speed;
    }

    void update(double time) {
        double dt = previous_frame_time > 0.0 ? time - previous_frame_time : 0.0;
        previous_frame_time = time;
        updateCameraMovement(std::clamp(dt, 0.0, 0.05));
        rotation_angle = (float)time * rotation_speed;

        ImGui::Begin("Cube settings");

        ImGui::Separator(); ImGui::Text("Camera");
        ImGui::SliderFloat("Camera X", &camera_x, -5, 5);
        ImGui::SliderFloat("Camera Y", &camera_y, -5, 5);
        ImGui::SliderFloat("Camera Z", &camera_z, -5, 5);
        float yaw_d = camera_yaw * 57.2957795f;
        if (ImGui::SliderFloat("Camera yaw", &yaw_d, -180, 180)) camera_yaw = yaw_d * 0.0174532925f;
        float pitch_d = camera_pitch * 57.2957795f;
        if (ImGui::SliderFloat("Camera pitch", &pitch_d, -89, 89)) camera_pitch = pitch_d * 0.0174532925f;
        float fov_d = camera_fov * 57.2957795f;
        if (ImGui::SliderFloat("Camera FOV", &fov_d, 30, 100)) camera_fov = fov_d * 0.0174532925f;
        ImGui::Checkbox("Perspective", &use_perspective);

        ImGui::Separator(); ImGui::Text("Cube");
        ImGui::SliderFloat("Rotation speed", &rotation_speed, 0, 3);
        ImGui::SliderFloat("Position X", &cube_position_x, -2, 2);
        ImGui::SliderFloat("Position Y", &cube_position_y, -2, 2);
        ImGui::SliderFloat("Position Z", &cube_position_z, -1.5f, 1.5f);
        ImGui::SliderFloat("Scale", &cube_scale, 0.2f, 2);

        // выбор текстуры
        if (!g_textures.empty()) {
            std::vector<const char*> names;
            for (auto& t : g_textures) names.push_back(t.name.c_str());
            if (ImGui::Combo("Texture", &g_selected_texture, names.data(), (int)names.size())) {
                updateTextureDescriptor();
            }
        }
        else {
            ImGui::TextColored({ 1,0,0,1 }, "No textures loaded!");
        }

        ImGui::Separator(); ImGui::Text("Directional light");
        ImGui::SliderFloat3("Light direction", lighting.light_direction, -1, 1);
        ImGui::ColorEdit3("Light color", lighting.light_color);
        ImGui::SliderFloat("Light intensity", &lighting.light_intensity, 0, 5);

        ImGui::Separator(); ImGui::Text("Point Light");
        ImGui::Checkbox("Point enabled", &lighting.point_enabled);
        ImGui::SliderFloat3("Point position", lighting.point_position, -5, 5);
        ImGui::ColorEdit3("Point color", lighting.point_color);
        ImGui::SliderFloat("Point intensity", &lighting.point_intensity, 0, 50);

        ImGui::Separator(); ImGui::Text("Spot Light");
        ImGui::Checkbox("Spot enabled", &lighting.spot_enabled);
        ImGui::SliderFloat3("Spot position", lighting.spot_position, -5, 5);
        ImGui::SliderFloat3("Spot direction", lighting.spot_direction, -1, 1);
        ImGui::ColorEdit3("Spot color", lighting.spot_color);
        ImGui::SliderFloat("Spot intensity", &lighting.spot_intensity, 0, 50);
        ImGui::SliderFloat("Spot inner", &lighting.spot_inner_angle, 1, 85);
        ImGui::SliderFloat("Spot outer", &lighting.spot_outer_angle, 1, 89);
        if (lighting.spot_inner_angle > lighting.spot_outer_angle)
            lighting.spot_inner_angle = lighting.spot_outer_angle;

        ImGui::Separator(); ImGui::Text("Ambient");
        ImGui::ColorEdit3("Ambient color", lighting.ambient_color);
        ImGui::SliderFloat("Ambient intensity", &lighting.ambient_intensity, 0, 1);

        ImGui::Separator(); ImGui::Text("Material");
        ImGui::ColorEdit3("Specular color", lighting.specular_color);
        ImGui::SliderFloat("Shininess", &lighting.shininess, 1, 256);

        ImGui::End();
    }

    void render(const graphics::internal::FrameData& fd) {
        auto& ctx = graphics::internal::context;

        VkCommandBufferBeginInfo bi{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        vkBeginCommandBuffer(fd.command_buffer, &bi);

        VkClearValue clears[2] = {
            {.color = { {0.1f,0.1f,0.1f,1.0f} } },
            {.depthStencil = { 1.0f, 0 } }
        };
        VkRenderPassBeginInfo rp{ .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = ctx.render_pass, .framebuffer = fd.framebuffer,
            .renderArea = { {0,0}, ctx.swapchain_extent },
            .clearValueCount = 2, .pClearValues = clears };
        vkCmdBeginRenderPass(fd.command_buffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{ 0,0, (float)ctx.swapchain_extent.width, (float)ctx.swapchain_extent.height, 0,1 };
        VkRect2D sc{ {0,0}, ctx.swapchain_extent };
        vkCmdSetViewport(fd.command_buffer, 0, 1, &vp);
        vkCmdSetScissor(fd.command_buffer, 0, 1, &sc);
        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, &off);
        vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);

        float aspect = (float)ctx.swapchain_extent.width / (float)ctx.swapchain_extent.height;
        Mat4 model = createModel(rotation_angle);
        Mat4 view = createView();
        Mat4 proj = createProjection(aspect, use_perspective);
        Mat4 mvp = multiplyMatrices(proj, multiplyMatrices(view, model));

        ModelUBO mu{ .model = model, .mvp = mvp };
        std::memcpy((char*)model_ubo_mapped + 0, &mu, sizeof(mu));

        // lighting UBO
        LightingUniform lu{};
        lu.light_direction[0] = lighting.light_direction[0];
        lu.light_direction[1] = lighting.light_direction[1];
        lu.light_direction[2] = lighting.light_direction[2];
        lu.light_color_intensity[0] = lighting.light_color[0];
        lu.light_color_intensity[1] = lighting.light_color[1];
        lu.light_color_intensity[2] = lighting.light_color[2];
        lu.light_color_intensity[3] = lighting.light_intensity;
        lu.ambient_color_intensity[0] = lighting.ambient_color[0];
        lu.ambient_color_intensity[1] = lighting.ambient_color[1];
        lu.ambient_color_intensity[2] = lighting.ambient_color[2];
        lu.ambient_color_intensity[3] = lighting.ambient_intensity;
        lu.specular_color_shininess[0] = lighting.specular_color[0];
        lu.specular_color_shininess[1] = lighting.specular_color[1];
        lu.specular_color_shininess[2] = lighting.specular_color[2];
        lu.specular_color_shininess[3] = lighting.shininess;
        lu.camera_position[0] = camera_x; lu.camera_position[1] = camera_y; lu.camera_position[2] = camera_z; lu.camera_position[3] = 1;
        lu.point_position[0] = lighting.point_position[0];
        lu.point_position[1] = lighting.point_position[1];
        lu.point_position[2] = lighting.point_position[2]; lu.point_position[3] = 1;
        lu.point_color_intensity[0] = lighting.point_color[0];
        lu.point_color_intensity[1] = lighting.point_color[1];
        lu.point_color_intensity[2] = lighting.point_color[2];
        lu.point_color_intensity[3] = lighting.point_enabled ? lighting.point_intensity : 0.0f;
        std::memcpy(lighting_buffer_mapped, &lu, sizeof(lu));

        PointLightsBuffer pl{}; pl.count = lighting.point_enabled ? 1u : 0u;
        if (pl.count > 0) {
            auto& l = pl.lights[0];
            l.position[0] = lighting.point_position[0]; l.position[1] = lighting.point_position[1];
            l.position[2] = lighting.point_position[2]; l.position[3] = 1;
            l.color_intensity[0] = lighting.point_color[0]; l.color_intensity[1] = lighting.point_color[1];
            l.color_intensity[2] = lighting.point_color[2]; l.color_intensity[3] = lighting.point_intensity;
        }
        std::memcpy(point_lights_buffer_mapped, &pl, sizeof(pl));

        SpotLightsBuffer sl{}; sl.count = lighting.spot_enabled ? 1u : 0u;
        if (sl.count > 0) {
            auto& l = sl.lights[0];
            l.position[0] = lighting.spot_position[0]; l.position[1] = lighting.spot_position[1];
            l.position[2] = lighting.spot_position[2]; l.position[3] = 1;
            l.direction[0] = lighting.spot_direction[0]; l.direction[1] = lighting.spot_direction[1];
            l.direction[2] = lighting.spot_direction[2]; l.direction[3] = 0;
            l.color_intensity[0] = lighting.spot_color[0]; l.color_intensity[1] = lighting.spot_color[1];
            l.color_intensity[2] = lighting.spot_color[2]; l.color_intensity[3] = lighting.spot_intensity;
            l.angles[0] = lighting.spot_inner_angle; l.angles[1] = lighting.spot_outer_angle;
        }
        std::memcpy(spot_lights_buffer_mapped, &sl, sizeof(sl));

        // bind sets
        uint32_t modelOffset = 0;
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout, 0, 1, &lighting_descriptor_set, 0, nullptr);
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout, 1, 1, &model_descriptor_set, 1, &modelOffset);
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout, 2, 1, &point_lights_descriptor_set, 0, nullptr);
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout, 3, 1, &spot_lights_descriptor_set, 0, nullptr);
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout, 4, 1, &texture_descriptor_set, 0, nullptr);
        vkCmdDrawIndexed(fd.command_buffer, (uint32_t)cube_indices.size(), 1, 0, 0, 0);
        vkCmdEndRenderPass(fd.command_buffer);
        vkEndCommandBuffer(fd.command_buffer);
    }

} // namespace application