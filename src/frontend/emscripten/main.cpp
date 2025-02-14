// Copyright 2022 TotalJustice.
// SPDX-License-Identifier: GPL-3.0-only
#include <gba.hpp>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <vector>
#include <string>

#include <sdl2_base.hpp>
#include <SDL.h>
// intellisense is having a bad day, i cba to fix it
#if 0
#include </home/total/emsdk/upstream/emscripten/cache/sysroot/include/emscripten/emscripten.h>
#include </home/total/emsdk/upstream/emscripten/cache/sysroot/include/emscripten/html5.h>
#include </home/total/emsdk/upstream/emscripten/cache/sysroot/include/emscripten/html5_webgl.h>
#include </home/total/emsdk/upstream/emscripten/cache/sysroot/include/emscripten/console.h>
#else
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <emscripten/console.h>
#endif

namespace {

enum class Menu
{
    ROM,
    SIDEBAR,
};

enum TouchID : int
{
    TouchID_A,
    TouchID_B,
    TouchID_L,
    TouchID_R,
    TouchID_UP,
    TouchID_DOWN,
    TouchID_LEFT,
    TouchID_RIGHT,
    TouchID_START,
    TouchID_SELECT,
    TouchID_OPTIONS,
    TouchID_TITLE,
    TouchID_OPEN,
    TouchID_SAVE,
    TouchID_LOAD,
    TouchID_BACK,
    TouchID_IMPORT,
    TouchID_EXPORT,
    TouchID_FULLSCREEN,
    TouchID_AUDIO,
    TouchID_FASTFORWARD,
    TouchID_MAX,
};

constexpr auto get_touch_id_asset(TouchID id)
{
    switch (id)
    {
        case TouchID_A: return "assets/buttons/a.png";
        case TouchID_B: return "assets/buttons/b.png";
        case TouchID_L: return "assets/buttons/l.png";
        case TouchID_R: return "assets/buttons/r.png";
        case TouchID_UP: return "assets/buttons/dpad_up.png";
        case TouchID_DOWN: return "assets/buttons/dpad_down.png";
        case TouchID_LEFT: return "assets/buttons/dpad_left.png";
        case TouchID_RIGHT: return "assets/buttons/dpad_right.png";
        case TouchID_START: return "assets/buttons/start.png";
        case TouchID_SELECT: return "assets/buttons/select.png";
        case TouchID_OPTIONS: return "assets/buttons/setting_sandwich.png";
        case TouchID_TITLE: return "assets/menu/title.png";
        case TouchID_OPEN: return "assets/menu/open.png";
        case TouchID_SAVE: return "assets/menu/save.png";
        case TouchID_LOAD: return "assets/menu/load.png";
        case TouchID_BACK: return "assets/menu/back.png";
        case TouchID_IMPORT: return "assets/menu/import.png";
        case TouchID_EXPORT: return "assets/menu/export.png";
        case TouchID_FULLSCREEN:  return "assets/buttons/larger.png";
        case TouchID_AUDIO:  return "assets/buttons/musicOn.png";
        case TouchID_FASTFORWARD:  return "assets/buttons/fastForward.png";
        case TouchID_MAX: return "NULL";
    }

    return "NULL";
}

struct TouchButton
{
    SDL_Texture* texture{};
    SDL_Rect rect{};
    int w{};
    int h{};
    bool enabled{};
    bool dragable{};
};

struct TouchCacheEntry
{
    SDL_FingerID finger_id;
    TouchID touch_id;
    bool down;
};

struct RomEventData
{
    ~RomEventData()
    {
        if (data != nullptr)
        {
            std::free(this->data);
        }
    }

    char name[256]{};
    std::uint8_t* data{};
    std::size_t len{};
};

struct ButtonEventData
{
    int type;
    int down;
};

std::uint32_t ROM_LOAD_EVENT = 0;
std::uint32_t FLUSH_SAVE_EVENT = 0;

struct App final : frontend::sdl2::Sdl2Base
{
    App(int argc, char** argv);
    ~App() override;

public:
    auto loadsave(const std::string& path = "") -> bool override;
    auto savegame(const std::string& path = "") -> bool override;

    auto loadstate(const std::string& path = "") -> bool override;
    auto savestate(const std::string& path = "") -> bool override;

    auto render() -> void override;

    auto change_menu(Menu new_menu) -> void;

    auto on_touch_button_change(TouchID touch_id, bool down) -> void;
    auto is_touch_in_range(int x, int y) -> int;

    auto on_touch_up(std::span<TouchCacheEntry> cache, SDL_FingerID id) -> void;
    auto on_touch_down(std::span<TouchCacheEntry> cache, SDL_FingerID id, int x, int y) -> void;
    auto on_touch_motion(std::span<TouchCacheEntry> cache, SDL_FingerID id, int x, int y) -> void;

    auto on_key_event(const SDL_KeyboardEvent& e) -> void override;
    auto on_user_event(SDL_UserEvent& e) -> void override;
    auto on_controlleraxis_event(const SDL_ControllerAxisEvent& e) -> void override;
    auto on_controllerbutton_event(const SDL_ControllerButtonEvent& e) -> void override;
    auto on_mousebutton_event(const SDL_MouseButtonEvent& e) -> void override;
    auto on_mousemotion_event(const SDL_MouseMotionEvent& e) -> void override;
    auto on_touch_event(const SDL_TouchFingerEvent& e) -> void override;

    auto is_fullscreen() const -> bool override;
    auto toggle_fullscreen() -> void override;

    auto resize_emu_screen() -> void override;
    auto rom_file_picker() -> void override;

    auto on_speed_change() -> void;
    auto on_audio_change() -> void;

    auto on_flush_save() -> void;

public:
    TouchButton touch_buttons[TouchID_MAX]{};
    TouchCacheEntry touch_entries[10]{}; // 10 fingers max
    TouchCacheEntry mouse_entries[1]{}; // 1 mouse max
    bool touch_hidden{false};

