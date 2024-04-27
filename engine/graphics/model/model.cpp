#include "model.hpp"
#include "tool/utils.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include "log/log.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/hash.hpp"

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
IdaModel::Builder IdaModel::builder = IdaModel::Builder();

std::vector<vk::VertexInputBindingDescription> IdaModel::Vertex::GetBindingDescriptions() {
    std::vector<vk::VertexInputBindingDescription> bindingDescriptions(1);
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(Vertex);
    bindingDescriptions[0].inputRate = vk::VertexInputRate::eVertex;
    return bindingDescriptions;
}

std::vector<vk::VertexInputAttributeDescription> IdaModel::Vertex::GetAttributeDescriptions() {
    std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    attributeDescriptions.emplace_back(0, 0, vk::Format::eR32G32B32Sfloat, static_cast<uint32_t>(offsetof(Vertex, position)));
    attributeDescriptions.emplace_back(1, 0, vk::Format::eR32G32B32Sfloat, static_cast<uint32_t>(offsetof(Vertex, color)));
    attributeDescriptions.emplace_back(2, 0, vk::Format::eR32G32B32Sfloat, static_cast<uint32_t>(offsetof(Vertex, normal)));
    attributeDescriptions.emplace_back(3, 0, vk::Format::eR32G32Sfloat, static_cast<uint32_t>(offsetof(Vertex, uv)));
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
        vertexSize,
        vertexCount_,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    stagingBuffer.Map();
    stagingBuffer.WriteToBuffer((void*)vertices.data());

    vertexBuffer_ = std::make_unique<IdaBuffer>(
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
        indexSize,
        indexCount_,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    stagingBuffer.Map();
    stagingBuffer.WriteToBuffer((void*)indices.data());

    indexBuffer_ = std::make_unique<IdaBuffer>(
        indexSize,
        indexCount_,
        vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    IdaBuffer::Utils::CopyBuffer(stagingBuffer.GetBuffer(), indexBuffer_->GetBuffer(), bufferSize);
}

std::unique_ptr<IdaModel> IdaModel::CreateCube(ModelDrawType drawType) {
    // it will be change
    builder.vertices = {
        // front
        {{-1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        // back
        {{-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{1.0f, -1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        // top
        {{-1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        // bottom
        {{-1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        // right
        {{1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        // left
        {{-1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-1.0f, -1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
    };
    if(drawType == TRIANGLE) {
        builder.indices = {
            0, 1, 2, 2, 3, 0, // front
            4, 5, 6, 6, 7, 4, // back
            8, 9, 10, 10, 11, 8, // top
            12, 13, 14, 14, 15, 12, // bottom
            16, 17, 18, 18, 19, 16, // right
            20, 21, 22, 22, 23, 20, // left
        };
    } else if(drawType == LINE) {
        builder.indices = {
            // Front face
            0, 1, 1, 2, 2, 3, 3, 0,
            // Back face
            4, 5, 5, 6, 6, 7, 7, 4,
            // Top face
            8, 9, 9, 10, 10, 11, 11, 8,
            // Bottom face
            12, 13, 13, 14, 14, 15, 15, 12,
            // Right face
            16, 17, 17, 18, 18, 19, 19, 16,
            // Left face
            20, 21, 21, 22, 22, 23, 23, 20,
        };
    }
    return std::make_unique<IdaModel>(builder);
}

} // namespace ida