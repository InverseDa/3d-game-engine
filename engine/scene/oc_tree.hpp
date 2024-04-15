#ifndef ENGINE_OC_TREE_HPP
#define ENGINE_OC_TREE_HPP

#include <vector>
#include <memory>

#include "glm/vec3.hpp"
#include "object/object_structure.hpp"
#include "log/log.hpp"

namespace ida {
enum OcTreeNodeType {
    ROOT,
    BOTTOM_LEFT_FRONT,
    BOTTOM_LEFT_BACK,
    BOTTOM_RIGHT_FRONT,
    BOTTOM_RIGHT_BACK,
    TOP_LEFT_FRONT,
    TOP_LEFT_BACK,
    TOP_RIGHT_FRONT,
    TOP_RIGHT_BACK
};

enum OcTreePreOrderFunction {
    PRINT,
    DRAW
};

template <class T>
class OcTreeNode {
  public:
    explicit OcTreeNode(AABB aabb) : aabb(aabb){};
    ~OcTreeNode() = default;

    OcTreeNode(const OcTreeNode&) = delete;
    OcTreeNode& operator=(const OcTreeNode&) = delete;
    OcTreeNode(OcTreeNode&&) = default;
    OcTreeNode& operator=(OcTreeNode&&) = default;

    bool IsLeaf() const {
        return !topLeftFront && !topLeftBack && !topRightFront && !topRightBack &&
               !bottomLeftFront && !bottomLeftBack && !bottomRightFront && !bottomRightBack;
    }

    T* GetData() const { return data_.get(); }

    // bounding box
    AABB aabb{};
    // eight children
    OcTreeNode<T>* topLeftFront = nullptr;
    OcTreeNode<T>* topLeftBack = nullptr;
    OcTreeNode<T>* topRightFront = nullptr;
    OcTreeNode<T>* topRightBack = nullptr;
    OcTreeNode<T>* bottomLeftFront = nullptr;
    OcTreeNode<T>* bottomLeftBack = nullptr;
    OcTreeNode<T>* bottomRightFront = nullptr;
    OcTreeNode<T>* bottomRightBack = nullptr;

  private:
    // node type
    OcTreeNodeType type_ = ROOT;
    // node data
    std::shared_ptr<T> data_;
};

template <class T>
class OcTree {
  public:
    explicit OcTree(AABB aabb, int depth = 8) : maxDepth_(depth), maxAABB_(aabb) {
        root_ = new OcTreeNode<T>(aabb);
        IO::Assert(CreateTree(root_, 0), "Failed to create OcTree");
    }
    ~OcTree() {
        DeleteTree(root_);
    };

    OcTree(const OcTree&) = delete;
    OcTree& operator=(const OcTree&) = delete;
    OcTree(OcTree&&) = default;
    OcTree& operator=(OcTree&&) = default;

    void Insert(T* data, glm::vec3 position) {
        OcTreeNode<T>* node = Find(root_, position);
        if (node) {
            node->data_ = std::shared_ptr<T>(data);
        }
    }

    T* Find(OcTreeNode<T>* node, glm::vec3& position) {
        if (!node) {
            return nullptr;
        }

        if (node->aabb.Contains(position)) {
            if (node->IsLeaf()) {
                return node->GetData();
            } else {
                T* result = Find(node->bottomLeftFront, position);
                if (result) {
                    return result;
                }
                result = Find(node->bottomLeftBack, position);
                if (result) {
                    return result;
                }
                result = Find(node->bottomRightFront, position);
                if (result) {
                    return result;
                }
                result = Find(node->bottomRightBack, position);
                if (result) {
                    return result;
                }
                result = Find(node->topLeftFront, position);
                if (result) {
                    return result;
                }
                result = Find(node->topLeftBack, position);
                if (result) {
                    return result;
                }
                result = Find(node->topRightFront, position);
                if (result) {
                    return result;
                }
                result = Find(node->topRightBack, position);
                if (result) {
                    return result;
                }
            }
        }

        return nullptr;
    }