    Menu menu{Menu::ROM};
    SDL_TimerID sram_sync_timer;
};

constexpr auto get_scale(float minw, float minh, float w, float h)
{
    const auto scale_w = w / minw;
    const auto scale_h = h / minh;
    return std::min(scale_w, scale_h);
}

auto em_key_callback(int eventType, const EmscriptenKeyboardEvent *keyEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_KEYPRESS:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_KEYPRESS]\n");
            break;

        case EMSCRIPTEN_EVENT_KEYDOWN:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_KEYDOWN]\n");
            break;

        case EMSCRIPTEN_EVENT_KEYUP:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_KEYUP]\n");
            break;
    }

    return false;
}

auto em_mouse_callback(int eventType, const EmscriptenMouseEvent *mouseEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_CLICK:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_CLICK]\n");
            break;

        case EMSCRIPTEN_EVENT_MOUSEDOWN:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_MOUSEDOWN]\n");
            break;

        case EMSCRIPTEN_EVENT_MOUSEUP:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_MOUSEUP]\n");
            break;

        case EMSCRIPTEN_EVENT_DBLCLICK:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_DBLCLICK]\n");
            break;

        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_MOUSEMOVE]\n");
            break;

        case EMSCRIPTEN_EVENT_MOUSEENTER:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_MOUSEENTER]\n");
            break;

        case EMSCRIPTEN_EVENT_MOUSELEAVE:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_MOUSELEAVE]\n");
            break;
    }

    return false;
}

auto em_wheel_callback(int eventType, const EmscriptenWheelEvent *wheelEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_WHEEL:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_WHEEL]\n");
            break;
    }

    return false;
}

auto em_ui_callback(int eventType, const EmscriptenUiEvent *uiEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_RESIZE:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_RESIZE]\n");
            break;

        case EMSCRIPTEN_EVENT_SCROLL:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_SCROLL]\n");
            break;
    }

    return false;
}

auto em_focus_callback(int eventType, const EmscriptenFocusEvent *focusEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_BLUR:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_BLUR] node: %s id: %s\n", focusEvent->nodeName, focusEvent->id);
            break;

        case EMSCRIPTEN_EVENT_FOCUS:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_FOCUS] node: %s id: %s\n", focusEvent->nodeName, focusEvent->id);
            break;

        case EMSCRIPTEN_EVENT_FOCUSIN:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_FOCUSIN] node: %s id: %s\n", focusEvent->nodeName, focusEvent->id);
            break;

        case EMSCRIPTEN_EVENT_FOCUSOUT:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_FOCUSOUT] node: %s id: %s\n", focusEvent->nodeName, focusEvent->id);
            break;
    }

    return false;
}

auto em_deviceorientation_callback(int eventType, const EmscriptenDeviceOrientationEvent *deviceOrientationEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_DEVICEORIENTATION:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_DEVICEORIENTATION]\n");
            break;
    }

    return false;
}

auto em_devicemotion_callback(int eventType, const EmscriptenDeviceMotionEvent *deviceMotionEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_DEVICEMOTION:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_DEVICEMOTION]\n");
            break;
    }

    return false;
}

auto em_orientationchange_callback(int eventType, const EmscriptenOrientationChangeEvent *orientationChangeEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_ORIENTATIONCHANGE:
            switch (orientationChangeEvent->orientationIndex)
            {
                case EMSCRIPTEN_ORIENTATION_PORTRAIT_PRIMARY:
                    // emscripten_console_logf("[EMSCRIPTEN_ORIENTATION_PORTRAIT_PRIMARY] angle: %d\n", orientationChangeEvent->orientationAngle);
                    break;

                case EMSCRIPTEN_ORIENTATION_PORTRAIT_SECONDARY:
                    // emscripten_console_logf("[EMSCRIPTEN_ORIENTATION_PORTRAIT_SECONDARY] angle: %d\n", orientationChangeEvent->orientationAngle);
                    break;

                case EMSCRIPTEN_ORIENTATION_LANDSCAPE_PRIMARY:
                    // emscripten_console_logf("[EMSCRIPTEN_ORIENTATION_LANDSCAPE_PRIMARY] angle: %d\n", orientationChangeEvent->orientationAngle);
                    break;

                case EMSCRIPTEN_ORIENTATION_LANDSCAPE_SECONDARY:
                    // emscripten_console_logf("[EMSCRIPTEN_ORIENTATION_LANDSCAPE_SECONDARY] angle: %d\n", orientationChangeEvent->orientationAngle);
                    break;

                default:
                    // emscripten_console_logf("[EMSCRIPTEN_ORIENTATION_UNKNOWN] angle: %d index: %d\n", orientationChangeEvent->orientationAngle, orientationChangeEvent->orientationIndex);
                    break;
            }
            break;
    }

    return false;
}

auto em_fullscreenchange_callback(int eventType, const EmscriptenFullscreenChangeEvent *fullscreenChangeEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_FULLSCREENCHANGE:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_FULLSCREENCHANGE] node: %s id: %s fullscreen: %u screen: %dx%d element: %dx%d\n", fullscreenChangeEvent->nodeName, fullscreenChangeEvent->id, fullscreenChangeEvent->isFullscreen, fullscreenChangeEvent->elementWidth, fullscreenChangeEvent->screenHeight, fullscreenChangeEvent->elementWidth, fullscreenChangeEvent->elementHeight);
            break;
    }

    return false;
}

auto em_pointerlockchange_callback(int eventType, const EmscriptenPointerlockChangeEvent *pointerlockChangeEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_POINTERLOCKCHANGE:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_POINTERLOCKCHANGE] node: %s id: %s\n", pointerlockChangeEvent->nodeName, pointerlockChangeEvent->id);
            break;
    }

    return false;
}

auto em_pointerlockerror_callback(int eventType, const void *reserved, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_POINTERLOCKERROR:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_POINTERLOCKERROR]\n");
            break;
    }

    return false;
}

