#include "oc_tree.hpp"
#include "log/log.hpp"

namespace ida {
// ============================================================================
// OcTreeNode
// ============================================================================
template <class T>
void OcTreeNode<T>::Insert(T* data, glm::vec3 position) {
}

template <class T>
void OcTreeNode<T>::Remove(T* data, glm::vec3 position) {
}

template <class T>
T* OcTreeNode<T>::Find(OcTreeNode<T>* node, glm::vec3& position) {
    if (position.x > node->aabb_.max.x || position.x < node->aabb_.min.x ||
        position.y > node->aabb_.max.y || position.y < node->aabb_.min.y ||
        position.z > node->aabb_.max.z || position.z < node->aabb_.min.z) {
        return nullptr;
    }

    if (node->IsLeaf()) {
        return node->data_;
    }

    glm::vec3 center = node->aabb_.min + (node->aabb_.max - node->aabb_.min) / 2.0f;

    if (position.x < center.x) {
        if (position.y < center.y) {
            return position.z < center.z ? Find(node->bottomLeftFront_, position) : Find(node->bottomLeftBack_, position);
        } else {
            return position.z < center.z ? Find(node->topLeftFront_, position) : Find(node->topLeftBack_, position);
        }
    } else {
        if (position.y < center.y) {
            return position.z < center.z ? Find(node->bottomRightFront_, position) : Find(node->bottomRightBack_, position);
        } else {
            return position.z < center.z ? Find(node->topRightFront_, position) : Find(node->topRightBack_, position);
        }
    }
}

// ============================================================================
// OcTree
// ============================================================================
//template <class T>
//OcTree<T>::OcTree(AABB aabb, int depth) {
//    IO::Assert(depth > 0, "Depth must be greater than 0");
//}

} // namespace ida