    bool CreateTree(OcTreeNode<T>* node, int depth = 8) {
        if (depth == maxDepth_) {
            return true;
        }

        AABB aabb = node->aabb;
        glm::vec3 center = aabb.min + (aabb.max - aabb.min) / 2.0f;
        node->bottomLeftFront = new OcTreeNode<T>(AABB{aabb.min, center});
        node->bottomLeftBack = new OcTreeNode<T>(AABB{glm::vec3(aabb.min.x, aabb.min.y, center.z), glm::vec3(center.x, center.y, aabb.max.z)});
        node->bottomRightFront = new OcTreeNode<T>(AABB{glm::vec3(center.x, aabb.min.y, aabb.min.z), glm::vec3(aabb.max.x, center.y, center.z)});
        node->bottomRightBack = new OcTreeNode<T>(AABB{glm::vec3(center.x, aabb.min.y, center.z), glm::vec3(aabb.max.x, center.y, aabb.max.z)});
        node->topLeftFront = new OcTreeNode<T>(AABB{glm::vec3(aabb.min.x, center.y, aabb.min.z), glm::vec3(center.x, aabb.max.y, center.z)});
        node->topLeftBack = new OcTreeNode<T>(AABB{glm::vec3(aabb.min.x, center.y, center.z), glm::vec3(center.x, aabb.max.y, aabb.max.z)});
        node->topRightFront = new OcTreeNode<T>(AABB{glm::vec3(center.x, center.y, aabb.min.z), glm::vec3(aabb.max.x, aabb.max.y, center.z)});
        node->topRightBack = new OcTreeNode<T>(AABB{center, aabb.max});

        return CreateTree(node->bottomLeftFront, depth + 1) &&
               CreateTree(node->bottomLeftBack, depth + 1) &&
               CreateTree(node->bottomRightFront, depth + 1) &&
               CreateTree(node->bottomRightBack, depth + 1) &&
               CreateTree(node->topLeftFront, depth + 1) &&
               CreateTree(node->topLeftBack, depth + 1) &&
               CreateTree(node->topRightFront, depth + 1) &&
               CreateTree(node->topRightBack, depth + 1);
    }

    void PreOrderTraversal() { PreOrderTraversalInternal(root_); }
    void InitNodeDrawList(std::vector<IdaGameObject>& nodeGO) { CreateGOListWithPreOrderTraversalInternal(root_, nodeGO); }

  private:
    OcTreeNode<T>* root_;
    int maxDepth_;
    AABB maxAABB_;

    void PreOrderTraversalInternal(OcTreeNode<T>* node) {
        if (node) {
            IO::PrintLog(LOG_LEVEL::LOG_LEVEL_INFO,
                         "OCTree Node: min(x={},y={},z={}), max(x={},y={},z={})",
                         node->aabb.min.x,
                         node->aabb.min.y,
                         node->aabb.min.z,
                         node->aabb.max.x,
                         node->aabb.max.y,
                         node->aabb.max.z);
            PreOrderTraversalInternal(node->bottomLeftFront);
            PreOrderTraversalInternal(node->bottomLeftBack);
            PreOrderTraversalInternal(node->bottomRightFront);
            PreOrderTraversalInternal(node->bottomRightBack);
            PreOrderTraversalInternal(node->topLeftFront);
            PreOrderTraversalInternal(node->topLeftBack);
            PreOrderTraversalInternal(node->topRightFront);
            PreOrderTraversalInternal(node->topRightBack);
        }
    }

    void CreateGOListWithPreOrderTraversalInternal(OcTreeNode<T>* node, std::vector<IdaGameObject>& nodeGO) {
        if (node) {
            // only draw if node is leaf
            if (node->IsLeaf()) {
                std::shared_ptr<ida::IdaModel> model = IdaModel::CreateCube(ModelDrawType::LINE);
                auto cubeGO = IdaGameObject::CreateGameObject<GameObjectType::Model>("cube");
                cubeGO.model = model;
                cubeGO.transform.translation = (node->aabb.max + node->aabb.min) * 0.5f;
                cubeGO.transform.scale = (node->aabb.max - node->aabb.min) * 0.5f;
                nodeGO.emplace_back(std::move(cubeGO));
            }

            CreateGOListWithPreOrderTraversalInternal(node->bottomLeftFront, nodeGO);
            CreateGOListWithPreOrderTraversalInternal(node->bottomLeftBack, nodeGO);
            CreateGOListWithPreOrderTraversalInternal(node->bottomRightFront, nodeGO);
            CreateGOListWithPreOrderTraversalInternal(node->bottomRightBack, nodeGO);
            CreateGOListWithPreOrderTraversalInternal(node->topLeftFront, nodeGO);
            CreateGOListWithPreOrderTraversalInternal(node->topLeftBack, nodeGO);
            CreateGOListWithPreOrderTraversalInternal(node->topRightFront, nodeGO);
            CreateGOListWithPreOrderTraversalInternal(node->topRightBack, nodeGO);
        }
    }

    void DeleteTree(OcTreeNode<T>*& node) {
        if (node) {
            DeleteTree(node->bottomLeftFront);
            DeleteTree(node->bottomLeftBack);
            DeleteTree(node->bottomRightFront);
            DeleteTree(node->bottomRightBack);
            DeleteTree(node->topLeftFront);
            DeleteTree(node->topLeftBack);
            DeleteTree(node->topRightFront);
            DeleteTree(node->topRightBack);
            delete node;
            node = nullptr;
        }
    }
};

} // namespace ida

#endif // ENGINE_OC_TREE_HPP
