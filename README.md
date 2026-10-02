# vkGarbage
Vulkan graphics engine developed for learning purposes.

## Editor UI setup

Dear ImGui v1.91.9b is vendored in `external/imgui` with its MIT license.
CMake builds the SDL3 and Vulkan backends; no extra install is needed.

- Add light/transform widgets in `EditorUi::build(Scene&)` (`src/EditorUi.cpp`).
  `scene.light()` returns the editable spotlight; `scene.object(scene.selectedObject())`
  returns the selected object's transform. UI edits are uploaded before drawing that frame.
- Example widget: `ImGui::DragFloat3("Position", &scene.light().position.x, 0.05f);`.
  Keep light direction nonzero, `0 < innerConeDegrees < outerConeDegrees < 90`,
  `0 < shadowNearPlane < shadowFarPlane`, and object scale components nonzero.
- Use **Show ImGui examples** in the starter panel to browse available widgets.
- Camera mouse look and relative mouse mode are disabled. WASD still moves the
  camera; left drag outside the UI rotates the selected object; Left/Right arrows changes selection.
- Implement picking/dragging in `Application::handleEvent`. SDL events always go to
  ImGui first. Respect `renderer_->ui().wantsMouse()` and `wantsKeyboard()` before
  handling scene input. SDL mouse coordinates are in window coordinates; convert
  them to framebuffer coordinates if your picking implementation uses pixel dimensions.
- `Renderer` owns the UI, records it after the scene, and waits for GPU completion
  before teardown. The UI has its own descriptor pool and handles swapchain format/count changes.

Object picking, translation gizmos, and light widgets are intentionally left as
implementation exercises. The floor remains non-selectable through Left/Right arrows.
