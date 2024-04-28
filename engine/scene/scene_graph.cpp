#include "scene_graph.hpp"

namespace ida {
SceneNode::SceneNode() {
}

SceneNode::~SceneNode() {

}

void SceneNode::AddGameObject(std::shared_ptr<IdaGameObject> gameObject) {

}

void SceneNode::RemoveGameObject(std::shared_ptr<IdaGameObject> gameObject) {

}

void SceneNode::Update(ida::FrameInfo& frameInfo) {

}

SceneGraph::SceneGraph() {

}

SceneGraph::~SceneGraph() {

}

void SceneGraph::AddGameObject(std::shared_ptr<IdaGameObject> gameObject) {

}

void SceneGraph::RemoveGameObject(std::shared_ptr<IdaGameObject> gameObject) {

}

void SceneGraph::Update(FrameInfo& frameInfo) {

}

} // namespace ida