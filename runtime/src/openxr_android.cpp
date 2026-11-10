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
#include <mutex>
#include <atomic>
#include <string>
#include <vector>

namespace nx::nvn { std::mutex& vk_queue_mutex(); }

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

bool xr_android_get_vulkan_instance_extensions(std::string* output) {
    if (!output || !xr_android_is_available()) return false;
    PFN_xrGetVulkanInstanceExtensionsKHR fn = nullptr;
    XrResult r = xrGetInstanceProcAddr(g_xr_instance, "xrGetVulkanInstanceExtensionsKHR",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&fn));
    if (XR_FAILED(r) || !fn) return false;
    uint32_t count = 0;
    r = fn(g_xr_instance, g_xr_system, 0, &count, nullptr);
    if (XR_FAILED(r) || !count) return false;
    std::vector<char> names(count);
    r = fn(g_xr_instance, g_xr_system, count, &count, names.data());
    if (XR_FAILED(r)) return false;
    *output = names.data();
    return true;
}
bool xr_android_get_vulkan_device_extensions(std::string* output) {
    if (!output || !xr_android_is_available()) return false;
    PFN_xrGetVulkanDeviceExtensionsKHR fn = nullptr;
    XrResult r = xrGetInstanceProcAddr(g_xr_instance, "xrGetVulkanDeviceExtensionsKHR",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&fn));
    if (XR_FAILED(r) || !fn) return false;
    uint32_t count = 0;
    r = fn(g_xr_instance, g_xr_system, 0, &count, nullptr);
    if (XR_FAILED(r) || !count) return false;
    std::vector<char> names(count);
    r = fn(g_xr_instance, g_xr_system, count, &count, names.data());
    if (XR_FAILED(r)) return false;
    *output = names.data();
    return true;
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

namespace { bool xr_load_frame_api(); bool xr_create_stereo(); bool xr_make_touch_actions(); }

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
    if (!xr_load_frame_api() || !xr_create_stereo() || !xr_make_touch_actions()) { xr_log("OpenXR stereo setup failed"); return false; }
    xr_log("OpenXR session, stereo swapchains, and Touch actions initialized");
    g_xr_setup_complete.store(true, std::memory_order_release);
    return true;
}


