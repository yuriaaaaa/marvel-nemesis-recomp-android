// The Android host: SDL's window and event loop in place of the Win32 one (sdk/runtime/app/main.cpp), the settings
// the launcher passes in the environment, and the game found in the folder the player picked.
//
// Environment (MarvelNemesisActivity.java):
//   MARVELNEMESIS_DIR       game folder: a disc image (.iso), or default.xbe with the disc's folders beside it
//   MARVELNEMESIS_DATA      Internal storage/MarvelNemesis: saves, mods, settings.toml and, with debug logging, logs
//   MARVELNEMESIS_CACHE     the app's own folder: shader and pipeline caches
//   MARVELNEMESIS_TOUCH     touch controls file (touch.cpp)
//   MARVELNEMESIS_SCALE     internal resolution, times the game's 640x480 (1 to 4)
//   MARVELNEMESIS_ASPECT    4:3 or 16:9 (the game is told it has a widescreen TV)
//   MARVELNEMESIS_VSYNC     0 or 1;  MARVELNEMESIS_FPS: frame cap, 0 for none
//   MARVELNEMESIS_DEBUG     1 writes every log line to MARVELNEMESIS_DATA/logs
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

#include <android/log.h>
#include <toml++/toml.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>

#include "app/app.h"
#include "audio/audio.h"
#include "core/input.h"
#include "core/log.h"
#include "vulkan.h"
#include "image_symbols.h"
#include "kernel/crypto.h"
#include "kernel/disc.h"
#include "kernel/kernel.h"
#include "touch.h"

extern const char kImageSha1[];      // the xbe recompiled into this library, written by CMake
extern const char kImageIdentity[];  // its code identity (xbe_identity.py)

namespace input {
void AddPad(SDL_JoystickID id);
void RemovePad(SDL_JoystickID id);
}  // namespace input

