// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/settings.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "filesystem_fixture.h"

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
        path = TestDirectory("kirby-settings-");
    }
    ~Directory() { std::error_code ec; fs::remove_all(path, ec); }
};
}
int main() {
    Directory temp;
    const auto file = temp.path / "user/settings.toml";
    const auto data = (temp.path / "Kirby's DATA \"test\" \\ files").lexically_normal();
    Kirby::Settings settings{data, 37.5f, false, true};
    settings.resolutionScale = 3;
    settings.msaaSamples = 4;
    settings.fullscreen = true;
    Kirby::SaveSettings(file, settings);
    const auto loaded = Kirby::LoadSettings(file);
    Check(loaded.gameData == data && loaded.masterVolume == 37.5f &&
          !loaded.vsync && loaded.gpuResidentCopies, "Settings did not round-trip");
    Check(loaded.resolutionScale == 3 && loaded.msaaSamples == 4, "Image quality settings did not round-trip");
    Check(loaded.fullscreen, "Fullscreen preference did not round-trip");
    Write(file, "[kirby]\nschema_version = 1\n");
    Check(!Kirby::LoadSettings(file).fullscreen, "Older settings should default to windowed mode");
    Kirby::SaveSettings(file, settings);
    for (const int samples : {1,4}) for (const bool fxaa : {false,true}) {
        settings.resolutionScale = 8; settings.msaaSamples = samples; settings.fxaa = fxaa;
        Kirby::SaveSettings(file, settings);
        const auto quality = Kirby::LoadSettings(file);
        Check(quality.resolutionScale == 8 && quality.msaaSamples == samples && quality.fxaa == fxaa,
              "8x resolution and all AA combinations must survive save/reload");
    }
    const auto serialized = Read(file);
    Write(file, serialized + "\n# Keep custom values\n[custom]\nanswer = 42\n");
    settings.masterVolume = 19;
    Kirby::SaveSettings(file, settings);
    const auto updated = Read(file);
    Check(updated.find("answer = 42") != std::string::npos &&
          updated.find("Keep custom values") != std::string::npos, "Unrelated values/comments were lost");
    Check(std::abs(Kirby::LoadSettings(file).masterVolume - 19) < 0.0001f, "New volume not persisted");
#ifdef _WIN32
    {
        // A failed final commit must leave the old file intact and clean up its
        // already-written temporary file. Deny delete/rename while allowing reads.
        Kirby::WindowsFile::Handle locked(CreateFileW(file.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        Check(bool(locked), "Cannot lock settings for failed-commit test");
        Reject([&] { Kirby::SaveSettings(file, settings); });
        Check(Read(file) == updated, "Failed atomic replacement damaged settings");
        Check(std::distance(fs::directory_iterator(file.parent_path()), fs::directory_iterator{}) == 1,
              "Failed atomic replacement leaked a temporary file");
    }
#endif
    const auto unicodeFile = temp.path / fs::path(u8"設定") / fs::path(u8"設定.toml");
    Kirby::SaveSettings(unicodeFile, settings);
    Check(Kirby::LoadSettings(unicodeFile).masterVolume == settings.masterVolume,
          "Unicode settings path did not round-trip");
    const auto jump = static_cast<size_t>(Kirby::GameAction::Jump);
    Check(Kirby::Rebind(settings.controls, jump, Kirby::BindingDevice::Gamepad, "right_shoulder").status ==
          Kirby::RebindResult::Status::Applied, "Gamepad binding edit failed");
    Check(Kirby::Rebind(settings.controls, jump, Kirby::BindingDevice::Keyboard, "J").status ==
          Kirby::RebindResult::Status::Applied, "Keyboard binding edit failed");
    Kirby::SaveSettings(file, settings);
    Check(Kirby::LoadSettings(file).controls == settings.controls, "Bindings did not persist across reload");
    const auto validBindings = Read(file);
    settings.motion.tilt = false; settings.motion.invertX = true;
    settings.motion.pointer = Kirby::PointerSource::RightStick;
    settings.motion.sensitivity = 2.25f; settings.motion.tiltGain = 1.75f;
    settings.motion.calibrationDevice = "00112233445566778899aabbccddeeff";
    settings.motion.bias = {.01f, -.02f, .03f};
    Kirby::SaveSettings(file, settings);
    Check(Kirby::LoadSettings(file).motion == settings.motion, "Motion settings/calibration did not round-trip");
    Write(file, validBindings);
    auto invalidBindings = settings;
    invalidBindings.controls.gamepad[jump] = "east";
    Reject([&] { Kirby::SaveSettings(file, invalidBindings); });
    Check(Read(file) == validBindings, "Rejected conflicting bindings damaged the saved configuration");

    settings.controls.stickActivation = 0.25f;
    settings.controls.triggerActivation = 0.75f;
    Kirby::SaveSettings(file, settings);
    Check(Kirby::LoadSettings(file).controls == settings.controls,
          "Axis activation settings did not survive save/reload");
    const auto validActivation = Read(file);
    for (const float invalid : {0.f, 1.f, -0.1f, std::nanf(""), INFINITY}) {
        for (const bool trigger : {false, true}) {
            auto candidate = settings;
            (trigger ? candidate.controls.triggerActivation : candidate.controls.stickActivation) = invalid;
            Reject([&] { Kirby::SaveSettings(file, candidate); });
            Check(Read(file) == validActivation, "Invalid activation settings damaged the saved configuration");
        }
    }
    settings.controls.stickActivation = 0.05f;
    settings.controls.triggerActivation = 0.95f;
    Kirby::SaveSettings(file, settings);
    Check(Kirby::LoadSettings(file).controls == settings.controls,
          "Activation endpoints did not survive numeric serialization");

    for (const auto* invalid : {"[broken", "[kirby]\nschema_version = 99\n",
            "[kirby]\nschema_version = 1\n[video]\nresolution_scale = 0\n",
            "[kirby]\nschema_version = 1\n[video]\nresolution_scale = 7\n",
            "[kirby]\nschema_version = 1\n[video]\nresolution_scale = 9\n",
            "[kirby]\nschema_version = 1\n[video]\nfxaa = 1\n",
            "[kirby]\nschema_version = 1\n[video]\nfullscreen = 1\n",
            "[kirby]\nschema_version = 1\n[video]\nresolution_scale = 2.5\n",
            "[kirby]\nschema_version = 1\n[video]\nresolution_scale = true\n",
            "[kirby]\nschema_version = 1\n[video]\nmsaa_samples = 2\n",
            "[kirby]\nschema_version = 1\n[video]\nmsaa_samples = 8\n",
            "[kirby]\nschema_version = 1\n[video]\nmsaa_samples = \"4\"\n",
            "[kirby]\nschema_version = 1\n[audio]\nvolume = nan\n",
            "[kirby]\nschema_version = 1\n[paths]\ndvd_root = false\n",
            "[kirby]\nschema_version = 1\nmotion = 1\n[motion]\ntilt = 1\n",
            "[kirby]\nschema_version = 1\n[motion]\npointer = \"invalid\"\n",
            "[kirby]\nschema_version = 1\n[motion]\nsensitivity = nan\n",
            "[kirby]\nschema_version = 1\n[motion]\ntilt_gain = 99\n",
            "[kirby]\nschema_version = 1\n[motion]\ndeadzone = -1\n",
            "[kirby]\nschema_version = 1\n[motion]\ncalibration_device = \"invalid\"\n"}) {
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
    Check(missing.resolutionScale == 1 && missing.msaaSamples == 1, "Missing settings must default to native resolution without MSAA");
    Check(!missing.fxaa, "Missing FXAA option must default to off");

    const auto legacy = temp.path / "launcher.cfg";
    Write(legacy, "relative DATA\n62\n0\n1\n");
    auto migrated = Kirby::LoadOrMigrateSettings(file, legacy, temp.path);
    Check(migrated.gameData == temp.path / "relative DATA" && migrated.masterVolume == 62 &&
          !migrated.vsync && migrated.gpuResidentCopies, "Legacy settings were not migrated");
    Check(Read(legacy) == "relative DATA\n62\n0\n1\n", "Legacy file was modified");
    const auto crlfLegacy = temp.path / "windows-launcher.cfg";
    const auto crlfFile = temp.path / "windows-settings.toml";
    std::ofstream(crlfLegacy, std::ios::binary) << "relative DATA\r\n62\r\n0\r\n1\r\n";
    const auto crlf = Kirby::LoadOrMigrateSettings(crlfFile, crlfLegacy, temp.path);
    Check(crlf.gameData == temp.path / "relative DATA" && crlf.masterVolume == 62 &&
          !crlf.vsync && crlf.gpuResidentCopies, "Windows CRLF legacy settings were not migrated");
    Write(legacy, "other DATA\n25\n1\n0\n");
    Check(Kirby::LoadOrMigrateSettings(file, legacy, temp.path).masterVolume == 62,
          "Legacy file overwrote an existing TOML file");
    fs::remove(file);
    Write(legacy, "資料 DATA\n42\n1\n");
    migrated = Kirby::LoadOrMigrateSettings(file, legacy, temp.path);
    Check(migrated.gameData == temp.path / fs::path(u8"資料 DATA"),
          "Legacy UTF-8 DATA path was corrupted during migration");
    fs::remove(file);
    Write(legacy, data.string() + "\n42\n1\n");
    migrated = Kirby::LoadOrMigrateSettings(file, legacy, temp.path);
    Check(migrated.masterVolume == 42 && migrated.vsync && migrated.gpuResidentCopies,
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
