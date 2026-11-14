// Builds without Dear ImGui (or a platform without SDL3 yet): no settings screen; config.ini still applies.
#include "ui/ui.h"
#include "ui/i18n.h"

namespace nx::ui {

void ui_vulkan_ready(PFN_vkGetInstanceProcAddr, VkInstance, VkPhysicalDevice, VkDevice, uint32_t, VkQueue, bool) {}
void ui_set_rotation(int) {}
void ui_record(VkCommandBuffer, VkImage, VkFormat, VkExtent2D) {}
void ui_notice(const std::string&) {}
void ui_screenshot_saved(const std::string&) {}
void ui_swapchain_changed() {}
void ui_picture_shown() {}
void ui_shutdown() {}
void ui_event(const SDL_Event&) {}
bool ui_pad(uint64_t, int32_t, int32_t) { return false; }
uint64_t ui_filter_game_buttons(uint64_t buttons, bool) { return buttons; }
bool ui_visible() { return false; }
bool ui_text_field() { return false; }
void ui_toggle() {}
void ui_open_online() {}
bool ui_touch(float, float, bool) { return false; }

// The headless Android/Quest build has no translation UI. Return the source key as a safe fallback.
const char* tr(const char* key) { return key ? key : ""; }
std::string trf(const char* key, const std::string& arg) {
    std::string result = tr(key);
    const auto marker = result.find("{}");
    if (marker != std::string::npos) result.replace(marker, 2, arg);
    return result;
}

}  // namespace nx::ui
