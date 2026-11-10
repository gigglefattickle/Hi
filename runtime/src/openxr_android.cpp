// Android OpenXR loader and runtime discovery. The Vulkan device/session hookup lives in nvn_vk.cpp;
// keeping this bootstrap separate lets the ordinary Android surface path remain unchanged.
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
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

bool xr_android_create_vulkan_instance(PFN_vkGetInstanceProcAddr gipa, const VkInstanceCreateInfo* vk_ci,
                                       VkInstance* instance, VkResult* vk_result) {
    if (!xr_android_is_available() || !gipa || !vk_ci || !instance || !vk_result) return false;
    PFN_xrCreateVulkanInstanceKHR create = nullptr;
    XrResult r = xrGetInstanceProcAddr(g_xr_instance, "xrCreateVulkanInstanceKHR",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&create));
    if (XR_FAILED(r) || !create) return false;
    XrVulkanInstanceCreateInfoKHR ci{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    ci.systemId = g_xr_system;
    ci.pfnGetInstanceProcAddr = gipa;
    ci.vulkanCreateInfo = vk_ci;
    r = create(g_xr_instance, &ci, instance, vk_result);
    if (XR_FAILED(r) || *vk_result != VK_SUCCESS) {
        xr_log("xrCreateVulkanInstanceKHR failed", r);
        return false;
    }
    return true;
}

bool xr_android_get_vulkan_physical_device(VkInstance instance, VkPhysicalDevice* physical_device) {
    if (!xr_android_is_available() || instance == VK_NULL_HANDLE || !physical_device) return false;
    PFN_xrGetVulkanGraphicsDevice2KHR get_device = nullptr;
    XrResult r = xrGetInstanceProcAddr(g_xr_instance, "xrGetVulkanGraphicsDevice2KHR",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&get_device));
    if (XR_FAILED(r) || !get_device) return false;
    XrVulkanGraphicsDeviceGetInfoKHR info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    info.systemId = g_xr_system;
    info.vulkanInstance = instance;
    r = get_device(g_xr_instance, &info, physical_device);
    if (XR_FAILED(r)) xr_log("xrGetVulkanGraphicsDevice2KHR failed", r);
    return XR_SUCCEEDED(r);
}

bool xr_android_create_vulkan_device(PFN_vkGetInstanceProcAddr gipa, VkPhysicalDevice physical_device,
                                     const VkDeviceCreateInfo* vk_ci, VkDevice* device, VkResult* vk_result) {
    if (!xr_android_is_available() || !gipa || physical_device == VK_NULL_HANDLE || !vk_ci || !device || !vk_result)
        return false;
    PFN_xrCreateVulkanDeviceKHR create = nullptr;
    XrResult r = xrGetInstanceProcAddr(g_xr_instance, "xrCreateVulkanDeviceKHR",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&create));
    if (XR_FAILED(r) || !create) return false;
    XrVulkanDeviceCreateInfoKHR ci{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    ci.systemId = g_xr_system;
    ci.pfnGetInstanceProcAddr = gipa;
    ci.vulkanPhysicalDevice = physical_device;
    ci.vulkanCreateInfo = vk_ci;
    r = create(g_xr_instance, &ci, device, vk_result);
    if (XR_FAILED(r) || *vk_result != VK_SUCCESS) {
        xr_log("xrCreateVulkanDeviceKHR failed", r);
        return false;
    }
    return true;
}

bool xr_android_create_session(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
                               uint32_t queue_family, uint32_t queue_index) {
    if (!xr_android_is_available() || instance == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE ||
        device == VK_NULL_HANDLE) return false;
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = instance;
    binding.physicalDevice = physical_device;
    binding.device = device;
    binding.queueFamilyIndex = queue_family;
    binding.queueIndex = queue_index;
    XrSessionCreateInfo ci{XR_TYPE_SESSION_CREATE_INFO};
    ci.next = &binding;
    ci.systemId = g_xr_system;
    XrResult r = xrCreateSession(g_xr_instance, &ci, &g_xr_session);
    if (XR_FAILED(r)) { xr_log("xrCreateSession failed", r); return false; }
    if (!xr_load_frame_api() || !xr_create_stereo()) { xr_log("OpenXR stereo setup failed"); return false; }
    xr_log("OpenXR stereo swapchains and reference spaces initialized");
    return true;
}


