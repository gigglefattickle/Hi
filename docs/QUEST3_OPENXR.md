# Quest 3 OpenXR app: renderer integration plan

The Quest build is a separate Android application (`io.github.acnhrecomp.quest3`, displayed as **ACNH VR**) and produces `acnh-vr.apk`. It uses `android/quest3/AndroidManifest.xml`; the existing phone app keeps `android/AndroidManifest.xml`, its original package ID, and its existing build output. Build the standalone Quest app with `tools/build_android.sh --quest3 --no-game --jobs 2`. The Quest app has its own Android app data directory, so its game-folder selection and saved app settings are separate from the phone app.


This document records the implementation boundary for a native Quest 3 build. It is deliberately separate from the existing Android phone build until the XR renderer is complete; the phone APK must continue to launch normally.

## Current architecture

- `android/AndroidManifest.xml` launches a Java home screen and then an Android `NativeActivity`.
- `runtime/src/platform_android.cpp` owns the Android native app glue, activity/window lifecycle, Vulkan loader selection, and Android Vulkan surface creation.
- `runtime/src/nvn/nvn_vk.cpp` owns the Vulkan instance, physical device, logical device, graphics queue, Android `VkSurfaceKHR`, `VkSwapchainKHR`, and presentation.
- `runtime/src/nvn/nvn_draw.cpp` replays the game's NVN command lists into Vulkan render passes.
- `runtime/src/platform_android_input.cpp` maps Android gamepad/touch events and now also maps OpenXR Touch actions into the game's conventional pad state.

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

The existing Android surface path remains as a fallback when no OpenXR runtime is available. On the Quest path, game frames are copied into the two OpenXR eye swapchains and submitted as a projection layer.

### 4. Camera and world scale

Use OpenXR's located view poses and FOVs to build left/right camera matrices. Keep the game's existing world coordinates and gameplay camera logic as the base; apply the tracked head pose as a VR camera offset rather than changing the game world or moving the player root on every head movement. Calibrate scale and floor height on device.

### 5. Quest Touch controller actions

Create an OpenXR action set for left/right controller poses, trigger, grip, face buttons, thumbsticks, and stick clicks. Translate button states into the existing `PadState` where that is semantically appropriate, and expose tracked controller poses separately for VR interaction. Do not treat a controller's Android gamepad event as a tracked pose.

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

The branch now contains a first end-to-end OpenXR presentation and input path:

- `runtime/src/openxr_android.cpp` initializes the Khronos Android loader, enables `XR_KHR_android_create_instance`, `XR_KHR_vulkan_enable`, and `XR_KHR_vulkan_enable2`, discovers the HMD, queries runtime-required Vulkan extensions, and creates the OpenXR session on the game's Vulkan device.
- `runtime/src/nvn/nvn_vk.cpp` creates the Vulkan instance/device through OpenXR, creates the session, polls the frame timing, acquires both eye images, copies the current game frame to each eye, and submits a two-view projection layer.
- The session state handler begins on `XR_SESSION_STATE_READY` and ends on `XR_SESSION_STATE_STOPPING`. Android's main loop polls OpenXR events.
- Two primary-stereo swapchains are created using the runtime's recommended per-eye dimensions. The frame loop uses `xrWaitFrame`, `xrBeginFrame`, `xrLocateViews`, `xrAcquireSwapchainImage`, `xrReleaseSwapchainImage`, and `xrEndFrame`.
- Headset view poses and left/right Touch controller poses are sampled. Touch trigger, grip, thumbstick, face buttons, menu, and stick-click actions are mapped into the game's existing pad input. `plat::read_xr_head_pose` and `plat::read_xr_controller_pose` expose the tracked poses to later camera/hand-model integration.
- OpenXR queue-access calls are synchronized with the renderer's Vulkan queue mutex.

**Important visual limitation:** both eye swapchains currently receive the same already-rendered game image. This gives the runtime two valid eye images and a projection-layer submission, but it is not true geometry-based stereoscopic rendering. The game's scene is not yet rendered twice from separate left/right camera matrices, and the tracked head pose has not yet been applied to the game's world camera. Controller inputs are mapped, while tracked controller poses are exposed for the next interaction/camera integration step.

**Build status:** source changes have been committed, but no Android build or Quest 3 hardware test has been run. No APK is verified. A local ARM64 build and on-device runtime validation are still required.

The next visual milestone is true per-eye game rendering with separate camera view/projection matrices and the tracked head pose, rather than copying one flat image to both eyes.

## Camera discovery diagnostic

The ACNH `sead::LookAtCamera::doUpdateMatrix` hook records world-camera transforms for temporal diagnostics. On the Quest build it can also be enabled independently of temporal upscaling with `NX_XR_CAMERA_TRACE=1`. Each second it logs the camera object's heap-relative address, the game callsite, and its position/target/up vectors. This is observational only; it does not move the camera or alter the player.

Use the trace from a repeatable scene (field, indoors, and while the player animates) to distinguish the player-follow camera from UI/map cameras. The repository does not contain ACNH model assets or a verified player skeleton/bone palette, so this trace alone cannot identify the animated head bone. That still requires a runtime model/skinning trace or game-asset inspection before a head-anchored first-person camera can be implemented safely.

## References

- Meta OpenXR support and Android loader setup: https://developers.meta.com/vr/documentation/native/android/mobile-openxr/
- Meta OpenXR instance/session and Vulkan graphics binding: https://developers.meta.com/vr/documentation/native/android/mobile-openxr-instance-session/
- Meta OpenXR swapchains: https://developers.meta.com/vr/documentation/native/android/mobile-openxr-swapchains/
- Khronos OpenXR SDK sample: https://github.com/KhronosGroup/OpenXR-SDK-Source/tree/main/src/tests/hello_xr


## Quest camera trace (no PC required)

On Quest 3, open **Settings → VR screen → Camera diagnostics**, enable **Camera Trace**, then restart the app. Play for at least 30 seconds in each useful situation (outdoors standing, walking, turning, and indoors). The app appends one sample per second to:

`<app data folder>/xr-camera-trace.txt`

Return to Settings → VR screen and choose **Open Camera Trace Folder**. Use the Quest file picker to locate and share/copy `xr-camera-trace.txt` using an available file-sharing destination. The trace contains camera transforms and game-relative object/callsite offsets; it does not change the player's world position. Disable Camera Trace when finished to avoid unnecessary writes.
