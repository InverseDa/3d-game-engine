#include "app.hpp"

#include "object/camera.hpp"
#include "graphics/core/context.hpp"
#include "graphics/core/keyboard_controller.hpp"
#include "graphics/core/mouse_controller.hpp"
#include "tool/global_info.hpp"
#include "graphics/swapchain/swapchain.hpp"
#include "system/point_light_system.hpp"
#include "system/simple_render_system.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/gtc/constants.hpp"
#include "glm/glm.hpp"
#include "glm/gtx/rotate_vector.hpp"

App::App(const std::string& title, int width, int height) {
    graphics_ = std::make_unique<ida::Graphics>();
    graphics_->InitGraphics(title, width, height);
    InitGlobalPool();
    LoadGameObjects();
}

App::~App() {
    ida::Context::GetInstance().device.waitIdle();
    globalPool_.reset();
    graphics_.reset();
    gameObjects_.clear();
    ida::Context::Quit();
}

bool App::InitGlobalPool() {
    globalPool_ = ida::IdaDescriptorPool::Builder()
                      .SetMaxSets(ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                      .AddPoolSize(vk::DescriptorType::eUniformBuffer, ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                      .Build();
    return globalPool_ != nullptr;
}

int App::Run() {
    std::vector<std::unique_ptr<ida::IdaBuffer>> uboBuffers(ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < uboBuffers.size(); i++) {
        uboBuffers[i] = std::make_unique<ida::IdaBuffer>(
            sizeof(ida::SimpleRenderUniformPackage),
            1,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible);
        uboBuffers[i]->Map();
    }
    auto globalSetLayout = ida::IdaDescriptorSetLayout::Builder()
                               .AddBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eAllGraphics)
                               .Build();
    std::vector<vk::DescriptorSet> globalDescriptorSets(ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < globalDescriptorSets.size(); i++) {
        auto bufferInfo = uboBuffers[i]->GetDescriptorInfo();
        ida::IdaDescriptorWriter(*globalSetLayout, *globalPool_)
            .WriteBuffer(0, &bufferInfo)
            .Build(globalDescriptorSets[i]);
    }

    ida::SimpleRenderSystem simpleRenderSystem{
        graphics_->renderer()->GetRenderPass(),
        globalSetLayout->GetDescriptorSetLayout(),
    };
    ida::PointLightSystem pointLightSystem{
        graphics_->renderer()->GetRenderPass(),
        globalSetLayout->GetDescriptorSetLayout(),
    };
    //    ida::TriangleRenderSystem triangleRenderSystem{
    //        renderer_->GetRenderPass(),
    //        globalSetLayout->GetDescriptorSetLayout(),
    //    };

    ida::IdaCamera camera{};

    auto viewObject = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Camera>("Camera");
    // TODO: ECS
    // viewObject->AddComponent<ida::IdaCameraComponent>(camera);
    viewObject.transform.translation.z = -2.5f;
    viewObject.transform.translation.y = 1.5f;
    ida::KeyboardMovementController cameraController{};
    ida::MouseMovementController mouseController{};

    auto currentTime = std::chrono::high_resolution_clock::now();
    graphics_->window()->Run([&]() {
        glfwPollEvents();

        auto newTime = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
        currentTime = newTime;

        float aspect = graphics_->renderer()->GetAspectRatio();
        cameraController.Move(graphics_->window()->GetWindow(), frameTime, viewObject);
        mouseController.MouseMovement(graphics_->window()->GetWindow(), frameTime, viewObject);
        camera.SetViewYXZ(viewObject.transform.translation, viewObject.transform.rotation);
        camera.SetPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100.0f);
        // camera.SetViewFromGLM(viewObject.transform.translation, viewObject.transform.translation + glm::vec3{0, 0, 1}, glm::vec3{0, 1, 0});
        // camera.SetPerspectiveProjectionFromGLM(glm::radians(50.f), aspect, 0.1f, 100.0f);
        IO::PrintLog(LOG_LEVEL::LOG_LEVEL_INFO, "Camera position: {}, {}, {}", viewObject.transform.translation.x, viewObject.transform.translation.y, viewObject.transform.translation.z);

        if (auto commandBuffer = graphics_->renderer()->BeginFrame()) {
            int frameIndex = graphics_->renderer()->GetCurrentFrameIndex();
            ida::FrameInfo frameInfo{
                frameIndex,
                frameTime,
                commandBuffer,
                camera,
                gameObjects_,
            };
            frameInfo.descriptorSets["simple"] = globalDescriptorSets[frameIndex];
            // update global UBO
            ida::SimpleRenderUniformPackage globalUbo{};
            globalUbo.view = camera.GetView();
            globalUbo.projection = camera.GetProjection();
            globalUbo.inverseView = camera.GetInverseView();
            pointLightSystem.Update(frameInfo, globalUbo);
            uboBuffers[frameIndex]->WriteToBuffer(&globalUbo);
            uboBuffers[frameIndex]->Flush();

            graphics_->renderer()->BeginSwapChainRenderPass(commandBuffer);
            {
                simpleRenderSystem.RenderGameObjects(frameInfo);
                pointLightSystem.Render(frameInfo);
                //                triangleRenderSystem.Render(frameInfo);
            }
            graphics_->renderer()->EndSwapChainRenderPass(commandBuffer);
            graphics_->renderer()->EndFrame();
        }
    });
    ida::Context::GetInstance().device.waitIdle();
    return 0;
}