namespace {
bool g_xr_running=false,g_xr_frame=false,g_xr_render=false;
XrSessionState g_xr_state=XR_SESSION_STATE_UNKNOWN;
XrSpace g_xr_local=XR_NULL_HANDLE,g_xr_view=XR_NULL_HANDLE;
XrViewConfigurationView g_xr_config[2]={{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
XrView g_xr_views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
XrSwapchain g_xr_swapchains[2]={XR_NULL_HANDLE,XR_NULL_HANDLE};
std::vector<XrSwapchainImageVulkan2KHR> g_xr_images[2];
uint32_t g_xr_indices[2]={}; bool g_xr_acquired[2]={};
XrTime g_xr_time=0;
PFN_xrPollEvent p_poll=nullptr;
PFN_xrBeginSession p_begin=nullptr;
PFN_xrEndSession p_end=nullptr;
PFN_xrWaitFrame p_wait=nullptr;
PFN_xrBeginFrame p_frame_begin=nullptr;
PFN_xrEndFrame p_frame_end=nullptr;
PFN_xrLocateViews p_locate_views=nullptr;
PFN_xrCreateReferenceSpace p_create_space=nullptr;
PFN_xrEnumerateViewConfigurationViews p_enum_views=nullptr;
PFN_xrEnumerateSwapchainFormats p_enum_formats=nullptr;
PFN_xrCreateSwapchain p_create_swapchain=nullptr;
PFN_xrEnumerateSwapchainImages p_enum_images=nullptr;
PFN_xrAcquireSwapchainImage p_acquire=nullptr;
PFN_xrWaitSwapchainImage p_wait_image=nullptr;
PFN_xrReleaseSwapchainImage p_release=nullptr;
template<class T> bool xr_load(const char* n,T& f) {
 XrResult r=xrGetInstanceProcAddr(g_xr_instance,n,reinterpret_cast<PFN_xrVoidFunction*>(&f));
 if(XR_FAILED(r)||!f){xr_log(n,r);return false;} return true;
}
bool xr_load_frame_api(){
 return xr_load("xrPollEvent",p_poll)&&xr_load("xrBeginSession",p_begin)&&xr_load("xrEndSession",p_end)&&
 xr_load("xrWaitFrame",p_wait)&&xr_load("xrBeginFrame",p_frame_begin)&&xr_load("xrEndFrame",p_frame_end)&&
 xr_load("xrLocateViews",p_locate_views)&&xr_load("xrCreateReferenceSpace",p_create_space)&&
 xr_load("xrEnumerateViewConfigurationViews",p_enum_views)&&xr_load("xrEnumerateSwapchainFormats",p_enum_formats)&&
 xr_load("xrCreateSwapchain",p_create_swapchain)&&xr_load("xrEnumerateSwapchainImages",p_enum_images)&&
 xr_load("xrAcquireSwapchainImage",p_acquire)&&xr_load("xrWaitSwapchainImage",p_wait_image)&&
 xr_load("xrReleaseSwapchainImage",p_release);
}
bool xr_create_stereo(){
 uint32_t n=0; XrResult r=p_enum_views(g_xr_instance,g_xr_system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&n,nullptr);
 if(XR_FAILED(r)||n!=2)return false;
 r=p_enum_views(g_xr_instance,g_xr_system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&n,g_xr_config);
 if(XR_FAILED(r))return false;
 XrReferenceSpaceCreateInfo ri{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
 ri.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;ri.poseInReferenceSpace.orientation.w=1;
 if(XR_FAILED(p_create_space(g_xr_session,&ri,&g_xr_local)))return false;
 ri.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
 if(XR_FAILED(p_create_space(g_xr_session,&ri,&g_xr_view)))return false;
 uint32_t fc=0;r=p_enum_formats(g_xr_session,0,&fc,nullptr);if(XR_FAILED(r)||!fc)return false;
 std::vector<int64_t> formats(fc);r=p_enum_formats(g_xr_session,fc,&fc,formats.data());if(XR_FAILED(r))return false;
 int64_t fmt=0;for(auto f:formats)if(f==VK_FORMAT_R8G8B8A8_UNORM){fmt=f;break;}
 if(!fmt)for(auto f:formats)if(f==VK_FORMAT_B8G8R8A8_UNORM){fmt=f;break;}
 if(!fmt)return false;
 for(uint32_t e=0;e<2;e++){
  XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
  ci.format=fmt;ci.sampleCount=1;ci.width=g_xr_config[e].recommendedImageRectWidth;
  ci.height=g_xr_config[e].recommendedImageRectHeight;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
  r=p_create_swapchain(g_xr_session,&ci,&g_xr_swapchains[e]);if(XR_FAILED(r))return false;
  uint32_t count=0;r=p_enum_images(g_xr_swapchains[e],0,&count,nullptr);if(XR_FAILED(r)||!count)return false;
  g_xr_images[e].assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
  r=p_enum_images(g_xr_swapchains[e],count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(g_xr_images[e].data()));
  if(XR_FAILED(r))return false;
 }
 return true;
}
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
