#ifndef ENGINE_SCENE_GRAPH_HPP
#define ENGINE_SCENE_GRAPH_HPP

#include "vulkan/vulkan.hpp"

#include "graphics/render/pipeline.hpp"
#include "object/game_object.hpp"
#include "tool/global_info.hpp"

namespace ida {
class SceneNode {
  public:
    SceneNode();
    ~SceneNode();
    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;

    void AddGameObject(std::shared_ptr<IdaGameObject> gameObject);
    void RemoveGameObject(std::shared_ptr<IdaGameObject> gameObject);

    void Update(FrameInfo& frameInfo);

  private:
    std::shared_ptr<IdaGameObject> data_;
    std::vector<std::unique_ptr<SceneNode>> children_;
};

class SceneGraph {
  public:
    SceneGraph();
    ~SceneGraph();
    SceneGraph(const SceneGraph&) = delete;
    SceneGraph& operator=(const SceneGraph&) = delete;

    void AddGameObject(std::shared_ptr<IdaGameObject> gameObject);
    void RemoveGameObject(std::shared_ptr<IdaGameObject> gameObject);

    void Update(FrameInfo& frameInfo);

  private:
    std::unique_ptr<SceneNode> root_;
};

} // namespace ida

#endif // ENGINE_SCENE_GRAPH_HPP
