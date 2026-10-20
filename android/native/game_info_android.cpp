// What the runtime asks of the game on Android: the Windows build's hooks (src/common/game_info.cpp) with the touch
// controls added to player 1, no window messages (no keyboard or F10 panel; the game's own Options > PC Settings menu
// works with the pad or the touch controls) and no XBE pin: app_android.cpp checks the game's code identity instead.
#include "app/app.h"
#include "common/pc_controls.h"
#include "common/pc_render.h"
#include "common/pc_video.h"
#include "common/settings_panel.h"
#include "core/input.h"
#include "touch.h"

namespace {

// player 1 is the touch controls and its controller together, always connected so the game starts without a pad
void OnPad(int port, input::Pad& pad) {
    game::settings_panel::FilterPad(port, pad);
    if (port != 0) return;
    touch::Apply(pad);
    pad.connected = true;
}

}  // namespace

const app::GameInfo app::kGame = {
    .title = L"Marvel Nemesis: Rise of the Imperfects",
    .short_title = L"Marvel Nemesis",
    .name = L"MarvelNemesis",
    .log = "marvelnemesis",
    .settings = "settings.toml",
    .splash = L"splash",
    .missing = L"Put the game in Internal storage/MarvelNemesis: its disc image (.iso), or default.xbe with its folders.",
    .images = {{"default.xbe", "MarvelNemesis"}},
    .options = {{"fps_counter", &game::settings_panel::FpsCounter(), nullptr}},
    .load = [](const void* table) {
        game::pc_controls::Load(table);
        game::pc_video::Load(table);
        game::pc_render::Load(table);
    },
    .save = [](void* table) {
        game::pc_controls::Save(table);
        game::pc_video::Save(table);
        game::pc_render::Save(table);
    },
    .init = game::settings_panel::Initialize,
    .loaded = nullptr,
    .window = nullptr,
    .message = nullptr,
    .ui = [](double fps) {
        game::settings_panel::Draw(fps);
        touch::Draw();
    },
    .pad = OnPad,
    .rumble = game::settings_panel::FilterRumble,
    .controller_port = game::pc_controls::ControllerPort,
    .ui_input = nullptr,
    .supported_xbe_sha1 = nullptr,
    .data_directory = "saves",
    .mods_directory = "mods",
};