namespace app {

namespace {

namespace fs = std::filesystem;

SDL_Window* g_window = nullptr;
float g_aspect = 4.0f / 3.0f;

std::string Env(const char* name, const char* fallback = "") {
    const char* v = std::getenv(name);
    return v && *v ? v : fallback;
}

fs::path DataDir() { return Env("MARVELNEMESIS_DATA", "/sdcard/MarvelNemesis"); }

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string Sha1(const std::vector<uint8_t>& data) {
    alignas(8) uint8_t ctx[128];
    uint8_t digest[20];
    crypto::ShaInit(ctx);
    crypto::ShaUpdate(ctx, data.data(), data.size());
    crypto::ShaFinal(ctx, digest);
    std::string hex;
    for (uint8_t b : digest) {
        hex += "0123456789abcdef"[b >> 4];
        hex += "0123456789abcdef"[b & 15];
    }
    return hex;
}

// xbe_identity.py's value: entry, base and every section's address, sizes and stored digest; the stored digests are
// checked against the sections' bytes too. A header patch (allowed media, region) keeps it; other code does not.
std::string Identity(const std::vector<uint8_t>& x, std::string& why) {
    auto u32 = [&](size_t o) -> uint32_t {
        if (o + 4 > x.size()) return 0;
        return uint32_t(x[o]) | uint32_t(x[o + 1]) << 8 | uint32_t(x[o + 2]) << 16 | uint32_t(x[o + 3]) << 24;
    };
    if (x.size() < 0x180 || std::memcmp(x.data(), "XBEH", 4) != 0) {
        why = "not an xbe";
        return {};
    }
    uint32_t base = u32(0x104), count = u32(0x11C), headers = u32(0x120) - base;
    std::vector<uint8_t> id;
    auto put = [&](uint32_t v) {
        for (int i = 0; i < 4; i++) id.push_back(uint8_t(v >> (i * 8)));
    };
    put(base);
    put(u32(0x128));
    for (uint32_t i = 0; i < count; i++) {
        size_t o = size_t(headers) + size_t(i) * 56;
        if (o + 56 > x.size()) {
            why = "section headers past the end";
            return {};
        }
        uint32_t va = u32(o + 4), vsize = u32(o + 8), raw = u32(o + 12), rsize = u32(o + 16);
        put(va);
        put(vsize);
        put(rsize);
        id.insert(id.end(), x.begin() + long(o + 36), x.begin() + long(o + 56));
        if (size_t(raw) + rsize > x.size()) {
            why = "a section past the end of the file";
            return {};
        }
        alignas(8) uint8_t ctx[128];
        uint8_t digest[20];
        uint8_t size[4] = {uint8_t(rsize), uint8_t(rsize >> 8), uint8_t(rsize >> 16), uint8_t(rsize >> 24)};
        crypto::ShaInit(ctx);
        crypto::ShaUpdate(ctx, size, 4);
        crypto::ShaUpdate(ctx, x.data() + raw, rsize);
        crypto::ShaFinal(ctx, digest);
        if (std::memcmp(digest, x.data() + o + 36, 20) != 0) {
            why = "section " + std::to_string(i) + " was modified";
            return {};
        }
    }
    return Sha1(id);
}

bool ReadHostFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

void Message(const char* text) {
    LOGE("%s", text);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Marvel Nemesis", text, g_window);
}

// a crash on a guest thread: what it was running, into the log, before Android's own report
void OnCrash(int sig, siginfo_t* info, void*) {
    if (kernel::Thread* t = kernel::Current(); t && t->ctx) {
        LOGE("signal %d at %p on guest thread %X", sig, info ? info->si_addr : nullptr, t->id);
        if (info && (sig == SIGSEGV || sig == SIGBUS)) {
            uintptr_t va = reinterpret_cast<uintptr_t>(info->si_addr), g = reinterpret_cast<uintptr_t>(kernel::g_base);
            if (va >= g && va - g < (uintptr_t(1) << 32)) LOGE("  guest address %08llX", (unsigned long long)(va - g));
        }
        kernel::LogGuestStack(*t->ctx, 256);
    } else {
        LOGE("signal %d at %p on a host thread", sig, info ? info->si_addr : nullptr);
    }
    logx::Flush();
    __android_log_print(ANDROID_LOG_FATAL, "MarvelNemesis", "crashed (signal %d), see the log", sig);
    signal(sig, SIG_DFL);
    raise(sig);
}

void InstallCrashHandler() {
    struct sigaction sa {};
    sa.sa_sigaction = OnCrash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &sa, nullptr);
}

std::atomic<bool> g_background{false};
std::atomic<bool> g_focus_paused{false};
std::mutex g_focus_owner;

// the activity going to the background takes its window with it; the renderer waits for the new one
bool SDLCALL Lifecycle(void*, SDL_Event* e) {
    switch (e->type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
            gpu::g_paused.store(true, std::memory_order_release);
            g_background.store(true);
            touch::Cancel();
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            touch::PollConfig();
            gpu::g_paused.store(false, std::memory_order_release);
            g_background.store(false);
            break;
        default:
            break;
    }
    return true;
}

}  // namespace

float DisplayAspect() { return touch::Stretch() ? 0.0f : g_aspect; }

Settings& Get() {
    static Settings s;
    return s;
}

Settings ReadSettings() {
    std::lock_guard lock(UiLock());
    return Get();
}