auto em_visibilitychange_callback(int eventType, const EmscriptenVisibilityChangeEvent *visibilityChangeEvent, void *userData) -> EM_BOOL
{
    auto app = static_cast<App*>(userData);

    switch (visibilityChangeEvent->visibilityState)
    {
        case EMSCRIPTEN_VISIBILITY_HIDDEN:
            // this is needed as firefox stops em loop from running very quickly, so
            // the pause won't be polled nor will sdl events fire in time!
            app->has_focus = false;
            app->update_audio_device_pause_status();
            app->on_flush_save();
            emscripten_console_logf("[EMSCRIPTEN_VISIBILITY_HIDDEN] hidden: %u\n", visibilityChangeEvent->hidden);
            break;

        case EMSCRIPTEN_VISIBILITY_VISIBLE:
            app->has_focus = true;
            emscripten_console_logf("[EMSCRIPTEN_VISIBILITY_VISIBLE] hidden: %u\n", visibilityChangeEvent->hidden);
            break;

        case EMSCRIPTEN_VISIBILITY_PRERENDER:
            emscripten_console_logf("[EMSCRIPTEN_VISIBILITY_PRERENDER] hidden: %u\n", visibilityChangeEvent->hidden);
            break;

        case EMSCRIPTEN_VISIBILITY_UNLOADED:
            emscripten_console_logf("[EMSCRIPTEN_VISIBILITY_UNLOADED] hidden: %u\n", visibilityChangeEvent->hidden);
            break;
    }

    return false;
}

auto em_touch_callback(int eventType, const EmscriptenTouchEvent *touchEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_TOUCHSTART:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_TOUCHSTART]\n");
            break;

        case EMSCRIPTEN_EVENT_TOUCHEND:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_TOUCHEND]\n");
            break;

        case EMSCRIPTEN_EVENT_TOUCHMOVE:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_TOUCHMOVE]\n");
            break;

        case EMSCRIPTEN_EVENT_TOUCHCANCEL:
            // emscripten_console_logf("[EMSCRIPTEN_EVENT_TOUCHCANCEL]\n");
            break;
    }

    return false;
}

auto em_gamepad_callback(int eventType, const EmscriptenGamepadEvent *gamepadEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_GAMEPADCONNECTED:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_GAMEPADCONNECTED] id: %s\n", gamepadEvent->id);
            break;

        case EMSCRIPTEN_EVENT_GAMEPADDISCONNECTED:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_GAMEPADDISCONNECTED] id: %s\n", gamepadEvent->id);
            break;
    }

    return false;
}

auto em_battery_callback(int eventType, const EmscriptenBatteryEvent *batteryEvent, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_BATTERYCHARGINGCHANGE:
            if (batteryEvent->charging)
            {
                emscripten_console_logf("[EMSCRIPTEN_EVENT_BATTERYCHARGINGCHANGE] time to charge: %.2f\n", batteryEvent->chargingTime);
            }
            else
            {
                emscripten_console_logf("[EMSCRIPTEN_EVENT_BATTERYCHARGINGCHANGE] discharging, time left: %.2f\n", batteryEvent->dischargingTime);
            }
            break;

        case EMSCRIPTEN_EVENT_BATTERYLEVELCHANGE:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_BATTERYLEVELCHANGE] %.2f\n", batteryEvent->level);
            break;
    }

    return false;
}

auto em_beforeunload_callback(int eventType, const void *reserved, void *userData) -> const char*
{
    auto app = static_cast<App*>(userData);
    app->on_flush_save();
    // return "";
    return "do you really want to exit?";
}

auto em_webgl_context_callback(int eventType, const void *reserved, void *userData) -> EM_BOOL
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_WEBGLCONTEXTLOST:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_WEBGLCONTEXTLOST]\n");
            break;

        case EMSCRIPTEN_EVENT_WEBGLCONTEXTRESTORED:
            emscripten_console_logf("[EMSCRIPTEN_EVENT_WEBGLCONTEXTRESTORED]\n");
            break;
    }

    return false;
}

auto em_set_button_rect(const char* id, int x, int y, int w, int h)
{
    EM_ASM({
        let id = UTF8ToString(arguments[0]);
        let button = document.getElementById(id);

        button.style.left = arguments[1] + 'px';
        button.style.top = arguments[2] + 'px';
        button.style.width = arguments[3] + 'px';
        button.style.height= arguments[4] + 'px';
    }, id, x, y, w, h);
}

auto em_set_button_visibility(const char* id, bool visible)
{
    EM_ASM({
        let id = UTF8ToString(arguments[0]);
        let button = document.getElementById(id);

        if (arguments[1]) {
            button.style.visibility = 'visible';
        } else {
            button.style.visibility = 'hidden';
        }
    }, id, visible);
}

auto em_set_buttons_visibility(bool visible)
{
    em_set_button_visibility("RomFilePicker", visible);
    em_set_button_visibility("DlSaves", visible);
}

auto em_idbfs_mkdir(const char* path, bool mount = true) -> void
{
    EM_ASM({
        let path = UTF8ToString(arguments[0]);
        let mount = arguments[1];

        if (!FS.analyzePath(path).exists) {
            FS.mkdir(path);
        }

        if (mount) {
            FS.mount(IDBFS, {}, path);
        }
    }, path, mount);
}

auto em_idbfs_syncfs(bool populate = false) -> void
{
    EM_ASM({
        let populate = arguments[0];

        FS.syncfs(populate, function (err) {
            if (err) {
                console.log(err);
            }
        });
    }, populate);
}

auto em_loop(void* user) -> void
{
    auto app = static_cast<App*>(user);
    app->step();
}

#ifdef EM_THREADS
auto sdl2_sram_timer_callback(Uint32 interval, void* user) -> Uint32
{
    auto app = static_cast<App*>(user);
    app->on_flush_save();
    return interval;
}
#endif

auto sdl2_audio_callback(void* user, Uint8* data, int len) -> void
{
    auto app = static_cast<App*>(user);
    // this may cause a race condition if not built with threads
    // as the audio may still be ran on its own thread...
    app->fill_audio_data_from_stream(data, len);
}

auto on_vblank_callback(void* user) -> void
{
    auto app = static_cast<App*>(user);
    app->update_pixels_from_gba();
}

auto on_audio_callback(void* user) -> void
{
    auto app = static_cast<App*>(user);
    app->fill_stream_from_sample_data();
}

