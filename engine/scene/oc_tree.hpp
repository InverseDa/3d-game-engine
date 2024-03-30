#ifndef ENGINE_OC_TREE_HPP
#define ENGINE_OC_TREE_HPP

#include <vector>
#include <memory>

#include "glm/vec3.hpp"
#include "object/object_structure.hpp"

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

template <class T>
class OcTreeNode {
  public:
    explicit OcTreeNode(AABB aabb) : aabb_(aabb) {};
    ~OcTreeNode() = default;

    OcTreeNode(const OcTreeNode&) = delete;
    OcTreeNode& operator=(const OcTreeNode&) = delete;
    OcTreeNode(OcTreeNode&&) = default;
    OcTreeNode& operator=(OcTreeNode&&) = default;

    void Insert(T* data, glm::vec3 position);
    void Remove(T* data, glm::vec3 position);
    T* Find(OcTreeNode<T>* node, glm::vec3& position);

  private:
    // node type
    //    OcTreeNodeType type_;
    // node data
    std::shared_ptr<T> data_;
    // bounding box
    AABB aabb_;
    // eight children
    OcTreeNode<T>* topLeftFront_ = nullptr;
    OcTreeNode<T>* topLeftBack_ = nullptr;
    OcTreeNode<T>* topRightFront_ = nullptr;
    OcTreeNode<T>* topRightBack_ = nullptr;
    OcTreeNode<T>* bottomLeftFront_ = nullptr;
    OcTreeNode<T>* bottomLeftBack_ = nullptr;
    OcTreeNode<T>* bottomRightFront_ = nullptr;
    OcTreeNode<T>* bottomRightBack_ = nullptr;
};

 template <class T>
 class OcTree {
   public:
     explicit OcTree(AABB aabb, int depth = 8);
     ~OcTree();

     OcTree(const OcTree&) = delete;
     OcTree& operator=(const OcTree&) = delete;
     OcTree(OcTree&&) = default;
     OcTree& operator=(OcTree&&) = default;

     void Insert(T* data, glm::vec3 position);
     void Remove(T* data, glm::vec3 position);
     T* Find(glm::vec3 position);

     void Clear();

     bool CreateTree(AABB aabb, int depth = 8);

     void PreOrderTraversal();
     void InOrderTraversal();
     void PostOrderTraversal();

   private:
     OcTreeNode<T>* root_;
 };

} // namespace ida

#endif // ENGINE_OC_TREE_HPP