namespace {
std::atomic<bool> g_xr_running{false};
std::atomic<bool> g_xr_setup_complete{false};
bool g_xr_frame=false,g_xr_render=false;
XrSessionState g_xr_state=XR_SESSION_STATE_UNKNOWN;
XrSpace g_xr_local=XR_NULL_HANDLE,g_xr_view=XR_NULL_HANDLE;
XrViewConfigurationView g_xr_config[2]={{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
XrView g_xr_views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
XrSwapchain g_xr_swapchains[2]={XR_NULL_HANDLE,XR_NULL_HANDLE};
std::vector<XrSwapchainImageVulkan2KHR> g_xr_images[2];
uint32_t g_xr_indices[2]={}; bool g_xr_acquired[2]={};
XrTime g_xr_time=0;

std::mutex g_xr_tracking_mutex;
XrActionSet g_xr_action_set=XR_NULL_HANDLE;
XrAction g_xr_hand_pose=XR_NULL_HANDLE,g_xr_trigger=XR_NULL_HANDLE,g_xr_grip=XR_NULL_HANDLE,g_xr_stick=XR_NULL_HANDLE;
XrAction g_xr_a=XR_NULL_HANDLE,g_xr_b=XR_NULL_HANDLE,g_xr_x=XR_NULL_HANDLE,g_xr_y=XR_NULL_HANDLE,g_xr_menu=XR_NULL_HANDLE,g_xr_stick_click=XR_NULL_HANDLE;
bool g_xr_buttons[2][6]={};
XrSpace g_xr_hand_space[2]={XR_NULL_HANDLE,XR_NULL_HANDLE};
XrPosef g_xr_head_pose{{0,0,0,1},{0,0,0}};
XrPosef g_xr_hand_pose_value[2]={{{0,0,0,1},{0,0,0}},{{0,0,0,1},{0,0,0}}};
bool g_xr_hand_active[2]={};float g_xr_trigger_value[2]={},g_xr_grip_value[2]={};XrVector2f g_xr_stick_value[2]={};
PFN_xrCreateActionSet p_create_action_set=nullptr;PFN_xrCreateAction p_create_action=nullptr;
PFN_xrSuggestInteractionProfileBindings p_suggest_bindings=nullptr;PFN_xrAttachSessionActionSets p_attach_actions=nullptr;
PFN_xrSyncActions p_sync_actions=nullptr;PFN_xrGetActionStatePose p_get_pose=nullptr;
PFN_xrGetActionStateFloat p_get_float=nullptr;PFN_xrGetActionStateVector2f p_get_vector=nullptr;PFN_xrGetActionStateBoolean p_get_boolean=nullptr;
PFN_xrCreateActionSpace p_create_action_space=nullptr;PFN_xrLocateSpace p_locate_space=nullptr;
PFN_xrStringToPath p_string_to_path=nullptr;

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
 xr_load("xrReleaseSwapchainImage",p_release)&&
 xr_load("xrCreateActionSet",p_create_action_set)&&xr_load("xrCreateAction",p_create_action)&&
 xr_load("xrSuggestInteractionProfileBindings",p_suggest_bindings)&&xr_load("xrAttachSessionActionSets",p_attach_actions)&&
 xr_load("xrSyncActions",p_sync_actions)&&xr_load("xrGetActionStatePose",p_get_pose)&&
 xr_load("xrGetActionStateFloat",p_get_float)&&xr_load("xrGetActionStateVector2f",p_get_vector)&&xr_load("xrGetActionStateBoolean",p_get_boolean)&&
 xr_load("xrCreateActionSpace",p_create_action_space)&&xr_load("xrLocateSpace",p_locate_space)&&
 xr_load("xrStringToPath",p_string_to_path);
}

bool xr_make_touch_actions(){
 XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};std::strncpy(si.actionSetName,"gameplay",XR_MAX_ACTION_SET_NAME_SIZE-1);
 std::strncpy(si.localizedActionSetName,"Gameplay",XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE-1);
 if(XR_FAILED(p_create_action_set(g_xr_instance,&si,&g_xr_action_set)))return false;
 XrPath hands[2]{},profile{},trigger[2]{},grip[2]{},stick[2]{},pose[2]{};
 const char* names[]={"/user/hand/left","/user/hand/right","/interaction_profiles/oculus/touch_controller",
 "/user/hand/left/input/trigger/value","/user/hand/right/input/trigger/value",
 "/user/hand/left/input/squeeze/value","/user/hand/right/input/squeeze/value",
 "/user/hand/left/input/thumbstick","/user/hand/right/input/thumbstick",
 "/user/hand/left/input/grip/pose","/user/hand/right/input/grip/pose"};
 XrPath* out[]={&hands[0],&hands[1],&profile,&trigger[0],&trigger[1],&grip[0],&grip[1],&stick[0],&stick[1],&pose[0],&pose[1]};
 for(int i=0;i<11;i++)if(XR_FAILED(p_string_to_path(g_xr_instance,names[i],out[i])))return false;
 auto make=[&](const char*n,const char*l,XrActionType t,XrAction*dst){XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
 std::strncpy(ci.actionName,n,XR_MAX_ACTION_NAME_SIZE-1);std::strncpy(ci.localizedActionName,l,XR_MAX_LOCALIZED_ACTION_NAME_SIZE-1);
 ci.actionType=t;ci.countSubactionPaths=2;ci.subactionPaths=hands;return XR_SUCCEEDED(p_create_action(g_xr_action_set,&ci,dst));};
 if(!make("button_a","A",XR_ACTION_TYPE_BOOLEAN_INPUT,&g_xr_a)||
 !make("button_b","B",XR_ACTION_TYPE_BOOLEAN_INPUT,&g_xr_b)||
 !make("button_x","X",XR_ACTION_TYPE_BOOLEAN_INPUT,&g_xr_x)||
 !make("button_y","Y",XR_ACTION_TYPE_BOOLEAN_INPUT,&g_xr_y)||
 !make("menu","Menu",XR_ACTION_TYPE_BOOLEAN_INPUT,&g_xr_menu)||
 !make("stick_click","Stick click",XR_ACTION_TYPE_BOOLEAN_INPUT,&g_xr_stick_click))return false;
 if(!make("hand_pose","Hand pose",XR_ACTION_TYPE_POSE_INPUT,&g_xr_hand_pose)||
 !make("trigger","Trigger",XR_ACTION_TYPE_FLOAT_INPUT,&g_xr_trigger)||
 !make("grip","Grip",XR_ACTION_TYPE_FLOAT_INPUT,&g_xr_grip)||
 !make("thumbstick","Thumbstick",XR_ACTION_TYPE_VECTOR2F_INPUT,&g_xr_stick))return false;
 XrPath pa{},pb{},px{},py{},pm{},plc{},prc{};
 const char* button_paths[]={"/user/hand/right/input/a/click","/user/hand/right/input/b/click",
 "/user/hand/left/input/x/click","/user/hand/left/input/y/click","/user/hand/left/input/menu/click",
 "/user/hand/left/input/thumbstick/click","/user/hand/right/input/thumbstick/click"};
 XrPath* button_out[]={&pa,&pb,&px,&py,&pm,&plc,&prc};
 for(int i=0;i<7;i++)if(XR_FAILED(p_string_to_path(g_xr_instance,button_paths[i],button_out[i])))return false;
 XrActionSuggestedBinding b[]={{g_xr_hand_pose,pose[0]},{g_xr_hand_pose,pose[1]},
 {g_xr_trigger,trigger[0]},{g_xr_trigger,trigger[1]},{g_xr_grip,grip[0]},{g_xr_grip,grip[1]},
 {g_xr_stick,stick[0]},{g_xr_stick,stick[1]},{g_xr_a,pa},{g_xr_b,pb},{g_xr_x,px},{g_xr_y,py},{g_xr_menu,pm},{g_xr_stick_click,plc},{g_xr_stick_click,prc}};
 XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};sb.interactionProfile=profile;
 sb.countSuggestedBindings=uint32_t(sizeof(b)/sizeof(b[0]));sb.suggestedBindings=b;if(XR_FAILED(p_suggest_bindings(g_xr_instance,&sb)))return false;
 XrSessionActionSetsAttachInfo at{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};at.countActionSets=1;at.actionSets=&g_xr_action_set;
 if(XR_FAILED(p_attach_actions(g_xr_session,&at)))return false;
 XrActionSpaceCreateInfo ac{XR_TYPE_ACTION_SPACE_CREATE_INFO};ac.action=g_xr_hand_pose;ac.poseInActionSpace.orientation.w=1;
 for(int h=0;h<2;h++){ac.subactionPath=hands[h];if(XR_FAILED(p_create_action_space(g_xr_session,&ac,&g_xr_hand_space[h])))return false;}
 return true;
}
void xr_update_tracking(){
 std::lock_guard<std::mutex> lock(g_xr_tracking_mutex);
 XrActiveActionSet active{g_xr_action_set,XR_NULL_PATH};XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};si.countActiveActionSets=1;si.activeActionSets=&active;
 if(XR_FAILED(p_sync_actions(g_xr_session,&si)))return;
 XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
 if(XR_SUCCEEDED(p_locate_space(g_xr_view,g_xr_local,g_xr_time,&head))&&(head.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT)&&(head.locationFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))g_xr_head_pose=head.pose;
 XrPath hands[2]{};p_string_to_path(g_xr_instance,"/user/hand/left",&hands[0]);p_string_to_path(g_xr_instance,"/user/hand/right",&hands[1]);
 for(int h=0;h<2;h++){XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};gi.subactionPath=hands[h];gi.action=g_xr_hand_pose;
 XrActionStatePose ps{XR_TYPE_ACTION_STATE_POSE};if(XR_SUCCEEDED(p_get_pose(g_xr_session,&gi,&ps))&&ps.isActive){
 XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};if(XR_SUCCEEDED(p_locate_space(g_xr_hand_space[h],g_xr_local,g_xr_time,&loc))&&(loc.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT)&&(loc.locationFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)){g_xr_hand_pose_value[h]=loc.pose;g_xr_hand_active[h]=true;}else g_xr_hand_active[h]=false;}else g_xr_hand_active[h]=false;
 XrActionStateFloat f{XR_TYPE_ACTION_STATE_FLOAT};gi.action=g_xr_trigger;if(XR_SUCCEEDED(p_get_float(g_xr_session,&gi,&f)))g_xr_trigger_value[h]=f.isActive?f.currentState:0;
 gi.action=g_xr_grip;if(XR_SUCCEEDED(p_get_float(g_xr_session,&gi,&f)))g_xr_grip_value[h]=f.isActive?f.currentState:0;
 XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};gi.action=g_xr_stick;if(XR_SUCCEEDED(p_get_vector(g_xr_session,&gi,&v)))g_xr_stick_value[h]=v.isActive?v.currentState:XrVector2f{0,0};
  XrActionStateBoolean bs{XR_TYPE_ACTION_STATE_BOOLEAN};XrAction face[6]={g_xr_a,g_xr_b,g_xr_x,g_xr_y,g_xr_menu,g_xr_stick_click};
  for(int k=0;k<6;k++){gi.action=face[k];if(XR_SUCCEEDED(p_get_boolean(g_xr_session,&gi,&bs)))g_xr_buttons[h][k]=bs.isActive&&bs.currentState;}}
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


