#include "scene/oc_tree.hpp"
#include "app.hpp"

int main() {
    ida::OcTree<int> tree(ida::AABB{glm::vec3(.0f), glm::vec3(1.0f)}, 2);
    // preorder traversal
    tree.PreOrderTraversal();

    // oc_tree_render_system
    App oc_tree_render_system_app{"OC Tree Render System", 1980, 1080};
    oc_tree_render_system_app.Run();
    return 0;
}