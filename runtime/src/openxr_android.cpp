// Android OpenXR loader and runtime discovery. The Vulkan device/session hookup lives in nvn_vk.cpp;
// keeping this bootstrap separate lets the ordinary Android surface path remain unchanged.
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <android/log.h>
#include <android_native_app_glue.h>
#include <cstring>
#include <string>
#include <vector>

namespace {
XrInstance g_xr_instance = XR_NULL_HANDLE;
XrSystemId g_xr_system = XR_NULL_SYSTEM_ID;

void xr_log(const char* message, XrResult result = XR_SUCCESS) {
    __android_log_print(result < 0 ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "ACNH-OpenXR",
                        "%s (XrResult=%d)", message, int(result));
}

bool has_extension(const std::vector<XrExtensionProperties>& extensions, const char* name) {
    for (const auto& e : extensions)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}
} // namespace

// Called from android_main after the NativeActivity is available. This deliberately stops at
// runtime/system discovery: a session must not be created until nvn_vk has an XR-compatible
// VkInstance/VkDevice and the renderer can submit OpenXR swapchain images.
bool xr_android_initialize(android_app* app) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) {
        xr_log("NativeActivity context is unavailable");
        return false;
    }

    PFN_xrInitializeLoaderKHR initialize_loader = nullptr;
    XrResult r = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&initialize_loader));
    if (XR_FAILED(r) || !initialize_loader) {
        xr_log("xrInitializeLoaderKHR is unavailable", r);
        return false;
    }

    XrLoaderInitInfoAndroidKHR loader_info{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader_info.applicationVM = app->activity->vm;
    loader_info.applicationContext = app->activity->clazz;
    r = initialize_loader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loader_info));
    if (XR_FAILED(r)) {
        xr_log("OpenXR Android loader initialization failed", r);
        return false;
    }

    uint32_t count = 0;
    r = xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
    if (XR_FAILED(r) || count == 0) {
        xr_log("No OpenXR instance extensions were reported", r);
        return false;
    }
    std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
    r = xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data());
    if (XR_FAILED(r)) {
        xr_log("OpenXR extension enumeration failed", r);
        return false;
    }

    // Android instance creation is mandatory on Android; Vulkan enable2 is the intended renderer bridge.
    if (!has_extension(extensions, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) ||
        !has_extension(extensions, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)) {
        xr_log("Runtime lacks XR_KHR_android_create_instance or XR_KHR_vulkan_enable2");
        return false;
    }

    const char* enabled[] = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME
    };
    XrInstanceCreateInfoAndroidKHR android_info{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android_info.applicationVM = app->activity->vm;
    android_info.applicationActivity = app->activity->clazz;

    XrInstanceCreateInfo create_info{XR_TYPE_INSTANCE_CREATE_INFO};
    create_info.next = &android_info;
    create_info.enabledExtensionCount = uint32_t(sizeof(enabled) / sizeof(enabled[0]));
    create_info.enabledExtensionNames = enabled;
    std::strncpy(create_info.applicationInfo.applicationName, "ACNH Recomp",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    create_info.applicationInfo.applicationVersion = 1;
    std::strncpy(create_info.applicationInfo.engineName, "Hi Recomp Runtime",
                 XR_MAX_ENGINE_NAME_SIZE - 1);
    create_info.applicationInfo.engineVersion = 1;
    create_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

    r = xrCreateInstance(&create_info, &g_xr_instance);
    if (XR_FAILED(r)) {
        xr_log("xrCreateInstance failed", r);
        return false;
    }

    XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(g_xr_instance, &system_info, &g_xr_system);
    if (XR_FAILED(r)) {
        xr_log("No head-mounted OpenXR system is available", r);
        xrDestroyInstance(g_xr_instance);
        g_xr_instance = XR_NULL_HANDLE;
        return false;
    }

    xr_log("OpenXR loader initialized; Quest HMD system discovered");
    return true;
}