bool xr_android_poll_events() {
 if(!g_xr_setup_complete.load(std::memory_order_acquire)||g_xr_session==XR_NULL_HANDLE||!p_poll)return false;
 XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
 while(p_poll(g_xr_instance,&event)==XR_SUCCESS){
  if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
   auto* changed=reinterpret_cast<XrEventDataSessionStateChanged*>(&event);g_xr_state=changed->state;
   if(g_xr_state==XR_SESSION_STATE_READY&&!g_xr_running){
    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrResult r=p_begin(g_xr_session,&bi);if(XR_SUCCEEDED(r)){g_xr_running=true;xr_log("OpenXR session began in READY state");}else xr_log("xrBeginSession failed",r);
   }else if(g_xr_state==XR_SESSION_STATE_STOPPING&&g_xr_running){p_end(g_xr_session);g_xr_running=false;xr_log("OpenXR session ended on STOPPING");}
   else if(g_xr_state==XR_SESSION_STATE_EXITING||g_xr_state==XR_SESSION_STATE_LOSS_PENDING)g_xr_running=false;
  }
  event={XR_TYPE_EVENT_DATA_BUFFER};
 }
 return g_xr_running;
}
bool xr_android_frame_begin() {
 if(!g_xr_running.load()||g_xr_frame)return false;
 XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};XrFrameState fs{XR_TYPE_FRAME_STATE};
 XrResult r=p_wait(g_xr_session,&wi,&fs);if(XR_FAILED(r)){xr_log("xrWaitFrame failed",r);return false;}
 g_xr_time=fs.predictedDisplayTime;XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
 { std::lock_guard<std::mutex> queue_lock(nx::nvn::vk_queue_mutex()); r=p_frame_begin(g_xr_session,&bi); } if(XR_FAILED(r)){xr_log("xrBeginFrame failed",r);return false;}
 g_xr_frame=true;g_xr_render=fs.shouldRender;
 XrViewState vs{XR_TYPE_VIEW_STATE};XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
 li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;li.displayTime=g_xr_time;li.space=g_xr_local;
 uint32_t count=0;r=p_locate_views(g_xr_session,&li,&vs,2,&count,g_xr_views);
 if(XR_FAILED(r)||count!=2){g_xr_render=false;xr_log("xrLocateViews failed",r);}
 xr_update_tracking();
 return g_xr_render;
}
void xr_android_eye_extent(uint32_t eye,uint32_t* width,uint32_t* height) {
 if(eye>=2)return;
 if(width)*width=g_xr_config[eye].recommendedImageRectWidth;
 if(height)*height=g_xr_config[eye].recommendedImageRectHeight;
}
void xr_android_get_tracking(float* head_pos,float* head_rot,float* left_pos,float* left_rot,
 float* right_pos,float* right_rot,float* triggers,float* grips,float* sticks,bool* active,uint64_t* buttons) {
 std::lock_guard<std::mutex> lock(g_xr_tracking_mutex);
 auto cp=[](const XrPosef&p,float*pos,float*rot){
  if(pos){pos[0]=p.position.x;pos[1]=p.position.y;pos[2]=p.position.z;}
  if(rot){rot[0]=p.orientation.x;rot[1]=p.orientation.y;rot[2]=p.orientation.z;rot[3]=p.orientation.w;}
 };
 cp(g_xr_head_pose,head_pos,head_rot);cp(g_xr_hand_pose_value[0],left_pos,left_rot);cp(g_xr_hand_pose_value[1],right_pos,right_rot);
 for(int h=0;h<2;h++){if(triggers)triggers[h]=g_xr_trigger_value[h];if(grips)grips[h]=g_xr_grip_value[h];
  if(sticks){sticks[h*2]=g_xr_stick_value[h].x;sticks[h*2+1]=g_xr_stick_value[h].y;}
  if(active)active[h]=g_xr_hand_active[h];if(buttons){buttons[h]=0;for(int k=0;k<6;k++)if(g_xr_buttons[h][k])buttons[h]|=(1ull<<k);}}
}
bool xr_android_acquire_eye(uint32_t eye,VkImage* image) {
 if(!g_xr_frame||!g_xr_render||eye>=2||!image||g_xr_acquired[eye])return false;
 XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
 XrResult acquire_result; { std::lock_guard<std::mutex> queue_lock(nx::nvn::vk_queue_mutex()); acquire_result=p_acquire(g_xr_swapchains[eye],&ai,&g_xr_indices[eye]); } if(XR_FAILED(acquire_result))return false;
 g_xr_acquired[eye]=true;
 XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
 if(XR_FAILED(p_wait_image(g_xr_swapchains[eye],&wi)))return false;
 *image=g_xr_images[eye][g_xr_indices[eye]].image;return true;
}
void xr_android_frame_end() {
 if(!g_xr_frame)return;
 XrCompositionLayerProjectionView pv[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
 bool submit=g_xr_render;
 for(uint32_t e=0;e<2;e++){
  if(g_xr_acquired[e]){XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
   XrResult release_result; { std::lock_guard<std::mutex> queue_lock(nx::nvn::vk_queue_mutex()); release_result=p_release(g_xr_swapchains[e],&ri); } if(XR_FAILED(release_result))submit=false;g_xr_acquired[e]=false;}else submit=false;
  pv[e].pose=g_xr_views[e].pose;pv[e].fov=g_xr_views[e].fov;pv[e].subImage.swapchain=g_xr_swapchains[e];
  pv[e].subImage.imageRect.offset={0,0};pv[e].subImage.imageRect.extent={
   int32_t(g_xr_config[e].recommendedImageRectWidth),int32_t(g_xr_config[e].recommendedImageRectHeight)};
 }
 XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=g_xr_local;layer.viewCount=2;layer.views=pv;
 const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
 XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};ei.displayTime=g_xr_time;ei.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
 ei.layerCount=submit?1:0;ei.layers=submit?layers:nullptr;
 XrResult r; { std::lock_guard<std::mutex> queue_lock(nx::nvn::vk_queue_mutex()); r=p_frame_end(g_xr_session,&ei); } if(XR_FAILED(r))xr_log("xrEndFrame failed",r);
 g_xr_frame=false;g_xr_render=false;
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