App::App(int argc, char** argv) : frontend::sdl2::Sdl2Base(argc, argv)
{
    if (!running)
    {
        return;
    }

    running = false;

    gameboy_advance.set_userdata(this);
    gameboy_advance.set_vblank_callback(on_vblank_callback);

    if (init_audio(this, sdl2_audio_callback, on_audio_callback))
    {
        gameboy_advance.set_audio_callback(on_audio_callback, sample_data);
    }

    ROM_LOAD_EVENT = SDL_RegisterEvents(1);

    // setup idbfs
    em_idbfs_mkdir("/save");
    em_idbfs_mkdir("/state");
    em_idbfs_syncfs(true);

    for (auto i =  0; i < TouchID_MAX; i++)
    {
        int w;
        int h;
        if (auto pixel_data = emscripten_get_preloaded_image_data(get_touch_id_asset(static_cast<TouchID>(i)), &w, &h))
        {
            if (auto tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h))
            {
                if (!SDL_UpdateTexture(tex, nullptr, pixel_data, 4 * w))
                {
                    if (i <= TouchID_OPTIONS)
                    {
                        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                        SDL_SetTextureAlphaMod(tex, 150);
                        touch_buttons[i].enabled = true;
                    }

                    // dpad and a,b,l,r are dragable
                    if (i <= TouchID_RIGHT)
                    {
                        touch_buttons[i].dragable = true;
                    }

                    touch_buttons[i].texture = tex;
                    touch_buttons[i].w = w;
                    touch_buttons[i].h = h;
                }
                else
                {
                    emscripten_console_logf("failed to update pixel data sadly: %s\n", SDL_GetError());
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to update pixle data", SDL_GetError(), nullptr);
                    SDL_DestroyTexture(tex);
                }
            }

            std::free(pixel_data);
        }
        else
        {
            emscripten_console_logf("failed to load pixel data via emscripten: %s\n", get_touch_id_asset(static_cast<TouchID>(i)));
        }
    }

    // SDL_timers fire instantly without pthread support!
    #ifdef EM_THREADS
        sram_sync_timer = SDL_AddTimer(1000 * 3, sdl2_sram_timer_callback, this);
    #else
        FLUSH_SAVE_EVENT = SDL_RegisterEvents(1);
        EM_ASM( setInterval(_em_flush_save, 1000 * 3); );
    #endif

    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_key_callback)) { emscripten_console_logf("[EM] failed to set keypress callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_key_callback)) { emscripten_console_logf("[EM] failed to set keydown callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_key_callback)) { emscripten_console_logf("[EM] failed to set keyup callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_click_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_mouse_callback)) { emscripten_console_logf("[EM] failed to set click callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_mousedown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_mouse_callback)) { emscripten_console_logf("[EM] failed to set mousedown callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_mouse_callback)) { emscripten_console_logf("[EM] failed to set mouseup callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_dblclick_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_mouse_callback)) { emscripten_console_logf("[EM] failed to set dblclick callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_mouse_callback)) { emscripten_console_logf("[EM] failed to set mousemove callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_mouseenter_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_mouse_callback)) { emscripten_console_logf("[EM] failed to set mouseenter callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_mouseleave_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_mouse_callback)) { emscripten_console_logf("[EM] failed to set mouseleave callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_mouseover_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_mouse_callback)) { emscripten_console_logf("[EM] failed to set mouseover callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_mouseout_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_mouse_callback)) { emscripten_console_logf("[EM] failed to set mouseout callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_wheel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_wheel_callback)) { emscripten_console_logf("[EM] failed to set wheel callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_ui_callback)) { emscripten_console_logf("[EM] failed to set resize callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_scroll_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_ui_callback)) { emscripten_console_logf("[EM] failed to set scroll callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_focus_callback)) { emscripten_console_logf("[EM] failed to set blur callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_focus_callback)) { emscripten_console_logf("[EM] failed to set focus callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_focusin_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_focus_callback)) { emscripten_console_logf("[EM] failed to set focusin callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_focusout_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_focus_callback)) { emscripten_console_logf("[EM] failed to set focusout callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_deviceorientation_callback(this, true, em_deviceorientation_callback)) { emscripten_console_logf("[EM] failed to set deviceorientation callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_devicemotion_callback(this, true, em_devicemotion_callback)) { emscripten_console_logf("[EM] failed to set devicemotion callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_orientationchange_callback(this, true, em_orientationchange_callback)) { emscripten_console_logf("[EM] failed to set orientationchange callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_fullscreenchange_callback)) { emscripten_console_logf("[EM] failed to set fullscreenchange callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_pointerlockchange_callback)) { emscripten_console_logf("[EM] failed to set pointerlockchange callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_pointerlockerror_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_pointerlockerror_callback)) { emscripten_console_logf("[EM] failed to set pointerlockerror callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_visibilitychange_callback(this, true, em_visibilitychange_callback)) { emscripten_console_logf("[EM] failed to set visibilitychange callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_touchstart_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_touch_callback)) { emscripten_console_logf("[EM] failed to set touchstart callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_touchend_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_touch_callback)) { emscripten_console_logf("[EM] failed to set touchend callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_touchmove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_touch_callback)) { emscripten_console_logf("[EM] failed to set touchmove callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_touchcancel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_touch_callback)) { emscripten_console_logf("[EM] failed to set touchcancel callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_gamepadconnected_callback(this, true, em_gamepad_callback)) { emscripten_console_logf("[EM] failed to set gamepadconnected callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_gamepaddisconnected_callback(this, true, em_gamepad_callback)) { emscripten_console_logf("[EM] failed to set gamepaddisconnected callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_batterychargingchange_callback(this, em_battery_callback)) { emscripten_console_logf("[EM] failed to set batterychargingchange callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_batterylevelchange_callback(this, em_battery_callback)) { emscripten_console_logf("[EM] failed to set batterylevelchange callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_beforeunload_callback(this, em_beforeunload_callback)) { emscripten_console_logf("[EM] failed to set beforeunload callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_webglcontextlost_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_webgl_context_callback)) { emscripten_console_logf("[EM] failed to set webglcontextlost callback\n"); }
    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_set_webglcontextrestored_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, em_webgl_context_callback)) { emscripten_console_logf("[EM] failed to set webglcontextrestored callback\n"); }

    if (EMSCRIPTEN_RESULT_SUCCESS != emscripten_lock_orientation(EMSCRIPTEN_ORIENTATION_LANDSCAPE_PRIMARY | EMSCRIPTEN_ORIENTATION_LANDSCAPE_SECONDARY))
    {
        emscripten_console_logf("[EM] failed to lock orientation\n");
    }

    change_menu(Menu::SIDEBAR);
    running = true;
}

App::~App()
{
    if (sram_sync_timer)
    {
        SDL_RemoveTimer(sram_sync_timer);
    }

    for (auto& entry : touch_buttons)
    {
        SDL_DestroyTexture(entry.texture);
    }
}

auto App::loadsave(const std::string& path) -> bool
{
    std::string new_path = "/save/" + create_save_path(rom_path);
    return frontend::Base::loadsave(new_path);
}

auto App::savegame(const std::string& path) -> bool
{
    std::string new_path = "/save/" + create_save_path(rom_path);
    if (frontend::Base::savegame(new_path))
    {
        em_idbfs_syncfs();
        return true;
    }
    // SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "save", "failed to save", nullptr);
    return false;
}

auto App::loadstate(const std::string& path) -> bool
{
    std::string new_path = "/state/" + create_state_path(rom_path);
    if (frontend::Base::loadstate(new_path))
    {
        return true;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "loadstate", "failed to loadstate", nullptr);
    return false;
}

auto App::savestate(const std::string& path) -> bool
{
    std::string new_path = "/state/" + create_state_path(rom_path);
    if (frontend::Base::savestate(new_path))
    {
        em_idbfs_syncfs();
        return true;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "savestate", "failed to savestate", nullptr);
    return false;
}

auto App::render() -> void
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    update_texture_from_pixels();

    // render gba
    SDL_RenderCopy(renderer, texture, nullptr, &emu_rect);

    if (menu == Menu::SIDEBAR)
    {
        const auto [w, h] = get_renderer_size();
        const auto side_scale = get_scale(115*2, 35*6, w, h);

        SDL_Rect r1 = {};
        r1.x = 0; r1.y = 2 * side_scale; r1.w = 115 * side_scale; r1.h = h - 13 * side_scale;
        SDL_Rect r2 = {};
        r2.x = w - (115 * side_scale); r2.y = 2 * side_scale; r2.w = 115 * side_scale; r2.h = h - 13 * side_scale;

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(renderer, &r1);
        SDL_RenderFillRect(renderer, &r2);
    }

    // render buttons
    if (!touch_hidden)
    {
        for (auto& entry : touch_buttons)
        {
            if (entry.texture && entry.enabled)
            {
                SDL_RenderCopy(renderer, entry.texture, nullptr, &entry.rect);
            }
        }
    }

    SDL_RenderPresent(renderer);
}

auto App::change_menu(Menu new_menu) -> void
{
    if (menu == new_menu)
    {
        return;
    }

    menu = new_menu;
    emu_run = false;

    switch (menu)
    {
        case Menu::ROM:
            emu_run = true;
            em_set_buttons_visibility(false);
            break;

        case Menu::SIDEBAR:
            // unset all buttons
            set_button(gba::Button::ALL, false);
            em_set_buttons_visibility(true);
            break;
    }

    for (std::size_t i = 0; i < SDL_arraysize(touch_buttons); i++)
    {
        auto& e = touch_buttons[i];

        if (i <= TouchID_OPTIONS)
        {
            e.enabled = menu == Menu::ROM;
        }
        else
        {
            e.enabled = menu == Menu::SIDEBAR;
        }
    }
}

auto App::on_touch_button_change(TouchID touch_id, bool down) -> void
{
    // emscripten_console_logf("[TOUCH-CHANGE] id: %d\n", touch_id);
    if (down)
    {
        emscripten_vibrate(50);
    }

    switch (touch_id)
    {
        case TouchID_A:        set_button(gba::Button::A, down);      break;
        case TouchID_B:        set_button(gba::Button::B, down);      break;
        case TouchID_L:        set_button(gba::Button::L, down);      break;
        case TouchID_R:        set_button(gba::Button::R, down);      break;
        case TouchID_UP:       set_button(gba::Button::UP, down);     break;
        case TouchID_DOWN:     set_button(gba::Button::DOWN, down);   break;
        case TouchID_LEFT:     set_button(gba::Button::LEFT, down);   break;
        case TouchID_RIGHT:    set_button(gba::Button::RIGHT, down);  break;
        case TouchID_START:    set_button(gba::Button::START, down);  break;
        case TouchID_SELECT:   set_button(gba::Button::SELECT, down); break;

        case TouchID_OPTIONS:
            if (down)
            {
                change_menu(Menu::SIDEBAR);
            }
            break;

        case TouchID_TITLE:
            break;

        case TouchID_OPEN:
            break;

        case TouchID_SAVE:
            if (down)
            {
                savestate();
                change_menu(Menu::ROM);
            }
            break;

        case TouchID_LOAD:
            if (down)
            {
                loadstate();
                change_menu(Menu::ROM);
            }
            break;

        case TouchID_BACK:
            if (down)
            {
                change_menu(Menu::ROM);
            }
            break;

        case TouchID_IMPORT:
            if (down)
            {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Unimplemented", "feature not yet implemented!", nullptr);
            }
            break;

        case TouchID_EXPORT:
            break;

        case TouchID_FULLSCREEN:
            if (down)
            {
                toggle_fullscreen();
                change_menu(Menu::ROM);
            }
            break;

        case TouchID_AUDIO:
            if (down)
            {
                emu_audio_disabled ^= 1;
                on_audio_change();
                change_menu(Menu::ROM);
            }
            break;

        case TouchID_FASTFORWARD:
            if (down)
            {
                emu_fast_forward ^= 1;
                on_speed_change();
                change_menu(Menu::ROM);
            }
            break;

        case TouchID_MAX:
            break;
    }
}

auto App::is_touch_in_range(int x, int y) -> int
{
    for (std::size_t i = 0; i < SDL_arraysize(touch_buttons); i++)
    {
        const auto& e = touch_buttons[i];

        if (e.enabled)
        {
            if (x >= e.rect.x && x <= (e.rect.x + e.rect.w))
            {
                if (y >= e.rect.y && y <= (e.rect.y + e.rect.h))
                {
                    // emscripten_console_logf("in range! x: %d y: %d\n", x, y);
                    return i;
                }
            }
        }
    }

    // emscripten_console_logf("not in range!\n");
    return -1;
}

auto App::on_touch_up(std::span<TouchCacheEntry> cache, SDL_FingerID id) -> void
{
    // emscripten_console_logf("[TOUCH-UP] id: %zd\n", id);

    for (auto& i : cache)
    {
        if (i.down && i.finger_id == id)
        {
            i.down = false;
            on_touch_button_change(i.touch_id, false);
            break;
        }
    }
}

auto App::on_touch_down(std::span<TouchCacheEntry> cache, SDL_FingerID id, int x, int y) -> void
{
    // emscripten_console_logf("[TOUCH-DOWN] x: %d y: %d id: %zd\n", x, y, id);

    if (touch_hidden)
    {
        touch_hidden = false;
        return;
    }

    const auto touch_id = is_touch_in_range(x, y);

    if (touch_id == -1)
    {
        return;
    }

    // find the first free entry and add it to it
    for (auto& i : cache)
    {
        if (!i.down)
        {
            i.finger_id = id;
            i.touch_id = static_cast<TouchID>(touch_id);
            i.down = true;

            on_touch_button_change(i.touch_id, true);
            break;
        }
    }
}

auto App::on_touch_motion(std::span<TouchCacheEntry> cache, SDL_FingerID id, int x, int y) -> void
{
    if (touch_hidden)
    {
        touch_hidden = false;
        return;
    }

    // emscripten_console_logf("[TOUCH-MOTION] x: %d y: %d id: %zd\n", x, y, id);

    // check that the button press maps to a texture coord
    const int touch_id = is_touch_in_range(x, y);

    if (touch_id == -1)
    {
        return;
    }

    for (auto& i : cache)
    {
        if (i.touch_id == touch_id)
        {
            return;
        }
    }

    // this is pretty inefficient, but its simple enough and works.
    on_touch_up(cache, id);

    // check if the button is dragable!
    if (touch_buttons[touch_id].dragable)
    {
        on_touch_down(cache, id, x, y);
    }
}

auto App::on_key_event(const SDL_KeyboardEvent& e) -> void
{
    touch_hidden = true;
    Sdl2Base::on_key_event(e);
}

auto App::on_user_event(SDL_UserEvent& e) -> void
{
    if (e.type == ROM_LOAD_EVENT)
    {
        auto data = static_cast<RomEventData*>(e.data1);

        std::scoped_lock lock{core_mutex};
        if (loadrom_mem(data->name, {data->data, data->len}))
        {
            change_menu(Menu::ROM);

            char buf[100];
            gba::Header header{gameboy_advance.rom};
            emscripten_console_logf(buf, "%s - [%.*s]", "Notorious BEEG", 12, header.game_title);
            SDL_SetWindowTitle(window, buf);

            emscripten_console_logf("[EM] loaded rom! name: %s len: %zu\n", data->name, data->len);
        }

        delete data;
    }
    else if (e.type == FLUSH_SAVE_EVENT)
    {
        on_flush_save();
    }
}

auto App::on_controlleraxis_event(const SDL_ControllerAxisEvent& e) -> void
{
    touch_hidden = true;
    Sdl2Base::on_controlleraxis_event(e);
}

auto App::on_controllerbutton_event(const SDL_ControllerButtonEvent& e) -> void
{
    touch_hidden = true;
    Sdl2Base::on_controllerbutton_event(e);
}

auto App::on_mousebutton_event(const SDL_MouseButtonEvent& e) -> void
{
    // we already handle touch events...
    if (e.which == SDL_TOUCH_MOUSEID)
    {
        return;
    }

    const auto [x, y] = get_window_to_render_scale(e.x, e.y);

    switch (e.type)
    {
        case SDL_MOUSEBUTTONUP:
            on_touch_up(mouse_entries, e.which);
            break;

        case SDL_MOUSEBUTTONDOWN:
            on_touch_down(mouse_entries, e.which, x, y);
            break;
    }
}

auto App::on_mousemotion_event(const SDL_MouseMotionEvent& e) -> void
{
    // we already handle touch events!
    if (e.which == SDL_TOUCH_MOUSEID)
    {
        return;
    }

    const auto [x, y] = get_window_to_render_scale(e.x, e.y);

    // only handle left clicks!
    if (e.state & SDL_BUTTON(SDL_BUTTON_LEFT))
    {
        on_touch_motion(mouse_entries, e.which, x, y);
    }
}

auto App::on_touch_event(const SDL_TouchFingerEvent& e) -> void
{
    // we need to un-normalise x, y
    const auto [ren_w, ren_h] = get_renderer_size();
    const int x = e.x * ren_w;
    const int y = e.y * ren_h;

    switch (e.type)
    {
        case SDL_FINGERUP:
            on_touch_up(touch_entries, e.fingerId);
            break;

        case SDL_FINGERDOWN:
            on_touch_down(touch_entries, e.fingerId, x, y);
            break;

        case SDL_FINGERMOTION:
            on_touch_motion(touch_entries, e.fingerId, x, y);
            break;
    }
}

auto App::is_fullscreen() const -> bool
{
    return EM_ASM_INT(
        let result =
            document.fullscreenElement ||
            document.mozFullScreenElement ||
            document.documentElement.webkitFullscreenElement ||
            document.documentElement.webkitCurrentFullScreenElement ||
            document.webkitFullscreenElement ||
            document.webkitCurrentFullScreenElement ||
            document.msFullscreenElement;

        if (document.fullscreenElement) {
            console.log("got fullscreenElement");
        }
        if (document.mozFullScreenElement) {
            console.log("got mozFullScreenElement");
        }
        if (document.documentElement.webkitFullscreenElement) {
            console.log("got documentElement.webkitFullscreenElement");
        }
        if (document.documentElement.webkitCurrentFullScreenElement) {
            console.log("got documentElement.webkitCurrentFullScreenElement");
        }
        if (document.webkitFullscreenElement) {
            console.log("got webkitFullscreenElement");
        }
        if (document.webkitCurrentFullScreenElement) {
            console.log("got webkitCurrentFullScreenElement");
        }
        if (document.msFullscreenElement) {
            console.log("got msFullscreenElement");
        }

        console.log("is_fullscreen result:", result != null);
        return result != null;
    );
}

auto App::toggle_fullscreen() -> void
{
    if (is_fullscreen())
    {
        EM_ASM(
            if (document.exitFullscreen) {
                document.exitFullscreen();
                console.log("got exitFullscreen");
            } else if (document.mozExitFullScreen) {
                document.mozExitFullScreen();
                console.log("got mozExitFullScreen");
            } else if (document.webkitExitFullScreen) {
                document.webkitCancelFullScreen();
                console.log("got webkitCancelFullScreen");
            } else if (document.webkitCancelFullScreen) {
                document.webkitCancelFullScreen();
                console.log("got webkitCancelFullScreen");
            } else if (document.msExitFullScreen) {
                document.msExitFullScreen();
                console.log("got msExitFullScreen");
            }
        );
    }
    else
    {
        EM_ASM(
            if (document.documentElement.requestFullscreen) {
                document.documentElement.requestFullscreen({ navigationUI: "hide" });
                console.log("got requestFullscreen");
            } else if (document.documentElement.mozRequestFullScreen) {
                document.documentElement.mozRequestFullScreen();
                console.log("got mozRequestFullScreen");
            } else if (document.documentElement.webkitRequestFullScreen) {
                document.documentElement.webkitRequestFullScreen();
                console.log("got webkitRequestFullScreen");
            } else if (document.documentElement.msRequestFullScreen) {
                document.documentElement.msRequestFullScreen();
                console.log("got msRequestFullScreen");
            }
        );
    }
}

auto App::resize_emu_screen() -> void
{
    Sdl2Base::resize_emu_screen();

    const auto [w, h] = get_renderer_size();
    const float scale2 = scale / 2.0;
    const auto side_scale = get_scale(115*2, 35*6, w, h);

    for (auto& entry : touch_buttons)
    {
        entry.rect.w = entry.w * scale2;
        entry.rect.h = entry.h * scale2;
    }

    for (std::size_t i = 0; i < TouchID_MAX; i++)
    {
        auto& entry = touch_buttons[i];

        if (i <= TouchID_OPTIONS)
        {
            entry.rect.w = entry.w * scale2;
            entry.rect.h = entry.h * scale2;
        }
        else
        {
            entry.rect.w = entry.w * side_scale;
            entry.rect.h = entry.h * side_scale;
        }
    }

    touch_buttons[TouchID_A].rect.x = (w - touch_buttons[TouchID_A].rect.w) - (20 * scale2);
    touch_buttons[TouchID_A].rect.y = (h - touch_buttons[TouchID_A].rect.h) - (40 * scale2);

    touch_buttons[TouchID_B].rect.x = (touch_buttons[TouchID_A].rect.x - touch_buttons[TouchID_B].rect.w) - (20 * scale2);
    touch_buttons[TouchID_B].rect.y = touch_buttons[TouchID_A].rect.y;

    touch_buttons[TouchID_UP].rect.x = 79 * scale2;
    touch_buttons[TouchID_UP].rect.y = h - 200 * scale2;

    touch_buttons[TouchID_DOWN].rect.x = touch_buttons[TouchID_UP].rect.x;
    touch_buttons[TouchID_DOWN].rect.y = (touch_buttons[TouchID_UP].rect.y + touch_buttons[TouchID_UP].rect.h) + (6 * scale2);

    touch_buttons[TouchID_LEFT].rect.x = 25 * scale2;
    touch_buttons[TouchID_LEFT].rect.y = h - 154 * scale2;

    touch_buttons[TouchID_RIGHT].rect.x = (touch_buttons[TouchID_LEFT].rect.x + touch_buttons[TouchID_LEFT].rect.w) + (16 * scale2);
    touch_buttons[TouchID_RIGHT].rect.y = touch_buttons[TouchID_LEFT].rect.y;

    touch_buttons[TouchID_L].rect.x = 25 * scale2;
    touch_buttons[TouchID_L].rect.y = 25 * scale2;

    touch_buttons[TouchID_R].rect.x = w - touch_buttons[TouchID_R].rect.w - 25 * scale2;
    touch_buttons[TouchID_R].rect.y = 25 * scale2;

    touch_buttons[TouchID_TITLE].rect.x = 5 * side_scale;
    touch_buttons[TouchID_TITLE].rect.y = 10 * side_scale;

    touch_buttons[TouchID_OPEN].rect.x = touch_buttons[TouchID_TITLE].rect.x;
    touch_buttons[TouchID_OPEN].rect.y = (touch_buttons[TouchID_TITLE].rect.y + touch_buttons[TouchID_TITLE].rect.h) + (7 * side_scale);

    touch_buttons[TouchID_SAVE].rect.x = touch_buttons[TouchID_TITLE].rect.x;
    touch_buttons[TouchID_SAVE].rect.y = (touch_buttons[TouchID_OPEN].rect.y + touch_buttons[TouchID_OPEN].rect.h) + (5 * side_scale);

    touch_buttons[TouchID_LOAD].rect.x = touch_buttons[TouchID_TITLE].rect.x;
    touch_buttons[TouchID_LOAD].rect.y = (touch_buttons[TouchID_SAVE].rect.y + touch_buttons[TouchID_SAVE].rect.h) + (5 * side_scale);

    touch_buttons[TouchID_BACK].rect.x = touch_buttons[TouchID_TITLE].rect.x;
    touch_buttons[TouchID_BACK].rect.y = (touch_buttons[TouchID_LOAD].rect.y + touch_buttons[TouchID_LOAD].rect.h) + (5 * side_scale);

    touch_buttons[TouchID_IMPORT].rect.x = (w - touch_buttons[TouchID_IMPORT].rect.w) - (5 * side_scale);
    touch_buttons[TouchID_IMPORT].rect.y = ((touch_buttons[TouchID_TITLE].rect.y + touch_buttons[TouchID_TITLE].rect.h) * 2) + (5 * side_scale);

    touch_buttons[TouchID_EXPORT].rect.x = touch_buttons[TouchID_IMPORT].rect.x;
    touch_buttons[TouchID_EXPORT].rect.y = (touch_buttons[TouchID_IMPORT].rect.y + touch_buttons[TouchID_IMPORT].rect.h) + (5 * side_scale);

    touch_buttons[TouchID_FULLSCREEN].rect.w /= 2;
    touch_buttons[TouchID_FULLSCREEN].rect.h /= 2;
    touch_buttons[TouchID_FULLSCREEN].rect.x = (w) - (65 * side_scale);
    touch_buttons[TouchID_FULLSCREEN].rect.y = touch_buttons[TouchID_BACK].rect.y + (10 * side_scale);

    touch_buttons[TouchID_AUDIO].rect.w /= 2;
    touch_buttons[TouchID_AUDIO].rect.h /= 2;
    touch_buttons[TouchID_AUDIO].rect.x = (touch_buttons[TouchID_FULLSCREEN].rect.x + touch_buttons[TouchID_FULLSCREEN].rect.w) + (10 * side_scale);
    touch_buttons[TouchID_AUDIO].rect.y = touch_buttons[TouchID_BACK].rect.y + (10 * side_scale);

    touch_buttons[TouchID_FASTFORWARD].rect.w /= 2;
    touch_buttons[TouchID_FASTFORWARD].rect.h /= 2;
    touch_buttons[TouchID_FASTFORWARD].rect.x = (touch_buttons[TouchID_FULLSCREEN].rect.x - touch_buttons[TouchID_FULLSCREEN].rect.w) - (10 * side_scale);
    touch_buttons[TouchID_FASTFORWARD].rect.y = touch_buttons[TouchID_BACK].rect.y + (10 * side_scale);

    touch_buttons[TouchID_START].rect.x = w / 2 + 5 * scale2;
    touch_buttons[TouchID_START].rect.y = 10 * scale2;

    touch_buttons[TouchID_SELECT].rect.x = w / 2 - touch_buttons[TouchID_SELECT].rect.w - 5 * scale2;
    touch_buttons[TouchID_SELECT].rect.y = 10 * scale2;

    touch_buttons[TouchID_OPTIONS].rect.x = w / 2 - touch_buttons[TouchID_OPTIONS].rect.w / 2;
    touch_buttons[TouchID_OPTIONS].rect.y = h - touch_buttons[TouchID_SELECT].rect.h - 10 * scale2;

    const auto resize_html_button = [this](const char* button_id, TouchID touch_id){
        const auto& rect = touch_buttons[touch_id].rect;
        const auto [btnx, btny] = get_render_to_window_scale(rect.x, rect.y);
        const auto [btnw, btnh] = get_render_to_window_scale(rect.w, rect.h);
        em_set_button_rect(button_id, btnx, btny, btnw, btnh);
    };

    resize_html_button("RomFilePicker", TouchID_OPEN);
    resize_html_button("DlSaves", TouchID_EXPORT);
}

auto App::rom_file_picker() -> void
{
    EM_ASM(
        let rom_input = document.getElementById("RomFilePicker");
        rom_input.click();
    );
}

auto App::on_speed_change() -> void
{
    // don't update audio if audio is disable!
    if (emu_audio_disabled || !SDL_WasInit(SDL_INIT_AUDIO))
    {
        return;
    }

    std::scoped_lock lock{core_mutex};

    if (emu_fast_forward)
    {
        gameboy_advance.set_audio_callback(on_audio_callback, sample_data, 65536 / 2);
    }
    else
    {
        gameboy_advance.set_audio_callback(on_audio_callback, sample_data);
    }
}

auto App::on_audio_change() -> void
{
    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        return;
    }

    std::scoped_lock lock0{core_mutex};
    std::scoped_lock lock1{audio_mutex};

    if (emu_audio_disabled)
    {
        SDL_AudioStreamClear(audio_stream);
        gameboy_advance.set_audio_callback(nullptr, {});
    }
    else
    {
        gameboy_advance.set_audio_callback(on_audio_callback, sample_data);
    }
}

auto App::on_flush_save() -> void
{
    // don't want to clear dirty flag as savegame also checks if
    // if flag is before saving.
    // i check the flag here in order to avoid locking the mutex
    // when not needed.
    if (has_rom && gameboy_advance.is_save_dirty(false))
    {
        std::scoped_lock lock{core_mutex};
        savegame("");
    }
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE auto em_load_rom_data(const char* name, uint8_t* data, int len) -> void
{
    emscripten_console_logf("[EM] loading rom! name: %s len: %d\n", name, len);

    if (len <= 0)
    {
        emscripten_console_logf("[EM] invalid rom size!\n");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "loadrom", "invalid rom size, less than or equal to zero!", nullptr);
        return;
    }

    auto event_data = new RomEventData;
    std::strcpy(event_data->name, name);
    event_data->data = static_cast<std::uint8_t*>(data);
    event_data->len = len;

    SDL_Event event{};
    event.user.type = ROM_LOAD_EVENT;
    event.user.data1 = event_data;
    SDL_PushEvent(&event);
}

EMSCRIPTEN_KEEPALIVE auto em_flush_save() -> void
{
    SDL_Event event{};
    event.user.type = FLUSH_SAVE_EVENT;
    SDL_PushEvent(&event);
}

EMSCRIPTEN_KEEPALIVE auto em_zip_all_saves() -> std::size_t
{
    const auto result = frontend::Base::zipall("/save", "TotalGBA_saves.zip");
    if (!result)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "No save files found!", "Try saving in game first\n\nIf you know there was a save file created, please contact me about the bug!", nullptr);
    }
    return result;
}

EMSCRIPTEN_KEEPALIVE auto main(int argc, char** argv) -> int
{
    auto app = std::make_unique<App>(argc, argv);
    emscripten_set_main_loop_arg(em_loop, app.get(), 0, true);
    return 0;
}

} // extern "C"
