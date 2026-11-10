# Quest 3 OpenXR port: renderer integration plan

This document records the implementation boundary for a native Quest 3 build. It is deliberately separate from the existing Android phone build until the XR renderer is complete; the phone APK must continue to launch normally.

## Current architecture

- `android/AndroidManifest.xml` launches a Java home screen and then an Android `NativeActivity`.
- `runtime/src/platform_android.cpp` owns the Android native app glue, activity/window lifecycle, Vulkan loader selection, and Android Vulkan surface creation.
- `runtime/src/nvn/nvn_vk.cpp` owns the Vulkan instance, physical device, logical device, graphics queue, Android `VkSurfaceKHR`, `VkSwapchainKHR`, and presentation.
- `runtime/src/nvn/nvn_draw.cpp` replays the game's NVN command lists into Vulkan render passes.
- `runtime/src/platform_android_input.cpp` currently maps Android gamepad/touch events to the game's conventional pad state. It does not expose tracked controller poses or OpenXR actions.

The existing Android path is a flat-screen Vulkan presentation path. Adding only the OpenXR loader or the immersive-HMD manifest category would not make the renderer stereo.

## Required architecture changes

### 1. OpenXR Android bootstrap

Add a Quest-specific launch/build variant without changing the normal phone manifest. The Quest variant must:

- declare the OpenXR immersive-HMD launcher category;
- initialize the Khronos Android loader using `XR_KHR_loader_init_android` and the activity's `JavaVM` / `jobject`;
- create an OpenXR instance with `XR_KHR_android_create_instance`;
- select `XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY`;
- process OpenXR session state and Android lifecycle transitions safely.

The loader version must meet Meta's documented minimum (1.0.34 or newer).

### 2. One Vulkan device shared with OpenXR

The Vulkan backend currently creates its own instance and device in `runtime/src/nvn/nvn_vk.cpp`. OpenXR's Vulkan graphics binding must use the exact Vulkan instance, physical device, device, and queue used by the game.

Before creating the Vulkan instance/device, query OpenXR's required Vulkan instance/device extensions and graphics requirements. Enable those extensions and preserve the project's existing feature/extension negotiation. Then create the OpenXR session with the Vulkan graphics binding. Do not create a second unrelated Vulkan device.

### 3. Replace flat presentation with OpenXR swapchains

The existing `VkSurfaceKHR` / `VkSwapchainKHR` path and `vkQueuePresentKHR` presenter are screen-oriented. The Quest path must instead:

- enumerate `PRIMARY_STEREO` view configuration and per-eye recommended sizes;
- create OpenXR swapchains backed by Vulkan images compatible with the shared device;
- acquire, wait for, render to, and release XR swapchain images each frame;
- submit a projection layer with the located pose and FOV for each eye;
- obey `xrWaitFrame`, `xrBeginFrame`, and `xrEndFrame` pacing;
- handle session stop/loss and Android window/activity recreation.

The current flat Android swapchain remains available for the non-Quest build.

### 4. Camera and world scale

Use OpenXR's located view poses and FOVs to build left/right camera matrices. Keep the game's existing world coordinates and gameplay camera logic as the base; apply the tracked head pose as a VR camera offset rather than changing the game world or moving the player root on every head movement. Calibrate scale and floor height on device.

### 5. Quest Touch controller actions

Create an OpenXR action set for left/right controller poses, aim poses, trigger, grip, face buttons, thumbsticks, and haptics. Translate button states into the existing `PadState` where that is semantically appropriate, and expose tracked hand/controller poses separately for future VR interaction. Do not treat a controller's Android gamepad event as a tracked pose.

### 6. Performance and visibility

Do not introduce distance-based object culling as part of the initial XR bring-up. First capture CPU replay timings, GPU timings, eye render resolution, and frame pacing on Quest 3. Any later visibility optimization must preserve gameplay-critical objects and be validated against reference screenshots/scenes.

## Milestones and acceptance criteria

1. **XR bootstrap:** Quest recognizes the app as immersive; OpenXR instance and session reach READY/RUNNING; no game rendering is required yet.
2. **Shared Vulkan binding:** OpenXR session is created using the game's actual Vulkan device and queue; no duplicate device is created.
3. **Stereo presentation:** both eye views render and are submitted as one projection layer; head rotation changes the view correctly.
4. **Head pose:** six-degree-of-freedom pose updates camera orientation and position with stable recentering and world scale.
5. **Controller actions:** left/right poses and buttons are logged and mapped; no loss of existing pad controls.
6. **Quest package:** arm64 APK has the immersive-HMD launcher category, packages the Khronos loader, and installs/launches on Quest 3.
7. **Performance pass:** profile a repeatable scene on Quest 3, then optimize measured bottlenecks without hiding important objects.

## Current implementation status

The first native bootstrap increment is now in the branch:

- `runtime/src/openxr_android.cpp` initializes the Khronos Android loader with the NativeActivity VM/context, checks for `XR_KHR_android_create_instance` and `XR_KHR_vulkan_enable2`, creates an OpenXR instance, and discovers the HMD system.
- `CMakeLists.txt` fetches Khronos OpenXR-SDK 1.1.58 and links the loader into the Android runtime.
- `tools/build_android.sh` packages `libopenxr_loader.so` into the ARM64 APK.
- `platform_android.cpp` runs the loader bootstrap before game startup. If no runtime is available, the existing flat-screen path remains the fallback.

**Not implemented yet:** Vulkan instance/device creation through `XR_KHR_vulkan_enable2`, `XrSession` creation, OpenXR frame loop, per-eye swapchains/projection layer, head-pose camera integration, Touch controller actions, and the Quest immersive launcher manifest. This is therefore not yet a playable VR build and no APK has been verified.

The next code milestone is to make `nvn_vk.cpp` obtain its Vulkan instance, XR-selected physical device, and logical device through OpenXR while preserving the existing feature chain. Only then can the session use the exact game device. A Quest 3 device run remains necessary to validate runtime startup, graphics binding, frame pacing, and controller tracking.

## References

- Meta OpenXR support and Android loader setup: https://developers.meta.com/vr/documentation/native/android/mobile-openxr/
- Meta OpenXR instance/session and Vulkan graphics binding: https://developers.meta.com/vr/documentation/native/android/mobile-openxr-instance-session/
- Meta OpenXR swapchains: https://developers.meta.com/vr/documentation/native/android/mobile-openxr-swapchains/
- Khronos OpenXR SDK sample: https://github.com/KhronosGroup/OpenXR-SDK-Source/tree/main/src/tests/hello_xr
