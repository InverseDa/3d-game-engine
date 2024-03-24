#include "app.hpp"
#include "camera/camera.hpp"
#include "core/context.hpp"
#include "core/keyboard_controller.hpp"
#include "global_info.hpp"
#include "swapchain/swapchain.hpp"
#include "system/point_light_system.hpp"
#include "system/triangle_render_system.hpp"
#include "system/simple_render_system.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/gtc/constants.hpp"
#include "glm/glm.hpp"
#include "glm/gtx/rotate_vector.hpp"

Application::Application(const std::string& title, int width, int height) {
    window_ = std::make_unique<ida::IdaWindow>(width, height, title);
    ida::Context::Init(window_->extensions, window_->getSurfaceCallback);
    renderer_ = std::make_unique<ida::IdaRenderer>(*window_);

    // Init global descriptor pool
    globalPool = ida::IdaDescriptorPool::Builder()
                     .SetMaxSets(ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                     .AddPoolSize(vk::DescriptorType::eUniformBuffer, ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                     .Build();

    LoadGameObjects();
}

Application::~Application() {
    ida::Context::GetInstance().device.waitIdle();
    globalPool.reset();
    renderer_.reset();
    window_.reset();
    gameObjects_.clear();
    ida::Context::Quit();
}

int Application::Run() {
    std::vector<std::unique_ptr<ida::IdaBuffer>> uboBuffers(ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < uboBuffers.size(); i++) {
        uboBuffers[i] = std::make_unique<ida::IdaBuffer>(
            ida::BufferType::UniformBuffer,
            sizeof(ida::GlobalUbo),
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
        ida::IdaDescriptorWriter(*globalSetLayout, *globalPool)
            .WriteBuffer(0, &bufferInfo)
            .Build(globalDescriptorSets[i]);
    }

    ida::SimpleRenderSystem simpleRenderSystem{
        renderer_->GetRenderPass(),
        globalSetLayout->GetDescriptorSetLayout(),
    };
    ida::PointLightSystem pointLightSystem{
        renderer_->GetRenderPass(),
        globalSetLayout->GetDescriptorSetLayout(),
    };
    //    ida::TriangleRenderSystem triangleRenderSystem{
    //        renderer_->GetRenderPass(),
    //        globalSetLayout->GetDescriptorSetLayout(),
    //    };

    ida::IdaCamera camera{};

    auto viewObject = ida::IdaGameObject::CreateGameObject(ida::GameObjectType::Camera);
    // TODO: ECS
    // viewObject->AddComponent<ida::IdaCameraComponent>(camera);
    viewObject.transform.translation.z = -2.5f;
    ida::KeyboardMovementController cameraController{};

    auto currentTime = std::chrono::high_resolution_clock::now();
    window_->Run([&]() {
        glfwPollEvents();

        auto newTime = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
        currentTime = newTime;

        cameraController.MoveInPlaneXZ(window_->GetWindow(), frameTime, viewObject);
        camera.SetViewYXZ(viewObject.transform.translation, viewObject.transform.rotation);
        float aspect = renderer_->GetAspectRatio();
        camera.SetPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100.0f);

        if (auto commandBuffer = renderer_->BeginFrame()) {
            int frameIndex = renderer_->GetCurrentFrameIndex();
            ida::FrameInfo frameInfo{
                frameIndex,
                frameTime,
                commandBuffer,
                camera,
                globalDescriptorSets[frameIndex],
                gameObjects_,
            };
            // update global UBO
            ida::GlobalUbo globalUbo{};
            globalUbo.view = camera.GetView();
            globalUbo.projection = camera.GetProjection();
            globalUbo.inverseView = camera.GetInverseView();
            pointLightSystem.Update(frameInfo, globalUbo);
            uboBuffers[frameIndex]->WriteToBuffer(&globalUbo);
            uboBuffers[frameIndex]->Flush();

            renderer_->BeginSwapChainRenderPass(commandBuffer);
            {
                simpleRenderSystem.RenderGameObjects(frameInfo);
                pointLightSystem.Render(frameInfo);
                //                triangleRenderSystem.Render(frameInfo);
            }
            renderer_->EndSwapChainRenderPass(commandBuffer);
            renderer_->EndFrame();
        }
    });
    ida::Context::GetInstance().device.waitIdle();
    return 0;
}

void Application::LoadGameObjects() {
    std::shared_ptr<ida::IdaModel> model = ida::IdaModel::ImportModel("models/flat_vase.obj");
    auto vase = ida::IdaGameObject::CreateGameObject(ida::GameObjectType::Model);
    vase.model = model;
    vase.transform.translation = {-.5f, .5f, 0.f};
    vase.transform.scale = {3.f, 1.5f, 3.f};
    gameObjects_.emplace(vase.GetId(), std::move(vase));

    model = ida::IdaModel::ImportModel("models/smooth_vase.obj");
    auto vase2 = ida::IdaGameObject::CreateGameObject(ida::GameObjectType::Model);
    vase2.model = model;
    vase2.transform.translation = {.5f, .5f, 0.f};
    vase2.transform.scale = {3.f, 1.5f, 3.f};
    gameObjects_.emplace(vase2.GetId(), std::move(vase2));

    model = ida::IdaModel::ImportModel("models/quad.obj");
    auto quad = ida::IdaGameObject::CreateGameObject(ida::GameObjectType::Model);
    quad.model = model;
    quad.transform.translation = {0.f, .5f, 0.f};
    quad.transform.scale = {300.f, 100.f, 300.f};
    gameObjects_.emplace(quad.GetId(), std::move(quad));

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
        auto pointLight = ida::IdaGameObject::MakePointLight(0.2f);
        pointLight.color = lightColors[i];
        auto rotateLight = glm::rotate(
            glm::mat4(1.f),
            (i * glm::two_pi<float>()) / lightColors.size(),
            {0.f, -1.f, 0.f});
        pointLight.transform.translation = glm::vec3(rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f));
        gameObjects_.emplace(pointLight.GetId(), std::move(pointLight));
    }
}