void App::LoadGameObjects() {
    std::shared_ptr<ida::IdaModel> model = ida::IdaModel::ImportModel("models/flat_vase.obj");
    auto vase = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Model>("vase");
    vase.model = model;
    vase.transform.translation = {-.5f, .5f, 0.f};
    vase.transform.scale = {3.f, 1.5f, 3.f};
    vase.transform.rotation = {glm::pi<float>(), 0, 0.f};
    gameObjects_.emplace(vase.GetName(), std::move(vase));

    model = ida::IdaModel::ImportModel("models/smooth_vase.obj");
    auto vase2 = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Model>("vase2");
    vase2.model = model;
    vase2.transform.translation = {.5f, .5f, 0.f};
    vase2.transform.scale = {3.f, 1.5f, 3.f};
    vase2.transform.rotation = {glm::pi<float>(), 0, 0.f};
    gameObjects_.emplace(vase2.GetName(), std::move(vase2));

    model = ida::IdaModel::ImportModel("models/quad.obj");
    auto quad = ida::IdaGameObject::CreateGameObject<ida::GameObjectType::Model>("quad");
    quad.model = model;
    quad.transform.translation = {0.f, .5f, 0.f};
    quad.transform.scale = {3.f, 1.f, 3.f};
    quad.transform.rotation = {glm::pi<float>(), 0, 0.f};
    gameObjects_.emplace(quad.GetName(), std::move(quad));

    //    std::shared_ptr<ida::IdaModel> model = ida::IdaModel::CustomModel(
    //        {
    //            {{-1.f, -1.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {1.f, 0.f}},
    //            {{1.f, -1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 0.f}},
    //            {{0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}, {0.5f, 1.f}},
    //        });
    //    auto triangle = ida::IdaGameObject::CreateGameObject(ida::GameObjectType::Model);
    //    triangle.model = model;
    //    triangle.transform.translation = {0.f, 0.f, 0.f};
    //    triangle.transform.scale = {1.f, 1.f, 1.f};
    //    gameObjects_.emplace(triangle.GetId(), std::move(triangle));

    std::vector<glm::vec3> lightColors{
        {1.f, .1f, .1f},
        {.1f, .1f, 1.f},
        {.1f, 1.f, .1f},
        {1.f, 1.f, .1f},
        {.1f, 1.f, 1.f},
        {1.f, 1.f, 1.f},
    };

    for (int i = 0; i < lightColors.size(); i++) {
        auto pointLight = ida::IdaGameObject::MakePointLight("Light" + std::to_string(i), 0.2f);
        pointLight.color = lightColors[i];
        auto rotateLight = glm::rotate(
            glm::mat4(1.f),
            (i * glm::two_pi<float>()) / lightColors.size(),
            {0.f, -1.f, 0.f});
        pointLight.transform.translation = glm::vec3(rotateLight * glm::vec4(-1.f, 2.f, -1.f, 1.f));
        gameObjects_.emplace(pointLight.GetName(), std::move(pointLight));
    }
}