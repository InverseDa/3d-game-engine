#include "app.hpp"
#include "system/simple_render_system.hpp"
#include "graphics/core/keyboard_controller.hpp"
#include "graphics/buffer/uniform_buffer_objects.hpp"

App::App(const std::string& title, int width, int height) {
    graphics_ = std::make_unique<ida::Graphics>();
    graphics_->InitGraphics(title, width, height);
    LoadGameObjects();
}

App::~App() {
    graphics_.reset();
    gameObjects_.clear();
    ida::Context::Quit();
}

int App::Run() {
    auto& ctx = ida::Context::GetInstance();
    auto& window = graphics_->window();
    auto& renderer = graphics_->renderer();
    auto& globalDescriptorPool = graphics_->globalPool();

    ida::UniformBufferObject<
        ida::GlobalUbo,
        vk::DescriptorType::eUniformBuffer>
        globalUbo{globalDescriptorPool};

    ida::SimpleRenderSystem simpleRenderSystem{
        renderer->GetRenderPass(),
        globalUbo.GetDescriptorSetLayout(),
    };

    ida::KeyboardMovementController cameraController{};
    ida::IdaCamera camera{};
    auto viewObject = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Camera>();
    viewObject.transform.translation.z = -2.5f;

    auto currentTime = std::chrono::high_resolution_clock::now();
    window->Run([&]() {
        glfwPollEvents();

        auto newTime = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
        currentTime = newTime;

        cameraController.MoveInPlaneXZ(graphics_->window()->GetWindow(), frameTime, viewObject);
        camera.SetViewYXZ(viewObject.transform.translation, viewObject.transform.rotation);
        float aspect = graphics_->renderer()->GetAspectRatio();
        camera.SetPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100.0f);

        if (auto commandBuffer = graphics_->renderer()->BeginFrame()) {
            int frameIndex = graphics_->renderer()->GetCurrentFrameIndex();
            ida::FrameInfo frameInfo{
                frameIndex,
                frameTime,
                commandBuffer,
                camera,
                globalUbo.GetDescriptorSet(frameIndex),
                gameObjects_,
            };
            // update global UBO
            ida::GlobalUbo data{};
            data.view = camera.GetView();
            data.projection = camera.GetProjection();
            data.inverseView = camera.GetInverseView();
            globalUbo.Update(frameIndex, data);

            renderer->BeginSwapChainRenderPass(commandBuffer);
            {
                simpleRenderSystem.RenderGameObjects(frameInfo);
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
    auto vase = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Model>();
    vase.model = model;
    vase.transform.translation = {-.5f, .5f, 0.f};
    vase.transform.scale = {3.f, 1.5f, 3.f};
    gameObjects_.emplace(vase.GetId(), std::move(vase));

    model = ida::IdaModel::ImportModel("models/smooth_vase.obj");
    auto vase2 = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Model>();
    vase2.model = model;
    vase2.transform.translation = {.5f, .5f, 0.f};
    vase2.transform.scale = {3.f, 1.5f, 3.f};
    gameObjects_.emplace(vase2.GetId(), std::move(vase2));

    model = ida::IdaModel::ImportModel("models/quad.obj");
    auto quad = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Model>();
    quad.model = model;
    quad.transform.translation = {0.f, .5f, 0.f};
    quad.transform.scale = {3.f, 3.f, 3.f};
    gameObjects_.emplace(quad.GetId(), std::move(quad));
}