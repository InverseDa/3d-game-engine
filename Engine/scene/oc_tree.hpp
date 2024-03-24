#ifndef ENGINE_OC_TREE_HPP
#define ENGINE_OC_TREE_HPP

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

    template<class T>
    class OcTreeNode {
    public:
        OcTreeNode();
    };
}

#endif //ENGINE_OC_TREE_HPP
