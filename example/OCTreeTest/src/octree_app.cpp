#include "app.hpp"
#include "system/simple_render_system.hpp"
#include "graphics/core/keyboard_controller.hpp"
#include "graphics/buffer/uniform_buffer_objects.hpp"
#include "system/oc_tree_render_system.hpp"
#include "scene/oc_tree.hpp"

std::unique_ptr<ida::OcTree<glm::vec3>> tree;
std::vector<ida::IdaGameObject> octreeNodeGO;

App::App(const std::string& title, int width, int height) {
    graphics_ = std::make_unique<ida::Graphics>();
    graphics_->InitGraphics(title, width, height);
    tree = std::make_unique<ida::OcTree<glm::vec3>>(ida::AABB{glm::vec3(-1.5f), glm::vec3(1.5f)}, 1);
    InitGlobalPool();
    LoadGameObjects();
}

App::~App() {
    ida::Context::GetInstance().device.waitIdle();
    globalPool_.reset();
    graphics_.reset();
    gameObjects_.clear();
    octreeNodeGO.clear();
    ida::Context::Quit();
}

bool App::InitGlobalPool() {
    globalPool_ = ida::IdaDescriptorPool::Builder()
                      .SetMaxSets(2 * ida ::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                      .AddPoolSize(vk::DescriptorType::eUniformBuffer, ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                      .AddPoolSize(vk::DescriptorType::eUniformBuffer, ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                      .Build();
    return globalPool_ != nullptr;
}

int App::Run() {
    auto& ctx = ida::Context::GetInstance();
    auto& window = graphics_->window();
    auto& renderer = graphics_->renderer();

    ida::UniformBufferObject<
        ida::SimpleRenderUniformPackage,
        vk::DescriptorType::eUniformBuffer>
        simpleRenderUbo{globalPool_};
    ida::UniformBufferObject<
        ida::OCTreeRenderUniformPackage,
        vk::DescriptorType::eUniformBuffer>
        ocTreeUbo{globalPool_};

    ida::SimpleRenderSystem simpleRenderSystem{
        renderer->GetRenderPass(),
        simpleRenderUbo.GetDescriptorSetLayout(),
    };
    ida::OCTreeRenderSystem ocTreeRenderSystem{
        renderer->GetRenderPass(),
        ocTreeUbo.GetDescriptorSetLayout(),
    };

    ida::KeyboardMovementController cameraController{};
    ida::IdaCamera camera{};
    auto viewObject = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Camera>("camera");
    viewObject.transform.translation.z = -2.5f;

    auto currentTime = std::chrono::high_resolution_clock::now();
    window->Run([&]() {
        glfwPollEvents();

        auto newTime = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
        currentTime = newTime;

        cameraController.MoveInPlaneXZ(window->GetWindow(), frameTime, viewObject);
        camera.SetViewYXZ(viewObject.transform.translation, viewObject.transform.rotation);
        float aspect = renderer->GetAspectRatio();
        camera.SetPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100.0f);

        if (auto commandBuffer = renderer->BeginFrame()) {
            int frameIndex = renderer->GetCurrentFrameIndex();
            ida::FrameInfo frameInfo{
                frameIndex,
                frameTime,
                commandBuffer,
                camera,
                gameObjects_,
            };
            frameInfo.descriptorSets["simple"] = simpleRenderUbo.GetDescriptorSet(frameIndex);
            frameInfo.descriptorSets["octree"] = ocTreeUbo.GetDescriptorSet(frameIndex);

            ida::SimpleRenderUniformPackage data{};
            ida::OCTreeRenderUniformPackage ocTreeData{};
            data.view = camera.GetView();
            data.projection = camera.GetProjection();
            data.inverseView = camera.GetInverseView();
            ocTreeData.view = camera.GetView();
            ocTreeData.projection = camera.GetProjection();
            ocTreeData.inverseView = camera.GetInverseView();

            simpleRenderUbo.Update(frameIndex, data);
            ocTreeUbo.Update(frameIndex, ocTreeData);

            renderer->BeginSwapChainRenderPass(commandBuffer);
            {
                simpleRenderSystem.RenderGameObjects(frameInfo);
                ocTreeRenderSystem.RenderGameObjects(frameInfo, octreeNodeGO);
            }
            renderer->EndSwapChainRenderPass(commandBuffer);
            renderer->EndFrame();
        }
    });
    ctx.device.waitIdle();
    return 0;
}

void App::LoadGameObjects() {
    std::shared_ptr<ida::IdaModel> model = ida::IdaModel::ImportModel("models/flat_vase.obj");
    auto vase = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Model>("vase");
    vase.model = model;
    vase.transform.translation = {-.5f, .5f, 0.f};
    vase.transform.scale = {3.f, 1.5f, 3.f};
    gameObjects_.emplace(vase.GetName(), std::move(vase));

    model = ida::IdaModel::ImportModel("models/smooth_vase.obj");
    auto vase2 = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Model>("vase2");
    vase2.model = model;
    vase2.transform.translation = {.5f, .5f, 0.f};
    vase2.transform.scale = {3.f, 1.5f, 3.f};
    gameObjects_.emplace(vase2.GetName(), std::move(vase2));

    tree->InitNodeDrawList(octreeNodeGO);
}