namespace {

fs::path SettingsFile() { return DataDir() / kGame.settings; }

// what the launcher decides (MARVELNEMESIS_*), over whatever the in-game PC Settings saved
void ApplyLauncher(Settings& s) {
    s.display_mode = DisplayMode::Fullscreen;
    s.width = s.height = 0;
    s.render_percent = 100;
    s.vsync = Env("MARVELNEMESIS_VSYNC", "1") != "0";
    s.fps_cap = uint32_t(std::atoi(Env("MARVELNEMESIS_FPS", "60").c_str()));
    s.fps_auto = false;
    s.pause_on_focus_loss = true;
    s.monitor.clear();
    // the Windows build's extra passes (MSAA, SMAA, AO, softer and larger shadows) are D3D12 only
    s.msaa = 0;
    s.smaa = false;
    s.ambient_occlusion = 0;
    s.shadow_softness = 0;
    s.shadow_resolution = 0;
    g_aspect = Env("MARVELNEMESIS_ASPECT", "4:3") == "16:9" ? 16.0f / 9.0f : 4.0f / 3.0f;
}

toml::table Table() {
    const Settings& s = Get();
    toml::table t{{"anisotropic_filtering", int64_t(s.aniso)},
                  {"brightness", int64_t(s.brightness)},
                  {"contrast", int64_t(s.contrast)},
                  {"join_ip", s.join_ip}};
    for (const Option& o : kGame.options) {
        if (o.flag) t.insert(o.key, *o.flag);
        else t.insert(o.key, int64_t(*o.value));
    }
    if (kGame.save) kGame.save(&t);
    return t;
}

}  // namespace

// settings.toml in the data folder holds the game's own settings (controls, movies, object distance, cheats' switches);
// the display ones come from the launcher every run
void Load() {
    std::lock_guard lock(UiLock());
    Settings& s = Get();
    toml::table t;
    try {
        t = toml::parse_file(SettingsFile().string());
        s.aniso = t["anisotropic_filtering"].value_or(s.aniso);
        s.brightness = std::min(t["brightness"].value_or(s.brightness), 100u);
        s.contrast = std::min(t["contrast"].value_or(s.contrast), 100u);
        s.join_ip = t["join_ip"].value_or(s.join_ip);
    } catch (...) {
        t = {};
    }
    for (const Option& o : kGame.options) {
        if (o.flag) *o.flag = t[o.key].value_or(*o.flag);
        else *o.value = t[o.key].value_or(*o.value);
    }
    if (kGame.load) kGame.load(&t);
    ApplyLauncher(s);
}

bool Save() {
    std::lock_guard lock(UiLock());
    fs::path target = SettingsFile(), temporary = target;
    temporary += ".tmp";
    try {
        std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
        f << Table() << "\n";
        f.close();
        std::error_code ec;
        if (f && !f.fail()) {
            fs::rename(temporary, target, ec);
            if (!ec) return true;
        }
    } catch (...) {
    }
    std::error_code ec;
    fs::remove(temporary, ec);
    LOGE("settings: could not save %s; previous file preserved", target.string().c_str());
    return false;
}

void LogSettings() {
    const Settings s = ReadSettings();
    LOGR("Settings: scale %.0fx, aspect %s%s, vsync %d, fps cap %u, aniso %u, brightness %u, contrast %u", RenderScale(480),
         Env("MARVELNEMESIS_ASPECT", "4:3").c_str(), touch::Stretch() ? " stretched" : "", s.vsync, s.fps_cap, s.aniso,
         s.brightness, s.contrast);
}

void LogSystem() {
    int v = SDL_GetVersion();
    LOGR("System: Android, SDL %d.%d.%d, game folder %s", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v),
         Env("MARVELNEMESIS_DIR").c_str());
    LogSettings();
}

void ApplyDisplay() {}

Mode Desktop() {
    int w = 0, h = 0;
    if (g_window) SDL_GetWindowSizeInPixels(g_window, &w, &h);
    return {uint32_t(w > 0 ? w : 1920), uint32_t(h > 0 ? h : 1080)};
}
Mode Output() { return Desktop(); }
Mode Desktop(const std::string&) { return Desktop(); }
std::vector<Display> Displays() {
    Display d;
    d.device = "android";
    d.number = 1;
    d.desktop = Desktop();
    d.refresh_hz = RefreshRate();
    d.primary = true;
    return {d};
}
void* MonitorHandle(const std::string&) { return nullptr; }
void RefreshDisplayInfo(bool) {}
uint32_t RefreshRate() {
    SDL_DisplayID id = g_window ? SDL_GetDisplayForWindow(g_window) : 0;
    const SDL_DisplayMode* m = id ? SDL_GetCurrentDisplayMode(id) : nullptr;
    return m && m->refresh_rate > 1 ? uint32_t(std::lround(m->refresh_rate)) : 0;
}
uint32_t EffectiveFpsCap() { return Get().fps_cap; }

