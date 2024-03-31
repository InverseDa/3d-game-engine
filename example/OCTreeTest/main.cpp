#include "scene/oc_tree.hpp"

int main() {
    ida::OcTree<int> tree(ida::AABB{glm::vec3(.0f), glm::vec3(1.0f)}, 2);
    // preorder traversal
    tree.PreOrderTraversal();

    // oc_tree_render_system


    return 0;
}