#ifndef VULKAN_LIB_MODEL_HPP
#define VULKAN_LIB_MODEL_HPP

#include "buffer/buffer.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"
#include "vulkan/vulkan.hpp"
#include <memory>
#include <vector>

namespace ida {
class IdaModel {
  public:
    struct Vertex {
        glm::vec3 position{};
        glm::vec3 color{};
        glm::vec3 normal{};
        glm::vec2 uv{};

        static std::vector<vk::VertexInputBindingDescription> GetBindingDescriptions();
        static std::vector<vk::VertexInputAttributeDescription> GetAttributeDescriptions();

        bool operator==(const Vertex& other) const {
            return position == other.position && color == other.color && normal == other.normal && uv == other.uv;
        }
    };
    struct Builder {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        void LoadModel(const std::string& path);
    };

    IdaModel(const Builder& builder);
    ~IdaModel();
    IdaModel(const IdaModel&) = delete;
    IdaModel& operator=(const IdaModel&) = delete;

    static std::unique_ptr<IdaModel> ImportModel(const std::string& path);
    static std::unique_ptr<IdaModel> CustomModel(const std::vector<Vertex>& vertices);

    void Bind(vk::CommandBuffer cmd);
    void Draw(vk::CommandBuffer cmd);

  private:
    void CreateVertexBuffer(const std::vector<Vertex>& vertices);
    void CreateIndexBuffer(const std::vector<uint32_t>& indices);

    std::unique_ptr<IdaBuffer> vertexBuffer_;
    std::unique_ptr<IdaBuffer> indexBuffer_;
    bool hasIndexBuffer_{false};

    uint32_t vertexCount_{0};
    uint32_t indexCount_{0};
};
} // namespace ida

#endif // VULKAN_LIB_MODEL_HPP