// the Vulkan backend takes this as a multiplier of the game's own resolution
float RenderScale(uint32_t) {
    int s = std::atoi(Env("MARVELNEMESIS_SCALE", "2").c_str());
    return float(std::clamp(s, 1, 4));
}

bool Widescreen() { return g_aspect > 1.5f; }

fs::path ExeDir() { return DataDir(); }

bool FindGame(GameFiles& game) {
    fs::path dir = Env("MARVELNEMESIS_DIR", DataDir().c_str());
    std::error_code ec;
    std::vector<fs::path> isos;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && Lower(e.path().extension().string()) == ".iso") isos.push_back(e.path());
    std::sort(isos.begin(), isos.end());

    bool other = false;
    auto consider = [&](const fs::path& name, std::vector<uint8_t> xbe, std::unique_ptr<kernel::Disc> disc) {
        std::string sha = Sha1(xbe), why;
        std::string id = Identity(xbe, why);
        bool match = sha == kImageSha1 || id == kImageIdentity;
        LOGR("Game: %s in %s, sha1 %s, code %s%s%s", IMAGE_XBE, name.string().c_str(), sha.c_str(),
             id.empty() ? "?" : id.c_str(), match ? "" : ", another release", why.empty() ? "" : (" (" + why + ")").c_str());
        if (!match) {
            other = true;
            return false;
        }
        game = {dir, std::move(xbe)};
        kernel::MountDisc(std::move(disc));
        return true;
    };
    std::vector<uint8_t> xbe;
    if (ReadHostFile(dir / IMAGE_XBE, xbe) && consider(dir, std::move(xbe), nullptr)) return true;
    for (const fs::path& iso : isos) {
        auto disc = std::make_unique<kernel::Disc>();
        std::string error;
        if (!disc->Open(iso, error)) {
            LOGW("game: %s: %s", iso.string().c_str(), error.c_str());
            continue;
        }
        if (!disc->Load(IMAGE_XBE, xbe)) {
            LOGW("game: %s has no %s", iso.string().c_str(), IMAGE_XBE);
            continue;
        }
        if (consider(iso, std::move(xbe), std::move(disc))) return true;
    }
    Message(other ? "The game in this folder is a different release than this build was made from (it needs the USA "
                    "Xbox Marvel Nemesis disc). Pick the USA copy in the launcher."
                  : "No game found. Put the disc image (.iso), or default.xbe with the disc's folders, in the game "
                    "folder chosen in the launcher.");
    return false;
}

int Instance() { return 0; }
void* Window() { return g_window; }

bool OpenSplash() { return false; }
void SplashFirstFrame() {}
void CloseSplash() {}
void OpenGameWindow(bool) {}
bool StartHandover() { return false; }
bool HandingOver() { return false; }
void FinishHandover() {}

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

// the app in the background: the guest's presenting thread waits here with its clocks, sound and rumble held
// (the Windows build's pause on focus loss)
bool FocusPaused() { return g_focus_paused.load(); }

