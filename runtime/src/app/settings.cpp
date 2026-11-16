// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/settings.h"
// Reuse WiiCompiled's pinned GPL TOML helpers, not its product defaults or global
// config singleton. See dependencies.json and docs/references.md for attribution.
#include "runtime_config.h"
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace Kirby {
namespace {
void Validate(const Settings& settings) {
    if (settings.gameData.empty() || settings.gameData.native().find(fs::path::value_type{}) != fs::path::string_type::npos)
        throw std::runtime_error("paths.dvd_root must be a nonempty path");
    if (!std::isfinite(settings.masterVolume) || settings.masterVolume < 0 || settings.masterVolume > 100)
        throw std::runtime_error("audio.volume must be between 0 and 1");
    ValidateControlBindings(settings.controls);
    ValidateMotionSettings(settings.motion);
    if (!ValidResolutionScale(settings.resolutionScale))
        throw std::runtime_error("video.resolution_scale must be 1, 2, 3, 4, 5, 6 or 8");
    if (settings.msaaSamples != 1 && settings.msaaSamples != 4)
        throw std::runtime_error("video.msaa_samples must be 1 (off) or 4");
}
std::optional<std::string> Read(const fs::path& file) {
    if (!fs::exists(file)) return std::nullopt;
    if (!fs::is_regular_file(file) || fs::file_size(file) > 262144)
        throw std::runtime_error("configuration must be a regular file smaller than 256 KiB");
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + RuntimeConfigFile::PathToUtf8(file));
    std::string text{std::istreambuf_iterator<char>(input), {}};
    if (input.bad()) throw std::runtime_error("failed to read " + RuntimeConfigFile::PathToUtf8(file));
    return text;
}
Settings Parse(const std::string& text, const fs::path& file, Settings defaults) {
    std::istringstream input(text);
    const auto document = toml::parse(input, RuntimeConfigFile::PathToUtf8(file));
    using namespace RuntimeConfigFile;
    for (const char* section : {"kirby", "paths", "audio", "video", "controller", "keyboard", "motion"})
        if (document.contains(section) && !document.at(section).is_table())
            throw std::runtime_error(std::string(section) + " must be a table");
    if (FindConfigValue<int64_t>(document, "kirby", "schema_version") != 1)
        throw std::runtime_error("unsupported or missing kirby.schema_version; expected 1");
    const auto present = [&](const char* section, const char* key) {
        return document.contains(section) && document.at(section).contains(key);
    };
    // Upstream ignores malformed optional fields. Kirby reports malformed values
    // for the subset it implements, instead of silently changing their meaning.
    if (present("video", "vsync") && !FindConfigValue<bool>(document, "video", "vsync"))
        throw std::runtime_error("video.vsync must be a boolean");
    const auto gpuCopies = FindConfigValue<bool>(document, "kirby", "gpu_resident_copies");
    if (present("kirby", "gpu_resident_copies") && !gpuCopies)
        throw std::runtime_error("kirby.gpu_resident_copies must be a boolean");
    const RuntimeUserConfig shared = ParseConfigDocument(document);
    if (present("audio", "volume") && !shared.audioVolume)
        throw std::runtime_error("audio.volume must be a number between 0 and 1");
    if (present("paths", "dvd_root") && !shared.dvdRoot)
        throw std::runtime_error("paths.dvd_root must be a string");
    if (shared.dvdRoot) {
        defaults.gameData = PathFromUtf8(*shared.dvdRoot);
        if (defaults.gameData.empty()) throw std::runtime_error("paths.dvd_root must be nonempty");
        if (defaults.gameData.is_relative()) defaults.gameData = fs::absolute(file).parent_path() / defaults.gameData;
    }
    if (shared.audioVolume) defaults.masterVolume = *shared.audioVolume * 100;
    if (shared.vsync) defaults.vsync = *shared.vsync;
    if (gpuCopies) defaults.gpuResidentCopies = *gpuCopies;
    for (const auto& [key, destination] : std::array{
            std::pair{"stick_activation", &defaults.controls.stickActivation},
            std::pair{"trigger_activation", &defaults.controls.triggerActivation}}) {
        if (!present("controller", key)) continue;
        const auto value = FindConfigValue<double>(document, "controller", key);
        if (!value || !std::isfinite(*value) || *value < 0.05 || *value > 0.95)
            throw std::runtime_error(std::string("controller.") + key + " must be between 0.05 and 0.95");
        *destination = static_cast<float>(*value);
    }
    const auto integer = [&](const char* key, int& target, int minimum, int maximum) {
        if (!present("video", key)) return;
        const auto value = FindConfigValue<int64_t>(document, "video", key);
        if (!value || *value < minimum || *value > maximum)
            throw std::runtime_error(std::string("video.") + key + " is not a supported integer");
        target = static_cast<int>(*value);
    };
    integer("resolution_scale", defaults.resolutionScale, 1, 8);
    integer("msaa_samples", defaults.msaaSamples, 1, 4);
    if (present("video", "fxaa")) {
        const auto value = FindConfigValue<bool>(document, "video", "fxaa");
        if (!value) throw std::runtime_error("video.fxaa must be a boolean");
        defaults.fxaa = *value;
    }
    for (size_t i = 0; i < GameActions.size(); ++i) {
        for (const auto device : {BindingDevice::Gamepad, BindingDevice::Keyboard}) {
            const char* section = device == BindingDevice::Gamepad ? "controller" : "keyboard";
            if (!present(section, GameActions[i].key)) continue;
            const auto value = FindConfigValue<std::string>(document, section, GameActions[i].key);
            if (!value) throw std::runtime_error(std::string(section) + "." + GameActions[i].key + " must be a binding string");
            auto& bindings = device == BindingDevice::Gamepad ? defaults.controls.gamepad : defaults.controls.keyboard;
            bindings[i] = CanonicalBinding(device, *value);
        }
    }
    auto& motion = defaults.motion;
    const auto boolean = [&](const char* key, bool& target) {
        if (!present("motion", key)) return;
        const auto value = FindConfigValue<bool>(document, "motion", key);
        if (!value) throw std::runtime_error(std::string("motion.") + key + " must be a boolean");
        target = *value;
    };
    const auto number = [&](const char* key, float& target) {
        if (!present("motion", key)) return;
        auto value = FindConfigValue<double>(document, "motion", key);
        if (!value) if (const auto integer = FindConfigValue<int64_t>(document, "motion", key)) value = double(*integer);
        if (!value || !std::isfinite(*value)) throw std::runtime_error(std::string("motion.") + key + " must be a finite number");
        target = static_cast<float>(*value);
    };
    boolean("tilt", motion.tilt); boolean("invert_tilt", motion.invertTilt);
    boolean("invert_x", motion.invertX); boolean("invert_y", motion.invertY);
    boolean("keyboard", motion.keyboard);
    number("sensitivity", motion.sensitivity); number("tilt_gain", motion.tiltGain);
    number("deadzone", motion.deadzone); number("stick_deadzone", motion.stickDeadzone);
    number("bias_x", motion.bias[0]); number("bias_y", motion.bias[1]); number("bias_z", motion.bias[2]);
    if (present("motion", "pointer")) {
        const auto value = FindConfigValue<std::string>(document, "motion", "pointer");
        if (value == "mouse") motion.pointer = PointerSource::Mouse;
        else if (value == "gyro") motion.pointer = PointerSource::Gyro;
        else if (value == "right_stick") motion.pointer = PointerSource::RightStick;
        else if (value == "disabled") motion.pointer = PointerSource::Disabled;
        else throw std::runtime_error("motion.pointer must be mouse, gyro, right_stick or disabled");
    }
    if (present("motion", "calibration_device")) {
        const auto value = FindConfigValue<std::string>(document, "motion", "calibration_device");
        if (!value) throw std::runtime_error("motion.calibration_device must be a string");
        motion.calibrationDevice = *value;
    }
    Validate(defaults);
    defaults.gameData = fs::absolute(defaults.gameData).lexically_normal();
    return defaults;
}
std::string Update(std::string text, const Settings& settings) {
    Validate(settings);
    const auto set = [&](const char* section, const char* key, const std::string& value) {
        auto updated = RuntimeConfigFile::UpdateSettingText(text, section, key, value);
        if (!updated) throw std::runtime_error("cannot update configuration document");
        text = std::move(*updated);
    };
    set("kirby", "schema_version", "1");
    set("kirby", "gpu_resident_copies", settings.gpuResidentCopies ? "true" : "false");
    set("paths", "dvd_root", RuntimeConfigFile::FormatString(
        RuntimeConfigFile::PathToUtf8(fs::absolute(settings.gameData).lexically_normal())));
    std::ostringstream volume;
    volume << std::setprecision(std::numeric_limits<float>::max_digits10) << settings.masterVolume / 100;
    set("audio", "volume", volume.str());
    set("video", "vsync", settings.vsync ? "true" : "false");
    set("video", "resolution_scale", std::to_string(settings.resolutionScale));
    set("video", "msaa_samples", std::to_string(settings.msaaSamples));
    set("video", "fxaa", settings.fxaa ? "true" : "false");
    for (const auto& [key, value] : std::array{
            std::pair{"stick_activation", settings.controls.stickActivation},
            std::pair{"trigger_activation", settings.controls.triggerActivation}}) {
        std::ostringstream number;
        number << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
        set("controller", key, number.str());
    }
    const auto& motion = settings.motion;
    const auto boolean = [&](const char* key, bool value) { set("motion", key, value ? "true" : "false"); };
    const auto number = [&](const char* key, float value) {
        std::ostringstream formatted;
        formatted << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
        set("motion", key, formatted.str());
    };
    boolean("tilt", motion.tilt); boolean("invert_tilt", motion.invertTilt);
    boolean("invert_x", motion.invertX); boolean("invert_y", motion.invertY);
    boolean("keyboard", motion.keyboard);
    number("sensitivity", motion.sensitivity); number("tilt_gain", motion.tiltGain);
    number("deadzone", motion.deadzone); number("stick_deadzone", motion.stickDeadzone);
    constexpr const char* pointers[]{"mouse", "gyro", "right_stick", "disabled"};
    set("motion", "pointer", RuntimeConfigFile::FormatString(pointers[static_cast<int>(motion.pointer)]));
    set("motion", "calibration_device", RuntimeConfigFile::FormatString(motion.calibrationDevice));
    number("bias_x", motion.bias[0]); number("bias_y", motion.bias[1]); number("bias_z", motion.bias[2]);
    for (size_t i = 0; i < GameActions.size(); ++i) {
        set("controller", GameActions[i].key, RuntimeConfigFile::FormatString(CanonicalBinding(BindingDevice::Gamepad, settings.controls.gamepad[i])));
        set("keyboard", GameActions[i].key, RuntimeConfigFile::FormatString(CanonicalBinding(BindingDevice::Keyboard, settings.controls.keyboard[i])));
    }
    return text;
}
void WriteAtomically(const fs::path& file, const std::string& text) {
    const auto destination = fs::absolute(file);
    fs::create_directories(destination.parent_path());
#ifdef _WIN32
    static std::atomic<uint64_t> sequence{};
    fs::path temporary;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        temporary = destination.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()) +
                    L"." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(sequence++);
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create temporary settings");
    try {
        DWORD written{};
        if (!WriteFile(handle, text.data(), DWORD(text.size()), &written, nullptr) || written != text.size() ||
            !FlushFileBuffers(handle)) throw std::runtime_error("cannot write and flush temporary settings");
        CloseHandle(handle); handle = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot replace settings");
    } catch (...) {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        DeleteFileW(temporary.c_str()); throw;
    }
#else
    std::string temporary = destination.string() + ".tmp.XXXXXX";
    int fd = mkstemp(temporary.data()); // Exclusive creation, mode 0600, same filesystem.
    if (fd < 0) throw std::runtime_error("cannot create temporary settings: " + std::string(std::strerror(errno)));
    try {
        size_t offset = 0;
        while (offset < text.size()) {
            const auto count = write(fd, text.data() + offset, text.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::runtime_error("cannot write temporary settings");
            offset += static_cast<size_t>(count);
        }
        int result;
        do { result = fsync(fd); } while (result < 0 && errno == EINTR);
        if (result < 0) throw std::runtime_error("cannot flush temporary settings");
        result = close(fd); fd = -1;
        if (result < 0) throw std::runtime_error("cannot close temporary settings");
        fs::rename(temporary, destination);
    } catch (...) {
        if (fd >= 0) close(fd);
        std::error_code ec; fs::remove(temporary, ec);
        throw;
    }
#endif
}
} // namespace

