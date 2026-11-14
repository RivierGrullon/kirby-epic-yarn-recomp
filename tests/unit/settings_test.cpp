// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/settings.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace fs = std::filesystem;
namespace {
unsigned checks;
void Check(bool pass, const char* message) {
    ++checks;
    if (!pass) throw std::runtime_error(message);
}
void Write(const fs::path& file, const std::string& text) { std::ofstream(file) << text; }
std::string Read(const fs::path& file) {
    std::ifstream input(file);
    return {std::istreambuf_iterator<char>(input), {}};
}
template<class F> void Reject(F&& action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, "Invalid settings were accepted");
}
struct Directory {
    fs::path path;
    Directory() {
        char name[] = "/tmp/kirby-settings-XXXXXX";
        if (!mkdtemp(name)) throw std::runtime_error("Cannot create test directory");
        path = name;
    }
    ~Directory() { std::error_code ec; fs::remove_all(path, ec); }
};
}
int main() {
    Directory temp;
    const auto file = temp.path / "user/settings.toml";
    const auto data = temp.path / "Kirby's DATA \"test\" \\ files";
    Kirby::Settings settings{data, 37.5f, false, true};
    Kirby::SaveSettings(file, settings);
    const auto loaded = Kirby::LoadSettings(file);
    Check(loaded.gameData == data && loaded.masterVolume == 37.5f &&
          !loaded.vsync && loaded.gpuResidentCopies, "Settings did not round-trip");
    const auto serialized = Read(file);
    Write(file, serialized + "\n# Keep custom values\n[custom]\nanswer = 42\n");
    settings.masterVolume = 19;
    Kirby::SaveSettings(file, settings);
    const auto updated = Read(file);
    Check(updated.find("answer = 42") != std::string::npos &&
          updated.find("Keep custom values") != std::string::npos, "Unrelated values/comments were lost");
    Check(std::abs(Kirby::LoadSettings(file).masterVolume - 19) < 0.0001f, "New volume not persisted");
    const auto jump = static_cast<size_t>(Kirby::GameAction::Jump);
    Check(Kirby::Rebind(settings.controls, jump, Kirby::BindingDevice::Gamepad, "right_shoulder").status ==
          Kirby::RebindResult::Status::Applied, "Gamepad binding edit failed");
    Check(Kirby::Rebind(settings.controls, jump, Kirby::BindingDevice::Keyboard, "J").status ==
          Kirby::RebindResult::Status::Applied, "Keyboard binding edit failed");
    Kirby::SaveSettings(file, settings);
    Check(Kirby::LoadSettings(file).controls == settings.controls, "Bindings did not persist across reload");
    const auto validBindings = Read(file);
    auto invalidBindings = settings;
    invalidBindings.controls.gamepad[jump] = "east";
    Reject([&] { Kirby::SaveSettings(file, invalidBindings); });
    Check(Read(file) == validBindings, "Rejected conflicting bindings damaged the saved configuration");

    for (const auto* invalid : {"[broken", "[kirby]\nschema_version = 99\n",
            "[kirby]\nschema_version = 1\n[audio]\nvolume = nan\n",
            "[kirby]\nschema_version = 1\n[paths]\ndvd_root = false\n"}) {
        Write(file, invalid);
        Reject([&] { Kirby::LoadSettings(file); });
        Reject([&] { Kirby::SaveSettings(file, settings); });
        Check(Read(file) == invalid, "Invalid source was overwritten");
    }
    fs::remove(file);
    auto missing = Kirby::LoadSettings(file, {data, 80, false, false});
    Check(missing.gameData == data && missing.masterVolume == 80 && !missing.vsync,
          "Missing-file defaults were ignored");
    Check(!fs::exists(file), "Read created a configuration file");

    const auto legacy = temp.path / "launcher.cfg";
    Write(legacy, "relative DATA\n62\n0\n1\n");
    auto migrated = Kirby::LoadOrMigrateSettings(file, legacy, temp.path);
    Check(migrated.gameData == temp.path / "relative DATA" && migrated.masterVolume == 62 &&
          !migrated.vsync && migrated.gpuResidentCopies, "Legacy settings were not migrated");
    Check(Read(legacy) == "relative DATA\n62\n0\n1\n", "Legacy file was modified");
    Write(legacy, "other DATA\n25\n1\n0\n");
    Check(Kirby::LoadOrMigrateSettings(file, legacy, temp.path).masterVolume == 62,
          "Legacy file overwrote an existing TOML file");
    fs::remove(file);
    Write(legacy, data.string() + "\n42\n1\n");
    migrated = Kirby::LoadOrMigrateSettings(file, legacy, temp.path);
    Check(migrated.masterVolume == 42 && migrated.vsync && !migrated.gpuResidentCopies,
          "Older three-line legacy file did not migrate");
    fs::remove(file);
    Write(legacy, "DATA\nnot a volume\n0\n");
    Reject([&] { Kirby::LoadOrMigrateSettings(file, legacy, temp.path); });
    Check(!fs::exists(file), "Bad legacy input created a TOML file");

    const auto blocker = temp.path / "file-not-directory";
    Write(blocker, "keep");
    Reject([&] { Kirby::SaveSettings(blocker / "settings.toml", settings); });
    Check(Read(blocker) == "keep", "Failed save damaged another file");
    std::cout << checks << " settings persistence/migration checks passed\n";
}
