#include "model.hpp"
#include "utils.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include "log/log.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <memory>
#include <unordered_map>
namespace std {
/**
 * @brief Hash function for glm::vec3
 */
template <>
struct hash<glm::vec3> {
    size_t operator()(const glm::vec3& v) const {
        size_t seed = 0;
        ida::hashCombine(seed, v.x, v.y, v.z);
        return seed;
    }
};
/**
 * @brief Hash function for glm::vec2
 */
template <>
struct hash<glm::vec2> {
    size_t operator()(const glm::vec2& v) const {
        size_t seed = 0;
        ida::hashCombine(seed, v.x, v.y);
        return seed;
    }
};
/**
 * @brief Hash function for Vertex
 */
template <>
struct hash<ida::IdaModel::Vertex> {
    size_t operator()(ida::IdaModel::Vertex const& vertex) const {
        size_t seed = 0;
        ida::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.uv);
        return seed;
    }
};
} // namespace std

namespace ida {
std::vector<vk::VertexInputBindingDescription> IdaModel::Vertex::GetBindingDescriptions() {
    std::vector<vk::VertexInputBindingDescription> bindingDescriptions(1);
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(Vertex);
    bindingDescriptions[0].inputRate = vk::VertexInputRate::eVertex;
    return bindingDescriptions;
}

std::vector<vk::VertexInputAttributeDescription> IdaModel::Vertex::GetAttributeDescriptions() {
    std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    attributeDescriptions.push_back({0, 0, vk::Format::eR32G32B32Sfloat, static_cast<uint32_t>(offsetof(Vertex, position))});
    attributeDescriptions.push_back({1, 0, vk::Format::eR32G32B32Sfloat, static_cast<uint32_t>(offsetof(Vertex, color))});
    attributeDescriptions.push_back({2, 0, vk::Format::eR32G32B32Sfloat, static_cast<uint32_t>(offsetof(Vertex, normal))});
    attributeDescriptions.push_back({3, 0, vk::Format::eR32G32Sfloat, static_cast<uint32_t>(offsetof(Vertex, uv))});
    return attributeDescriptions;
}

void IdaModel::Builder::LoadModel(const std::string& path) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        IO::ThrowError("Failed to load model: {}", path);
    }

    vertices.clear();
    indices.clear();

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};
    std::unordered_map<glm::vec3, uint32_t> s;
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};
            if (index.vertex_index >= 0) {
                vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2],
                };

                vertex.color = {
                    attrib.colors[3 * index.vertex_index + 0],
                    attrib.colors[3 * index.vertex_index + 1],
                    attrib.colors[3 * index.vertex_index + 2],
                };
            }
            if (index.normal_index >= 0) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2],
                };
            }
            if (index.texcoord_index >= 0) {
                vertex.uv = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    attrib.texcoords[2 * index.texcoord_index + 1],
                };
            }
            if (uniqueVertices.count(vertex) == 0) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }
            indices.push_back(uniqueVertices[vertex]);
        }
    }
}

IdaModel::IdaModel(const IdaModel::Builder& builder) {
    CreateVertexBuffer(builder.vertices);
    CreateIndexBuffer(builder.indices);
}

IdaModel::~IdaModel() {
    IO::PrintLog(LOG_LEVEL::LOG_LEVEL_INFO, "Model destroyed");
    vertexBuffer_.reset();
    indexBuffer_.reset();
}

std::unique_ptr<IdaModel> IdaModel::ImportModel(const std::string& path) {
    IO::PrintLog(LOG_LEVEL::LOG_LEVEL_INFO, "Importing model: {}", path);
    Builder builder{};
    builder.LoadModel(path);
    return std::make_unique<IdaModel>(builder);
}

void IdaModel::Bind(vk::CommandBuffer cmd) {
    vk::Buffer buffers[] = {vertexBuffer_->GetBuffer()};
    vk::DeviceSize offsets[] = {0};
    cmd.bindVertexBuffers(0, 1, buffers, offsets);
    if (hasIndexBuffer_) {
        cmd.bindIndexBuffer(indexBuffer_->GetBuffer(), 0, vk::IndexType::eUint32);
    }
}

void IdaModel::Draw(vk::CommandBuffer cmd) {
    if (hasIndexBuffer_) {
        cmd.drawIndexed(indexCount_, 1, 0, 0, 0);
    } else {
        cmd.draw(vertexCount_, 1, 0, 0);
    }
}

void IdaModel::CreateVertexBuffer(const std::vector<Vertex>& vertices) {
    vertexCount_ = static_cast<uint32_t>(vertices.size());
    IO::Assert(vertexCount_ >= 3, "Vertex count must be greater than 3");
    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertexCount_;
    uint32_t vertexSize = sizeof(vertices[0]);

    IdaBuffer stagingBuffer{
        BufferType::StagingBuffer,
        vertexSize,
        vertexCount_,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    stagingBuffer.Map();
    stagingBuffer.WriteToBuffer((void*)vertices.data());

    vertexBuffer_ = std::make_unique<IdaBuffer>(
        BufferType::VertexBuffer,
        vertexSize,
        vertexCount_,
        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    IdaBuffer::Utils::CopyBuffer(stagingBuffer.GetBuffer(), vertexBuffer_->GetBuffer(), bufferSize);
}

void IdaModel::CreateIndexBuffer(const std::vector<uint32_t>& indices) {
    indexCount_ = static_cast<uint32_t>(indices.size());
    hasIndexBuffer_ = indexCount_ > 0;
    if (!hasIndexBuffer_) {
        return;
    }

    vk::DeviceSize bufferSize = sizeof(indices[0]) * indexCount_;
    uint32_t indexSize = sizeof(indices[0]);

    IdaBuffer stagingBuffer{
        BufferType::StagingBuffer,
        indexSize,
        indexCount_,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    stagingBuffer.Map();
    stagingBuffer.WriteToBuffer((void*)indices.data());

    indexBuffer_ = std::make_unique<IdaBuffer>(
        BufferType::IndexBuffer,
        indexSize,
        indexCount_,
        vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    IdaBuffer::Utils::CopyBuffer(stagingBuffer.GetBuffer(), indexBuffer_->GetBuffer(), bufferSize);
}

std::unique_ptr<IdaModel> IdaModel::CustomModel(const std::vector<Vertex>& vertices) {
    Builder builder{};
    builder.vertices = vertices;
    return std::make_unique<IdaModel>(builder);
}

} // namespace ida