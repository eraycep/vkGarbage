#include "EditorUi.hpp"
#include "VulkanContext.hpp"
#include "Window.hpp"
#include "Scene.hpp"
#include "Common.hpp"
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <algorithm>
#include <stdexcept>

EditorUi::EditorUi(VulkanContext& context, const Window& window) : context_(context)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().IniFilename = nullptr; // No generated imgui.ini in the project.
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForVulkan(window.nativeHandle())) {
        ImGui::DestroyContext();
        throw std::runtime_error("Could not initialize ImGui SDL backend");
    }
}

EditorUi::~EditorUi()
{
    // Renderer waits for the device before destroying this object.
    if (vulkanReady_) ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void EditorUi::processEvent(const SDL_Event& event) { ImGui_ImplSDL3_ProcessEvent(&event); }
bool EditorUi::wantsMouse() const { return ImGui::GetIO().WantCaptureMouse; }
bool EditorUi::wantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

void EditorUi::prepare(VkFormat colorFormat, VkFormat depthFormat, std::uint32_t imageCount)
{
    imageCount = std::max(2u, imageCount);
    if (vulkanReady_ && colorFormat == colorFormat_ && depthFormat == depthFormat_ && imageCount == imageCount_) return;
    if (vulkanReady_) {
        context_.waitIdle();
        ImGui_ImplVulkan_Shutdown();
        vulkanReady_ = false;
    }
    colorFormat_ = colorFormat;
    depthFormat_ = depthFormat;
    imageCount_ = imageCount;
    chk(ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3,
        [](const char* name, void* data) {
            return vkGetInstanceProcAddr(static_cast<VulkanContext*>(data)->instance(), name);
        }, &context_));
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = context_.instance();
    info.PhysicalDevice = context_.physicalDevice();
    info.Device = context_.device();
    info.QueueFamily = context_.graphicsQueueFamily();
    info.Queue = context_.graphicsQueue();
    info.DescriptorPoolSize = 64; // Backend owns a separate pool for UI textures.
    info.MinImageCount = 2;
    info.ImageCount = imageCount_;
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat_;
    info.PipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat_;
    info.CheckVkResultFn = [](VkResult result) { chk(result); };
    chk(ImGui_ImplVulkan_Init(&info));
    vulkanReady_ = true;
}

void EditorUi::build(Scene& scene)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(330, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scene editor")) {
        ImGui::Text("Selected object: %u", scene.selectedObject());
        ImGui::TextWrapped("WASD: camera movement. Left drag outside this panel: rotate selected object. Left/Right arrows: selection.");
        ImGui::Separator();
        int debugMode = static_cast<int>(scene.debugMode());
        const char* debugModes[] = {"Lit", "Normals (view space)", "Shadow visibility", "Shadow depth"};
        if (ImGui::Combo("Debug view", &debugMode, debugModes, 4)) {
            scene.setDebugMode(static_cast<Scene::DebugMode>(debugMode));
        }
        if (scene.debugMode() == Scene::DebugMode::ShadowDepth) {
            ImGui::TextWrapped("Light depth: near is dark, far is white. Empty pixels are white.");
        }
        ImGui::Separator();
        // Add light widgets here using scene.light(), and transform widgets using
        // scene.object(scene.selectedObject()). Changes reach the GPU this frame.
        auto& light = scene.light();
        glm::vec3 direction = light.direction;
        if (ImGui::DragFloat3("Direction", &direction.x, 0.01f)) {
                if (glm::dot(direction, direction) > 0.000001f) {
                light.direction = direction;
                }
        }
        ImGui::TextUnformatted("Light and transform controls go here.");
        ImGui::Checkbox("Show ImGui examples", &showDemo_);
        ImGui::SliderFloat3("Light Position", (float*)&scene.light().position, -10.0f, 10.0f);
        // ImGui::SliderFloat3("Light Direction", (float*)&scene.light().direction, -3.0f, 3.0f);
        ImGui::ColorEdit3("Light Color", (float*)&scene.light().color);
        ImGui::SliderFloat("Light Intensity", (float*)&scene.light().intensity, 0.0f, 10.0f);
        ImGui::SliderFloat("Inner cone", &light.innerConeDegrees, 0.1f, light.outerConeDegrees - 0.1f, "%.1f deg");
        light.innerConeDegrees = std::clamp(light.innerConeDegrees, 0.1f, light.outerConeDegrees - 0.1f);
        ImGui::SliderFloat("Outer cone", &light.outerConeDegrees, light.innerConeDegrees + 0.1f, 89.0f, "%.1f deg");
        light.outerConeDegrees = std::clamp(light.outerConeDegrees, 0.2f, 89.0f);
        light.intensity = std::max(light.intensity, 0.0f);
    }
    ImGui::End();
    if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
    ImGui::Render();
}

void EditorUi::record(VkCommandBuffer cb)
{
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
}