void WaitWhileUnfocused() {
    for (;;) {
        if (!g_background.load()) {
            if (!FocusPaused()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        std::unique_lock owner(g_focus_owner, std::try_to_lock);
        if (!owner.owns_lock()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        g_focus_paused.store(true);
        kernel::HoldGuestTime(true);
        audio::SetPaused(true);
        input::StopRumble();
        LOGR("focus: paused (simulation/audio/clocks)");
        while (g_background.load()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        kernel::HoldGuestTime(false);
        audio::SetPaused(false);
        g_focus_paused.store(false);
        LOGR("focus: resumed");
        return;
    }
}

void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t cap = Get().fps_cap;
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    while (clock::now() + std::chrono::milliseconds(2) < next) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (clock::now() < next) std::this_thread::yield();
}

}  // namespace app

int main(int, char**) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path data = app::DataDir();
    fs::create_directories(data, ec);
    // the renderer keeps its caches in the working folder
    if (std::string cache = app::Env("MARVELNEMESIS_CACHE"); !cache.empty()) {
        fs::create_directories(cache, ec);
        if (chdir(cache.c_str()) != 0) __android_log_print(ANDROID_LOG_WARN, "MarvelNemesis", "chdir %s failed", cache.c_str());
    }
    bool debug = app::Env("MARVELNEMESIS_DEBUG") == "1";
    logx::kVerbose = debug;
    // errors only go to the app's own folder unless debug logging is on
    logx::Init(debug ? (data / "logs").string() : fs::current_path(ec).string(), app::kGame.log);
    app::InstallCrashHandler();
    app::Load();
    if (app::kGame.init) app::kGame.init();
    app::LogSystem();

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        __android_log_print(ANDROID_LOG_FATAL, "MarvelNemesis", "SDL_Init: %s", SDL_GetError());
        LOGE("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_AddEventWatch(app::Lifecycle, nullptr);
    app::g_window = SDL_CreateWindow("Marvel Nemesis", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app::g_window) {
        LOGE("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig fc;
    fc.SizePixels = 48.0f;  // the touch labels are drawn large; one big font keeps them sharp
    io.Fonts->AddFontDefault(&fc);
    ImGui_ImplSDL3_InitForVulkan(app::g_window);

    input::Init();
    app::GameFiles game;
    if (!app::FindGame(game)) {
        logx::Flush();
        return 1;
    }
    std::filesystem::path storage;
    std::string storage_error;
    if (!app::PrepareStorage(storage, storage_error)) {
        LOGE("storage: %s", storage_error.c_str());
        app::Message("Unable to prepare the saves folder. Existing files were preserved. See MarvelNemesis/logs.");
        logx::Flush();
        return 1;
    }
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), storage.string())) {
        app::Message("The game failed to start. Turn on Debug logging in the launcher and look in MarvelNemesis/logs.");
        logx::Flush();
        return 1;
    }

    uint64_t last_flush = SDL_GetTicks();
    for (;;) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 250)) {
            do {
                switch (e.type) {
                    case SDL_EVENT_QUIT:
                    case SDL_EVENT_TERMINATING:
                        logx::Flush();
                        kernel::Shutdown(0);
                    case SDL_EVENT_FINGER_DOWN:
                        touch::Event(touch::kDown, e.tfinger.fingerID, e.tfinger.x, e.tfinger.y);
                        break;
                    case SDL_EVENT_FINGER_MOTION:
                        touch::Event(touch::kMove, e.tfinger.fingerID, e.tfinger.x, e.tfinger.y);
                        break;
                    case SDL_EVENT_FINGER_UP:
                    case SDL_EVENT_FINGER_CANCELED:
                        touch::Event(touch::kUp, e.tfinger.fingerID, e.tfinger.x, e.tfinger.y);
                        break;
                    case SDL_EVENT_GAMEPAD_ADDED: input::AddPad(e.gdevice.which); break;
                    case SDL_EVENT_GAMEPAD_REMOVED: input::RemovePad(e.gdevice.which); break;
                    case SDL_EVENT_WINDOW_RESIZED:
                    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                        gpu::g_window_resized.store(true, std::memory_order_relaxed);
                        break;
                    default: break;
                }
            } while (SDL_PollEvent(&e));
        }
        uint64_t now = SDL_GetTicks();
        if (now - last_flush >= 1000) {
            last_flush = now;
            touch::PollConfig();
            logx::Flush();
        }
    }
}