Settings LoadSettings(const fs::path& file, Settings defaults) {
    Validate(defaults);
    if (const auto text = Read(file)) return Parse(*text, file, defaults);
    defaults.gameData = fs::absolute(defaults.gameData).lexically_normal();
    return defaults;
}
std::string FormatSettings(const Settings& settings) { return Update("", settings); }
void SaveSettings(const fs::path& file, const Settings& settings) {
    auto text = Read(file).value_or("");
    if (fs::exists(file)) Parse(text, file, {}); // Preserve invalid/future-version files.
    WriteAtomically(file, Update(std::move(text), settings));
}
Settings LoadOrMigrateSettings(const fs::path& file, const fs::path& legacy,
                              const fs::path& legacyWorkingDirectory, Settings defaults) {
    if (fs::exists(file) || !fs::exists(legacy)) return LoadSettings(file, defaults);
    const auto text = Read(legacy);
    std::istringstream input(*text);
    std::string data;
    int sync = 1, performance = 0;
    if (!std::getline(input, data))
        throw std::runtime_error("invalid legacy launcher.cfg; original file preserved");
    if (!data.empty() && data.back() == '\r') data.pop_back();
    if (data.empty() ||
        !(input >> defaults.masterVolume >> sync) || (sync != 0 && sync != 1))
        throw std::runtime_error("invalid legacy launcher.cfg; original file preserved");
    input >> std::ws;
    if (!input.eof() && (!(input >> performance) || (performance != 0 && performance != 1)))
        throw std::runtime_error("invalid legacy performance option; original file preserved");
    input >> std::ws;
    if (!input.eof()) throw std::runtime_error("unrecognized legacy settings; original file preserved");
    defaults.gameData = RuntimeConfigFile::PathFromUtf8(data);
    if (defaults.gameData.is_relative()) defaults.gameData = legacyWorkingDirectory / defaults.gameData;
    defaults.vsync = sync != 0;
    defaults.gpuResidentCopies = performance != 0;
    SaveSettings(file, defaults);
    return LoadSettings(file, defaults);
}
int InspectSettings(const fs::path& file, std::ostream& output, std::ostream& errors) {
    try {
        output << FormatSettings(LoadSettings(file));
        return 0;
    } catch (const std::exception& error) {
        errors << "Settings: " << error.what() << '\n';
        return 2;
    }
}
} // namespace Kirby
