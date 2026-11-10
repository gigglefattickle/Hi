// Android OpenXR loader and runtime discovery. The Vulkan device/session hookup lives in nvn_vk.cpp;
// keeping this bootstrap separate lets the ordinary Android surface path remain unchanged.
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <android/log.h>
#include <android_native_app_glue.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
XrInstance g_xr_instance = XR_NULL_HANDLE;
XrSystemId g_xr_system = XR_NULL_SYSTEM_ID;
XrSession g_xr_session = XR_NULL_HANDLE;
uint32_t g_vk_api_version = VK_API_VERSION_1_1;


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

// Small C++ ABI used by nvn_vk.cpp. Opaque Vulkan arguments keep OpenXR headers out of
// non-Android renderer builds while still routing the actual Vulkan create calls through XR.
bool xr_android_is_available() {
    return g_xr_instance != XR_NULL_HANDLE && g_xr_system != XR_NULL_SYSTEM_ID;
}

uint32_t xr_android_vulkan_api_version() {
    return g_vk_api_version;
}

bool xr_android_create_vulkan_instance(void* gipa_ptr, void* vk_ci_ptr, void* instance_ptr, void* vk_result_ptr) {
    if (!xr_android_is_available() || !gipa_ptr || !vk_ci_ptr || !instance_ptr || !vk_result_ptr) return false;
    PFN_xrCreateVulkanInstanceKHR create = nullptr;
    XrResult r = xrGetInstanceProcAddr(g_xr_instance, "xrCreateVulkanInstanceKHR",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&create));
    if (XR_FAILED(r) || !create) return false;
    XrVulkanInstanceCreateInfoKHR ci{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    ci.systemId = g_xr_system;
    ci.pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(gipa_ptr);
    ci.vulkanCreateInfo = static_cast<const VkInstanceCreateInfo*>(vk_ci_ptr);
    r = create(g_xr_instance, &ci, static_cast<VkInstance*>(instance_ptr), static_cast<VkResult*>(vk_result_ptr));
    if (XR_FAILED(r) || *static_cast<VkResult*>(vk_result_ptr) != VK_SUCCESS) {
        xr_log("xrCreateVulkanInstanceKHR failed", r);
        return false;
    }
    return true;
}

bool xr_android_get_vulkan_physical_device(void* instance_ptr, void* physical_ptr) {
    if (!xr_android_is_available() || !instance_ptr || !physical_ptr) return false;
    PFN_xrGetVulkanGraphicsDevice2KHR get_device = nullptr;
    XrResult r = xrGetInstanceProcAddr(g_xr_instance, "xrGetVulkanGraphicsDevice2KHR",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&get_device));
    if (XR_FAILED(r) || !get_device) return false;
    XrVulkanGraphicsDeviceGetInfoKHR info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    info.systemId = g_xr_system;
    info.vulkanInstance = *static_cast<VkInstance*>(instance_ptr);
    r = get_device(g_xr_instance, &info, static_cast<VkPhysicalDevice*>(physical_ptr));
    if (XR_FAILED(r)) xr_log("xrGetVulkanGraphicsDevice2KHR failed", r);
    return XR_SUCCEEDED(r);
}

bool xr_android_create_vulkan_device(void* gipa_ptr, void* physical_ptr, void* vk_ci_ptr,
                                     void* device_ptr, void* vk_result_ptr) {
    if (!xr_android_is_available() || !gipa_ptr || !physical_ptr || !vk_ci_ptr || !device_ptr || !vk_result_ptr)
        return false;
    PFN_xrCreateVulkanDeviceKHR create = nullptr;
    XrResult r = xrGetInstanceProcAddr(g_xr_instance, "xrCreateVulkanDeviceKHR",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&create));
    if (XR_FAILED(r) || !create) return false;
    XrVulkanDeviceCreateInfoKHR ci{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    ci.systemId = g_xr_system;
    ci.pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(gipa_ptr);
    ci.vulkanPhysicalDevice = *static_cast<VkPhysicalDevice*>(physical_ptr);
    ci.vulkanCreateInfo = static_cast<const VkDeviceCreateInfo*>(vk_ci_ptr);
    r = create(g_xr_instance, &ci, static_cast<VkDevice*>(device_ptr), static_cast<VkResult*>(vk_result_ptr));
    if (XR_FAILED(r) || *static_cast<VkResult*>(vk_result_ptr) != VK_SUCCESS) {
        xr_log("xrCreateVulkanDeviceKHR failed", r);
        return false;
    }
    return true;
}

bool xr_android_create_session(void* instance_ptr, void* physical_ptr, void* device_ptr,
                               uint32_t queue_family, uint32_t queue_index) {
    if (!xr_android_is_available() || !instance_ptr || !physical_ptr || !device_ptr) return false;
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = *static_cast<VkInstance*>(instance_ptr);
    binding.physicalDevice = *static_cast<VkPhysicalDevice*>(physical_ptr);
    binding.device = *static_cast<VkDevice*>(device_ptr);
    binding.queueFamilyIndex = queue_family;
    binding.queueIndex = queue_index;
    XrSessionCreateInfo ci{XR_TYPE_SESSION_CREATE_INFO};
    ci.next = &binding;
    ci.systemId = g_xr_system;
    XrResult r = xrCreateSession(g_xr_instance, &ci, &g_xr_session);
    if (XR_FAILED(r)) xr_log("xrCreateSession failed", r);
    return XR_SUCCEEDED(r);
}

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

    PFN_xrGetVulkanGraphicsRequirements2KHR get_requirements = nullptr;
    r = xrGetInstanceProcAddr(g_xr_instance, "xrGetVulkanGraphicsRequirements2KHR",
                              reinterpret_cast<PFN_xrVoidFunction*>(&get_requirements));
    if (XR_FAILED(r) || !get_requirements) {
        xr_log("xrGetVulkanGraphicsRequirements2KHR is unavailable", r);
        xrDestroyInstance(g_xr_instance);
        g_xr_instance = XR_NULL_HANDLE;
        g_xr_system = XR_NULL_SYSTEM_ID;
        return false;
    }
    XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    r = get_requirements(g_xr_instance, g_xr_system, &requirements);
    if (XR_FAILED(r)) {
        xr_log("OpenXR Vulkan graphics requirements query failed", r);
        xrDestroyInstance(g_xr_instance);
        g_xr_instance = XR_NULL_HANDLE;
        g_xr_system = XR_NULL_SYSTEM_ID;
        return false;
    }
    const uint32_t runtime_max = VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(requirements.maxApiVersionSupported),
                                                       XR_VERSION_MINOR(requirements.maxApiVersionSupported), 0);
    g_vk_api_version = std::min(VK_API_VERSION_1_3, runtime_max);
    const uint32_t runtime_min = VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(requirements.minApiVersionSupported),
                                                       XR_VERSION_MINOR(requirements.minApiVersionSupported), 0);
    if (g_vk_api_version < runtime_min) {
        xr_log("OpenXR runtime Vulkan version range is incompatible");
        xrDestroyInstance(g_xr_instance);
        g_xr_instance = XR_NULL_HANDLE;
        g_xr_system = XR_NULL_SYSTEM_ID;
        return false;
    }
    xr_log("OpenXR loader initialized; Quest HMD system discovered");
    
    return true;
}
