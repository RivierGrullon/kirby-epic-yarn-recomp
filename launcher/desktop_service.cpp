// SPDX-License-Identifier: GPL-3.0-only
#include "desktop_service.h"
#include "build_probe.h"
#include "language.h"
#include "platform.h"
#include "setup/installation.h"
#include "setup/handoff.h"
#include "save_manager.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include <memory>

namespace Launcher {
namespace {
using Json = nlohmann::json;

Json Config(const Kirby::Settings& c) {
    const auto& m = c.motion;
    return {{"volume",c.masterVolume},{"scale",c.resolutionScale},{"aa",(c.msaaSamples==4?2:0)+int(c.fxaa)},
        {"gameLanguage",c.gameLanguage.value_or(Kirby::Settings::English)},{"fullscreen",c.fullscreen},{"vsync",c.vsync},{"performance",c.gpuResidentCopies},
        {"stick",c.controls.stickActivation},{"trigger",c.controls.triggerActivation},
        {"tilt",m.tilt},{"invertTilt",m.invertTilt},{"gain",m.tiltGain},{"pointer",int(m.pointer)},
        {"sensitivity",m.sensitivity},{"invertX",m.invertX},{"invertY",m.invertY},
        {"deadzone",m.deadzone},{"stickDeadzone",m.stickDeadzone},{"keyboardMotion",m.keyboard}};
}

void Change(Kirby::Settings& c, const std::string& key, const Json& value) {
    auto& m = c.motion;
    if (key=="volume") value.get_to(c.masterVolume);
    else if (key=="gameLanguage") c.gameLanguage = value.get<int>();
    else if (key=="scale") value.get_to(c.resolutionScale);
    else if (key=="aa") {
        const int aa=value.get<int>();
        if (aa<0 || aa>3) throw std::runtime_error("Invalid anti-aliasing mode");
        c.msaaSamples=aa>=2?4:1; c.fxaa=(aa&1)!=0;
    } else if (key=="fullscreen") value.get_to(c.fullscreen);
    else if (key=="vsync") value.get_to(c.vsync);
    else if (key=="performance") value.get_to(c.gpuResidentCopies);
    else if (key=="stick") value.get_to(c.controls.stickActivation);
    else if (key=="trigger") value.get_to(c.controls.triggerActivation);
    else if (key=="tilt") value.get_to(m.tilt);
    else if (key=="invertTilt") value.get_to(m.invertTilt);
    else if (key=="gain") value.get_to(m.tiltGain);
    else if (key=="pointer") {
        const int mode=value.get<int>();
        if (mode<0 || mode>3) throw std::runtime_error("Invalid pointer source");
        m.pointer=static_cast<Kirby::PointerSource>(mode);
    } else if (key=="sensitivity") value.get_to(m.sensitivity);
    else if (key=="invertX") value.get_to(m.invertX);
    else if (key=="invertY") value.get_to(m.invertY);
    else if (key=="deadzone") value.get_to(m.deadzone);
    else if (key=="stickDeadzone") value.get_to(m.stickDeadzone);
    else if (key=="keyboardMotion") value.get_to(m.keyboard);
    else throw std::runtime_error("Unknown setting");
    (void)Kirby::FormatSettings(c); // Validate using the same schema as the game.
}

Json Build(const Kirby::BuildInfo& b) {
    return {{"revision",b.sourceRevision},{"fingerprint",b.sourceSha256},{"compiler",b.compiler},
        {"type",b.buildType},{"options",b.compilerOptions},{"aurora",b.auroraRevision},
        {"translator",b.translatorRevision},{"game",b.gameId},{"dol",b.dolSha1},{"modified",b.sourceModified}};
}

struct Input {
    using Pad = std::unique_ptr<SDL_Gamepad, decltype(&SDL_CloseGamepad)>;
    std::map<SDL_JoystickID,Pad> pads;
    SDL_JoystickID selected = 0;
    Json Tick(State& s, bool focused, bool controls) {
        auto& capture=s.bindingCapture;
        auto& p=s.motionPreview;
        Json navigation=Json::array();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type==SDL_EVENT_GAMEPAD_ADDED) {
                if (auto* pad=SDL_OpenGamepad(e.gdevice.which))
                    pads.insert_or_assign(e.gdevice.which,Pad(pad,SDL_CloseGamepad));
            }
            if (e.type==SDL_EVENT_GAMEPAD_REMOVED) pads.erase(e.gdevice.which);
            if (!focused) continue;
            if (capture.Busy() && capture.Handle(e,s.config.controls,s.settingsDirty)) continue;
            if (!capture.Busy() && e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                const char* name=SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(e.gbutton.button));
                if (name) navigation.push_back(name);
            }
        }
        capture.Tick(SDL_GetTicks(),focused && controls);
        Json devices=Json::array();
        for (const auto& [id,pad] : pads) {
            const auto* name=SDL_GetGamepadName(pad.get());
            devices.push_back({{"id",id},{"name",name?name:"Unnamed controller"}});
        }
        if (!pads.contains(selected)) selected=pads.empty()?0:pads.begin()->first;
        if (!focused || !controls || !selected || capture.Busy()) { if (p.pad) p.Close(); }
        else {
            if (!p.pad || SDL_GetGamepadID(p.pad)!=selected) p.Select(selected);
            const auto now=SDL_GetTicksNS();
            const float dt=p.lastTick?float(now-p.lastTick)/1.e9f:0;
            p.lastTick=now;
            const bool before=p.device.state.calibrating;
            p.device.Update(p.pad,s.config.motion,dt,true);
            if (before && !p.device.state.calibrating && p.device.state.calibrated) {
                s.config.motion.calibrationDevice=p.device.identity; s.config.motion.bias=p.device.state.bias;
                s.settingsDirty=true; p.message="Calibration complete. Save settings to use it in game.";
            }
        }
        const auto tilt=p.device.Tilt(s.config.motion);
        return {{"devices",devices},{"selected",selected},{"nav",navigation},{"phase",int(capture.phase)},
            {"release",capture.WaitingForRelease()},{"keyboardCapture",capture.device==Kirby::BindingDevice::Keyboard},
            {"message",capture.message},{"gyro",p.device.gyroRead},
            {"accel",p.device.accelRead},{"x",p.device.state.x},{"y",p.device.state.y},
            {"tiltAngle",std::atan2(-tilt[2],-tilt[1])*57.29578},{"calibrating",p.device.state.calibrating},
            {"motionMessage",p.message}};
    }
};
}

int DesktopService(State& s,
    const std::function<bool(const std::filesystem::path&,std::string&)>& verify,
    const std::function<bool()>& startGame,const std::function<std::optional<int>()>& pollGame) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    SDL_Init(SDL_INIT_GAMEPAD);
    Input input;
    BuildProbe probe;
    const auto refresh=[&] { s.runtimeBuild.reset(); probe.Cancel(); probe.Start(s.executable,s.root/"local/host-libs"); };
    const auto save=[&] {
        if (!s.settingsError.empty()) throw std::runtime_error(s.settingsError);
        Kirby::SaveSettings(s.settingsFile,s.config); s.settingsDirty=false;
    };
    s.dataVerified=verify(s.config.gameData,s.dataStatus);
    refresh();
    const auto saves=s.root/"local/user/nand/title/00010000/524b3545/data";
    std::string line;
    Json previous;
    while (std::getline(std::cin,line)) {
        Json request,reply;
        std::string command;
        try {
            request=Json::parse(line);
            command=request.at("cmd").get<std::string>();
            if (command=="quit") {
                if(request.value("discard",false))s.settingsDirty=false;
                break;
            }
            if (s.gameRunning) if (const auto code=pollGame()) {
                s.gameRunning=false;
                s.gameStatus=*code==0?"Game closed":"Game stopped with an error. Open Logs for details."; refresh();
            }
            probe.Poll();
            if (probe.Status()==BuildProbeStatus::Ready) s.runtimeBuild=probe.Information();
            if (command=="play") {
                if (s.gameRunning) throw std::runtime_error("Game running");
                if (!s.installationError.empty()) throw std::runtime_error(s.installationError);
                const auto selected=Setup::ReadInstallation(s.root);
                if ((selected?selected->identifier:"")!=s.installationId)
                    throw std::runtime_error("Installation selection changed. Restart the launcher before playing.");
                s.dataVerified=verify(s.config.gameData,s.dataStatus);
                if (!s.dataVerified) throw std::runtime_error(s.dataStatus);
                save(); probe.Cancel();
                if (!startGame()) throw std::runtime_error(SDL_GetError());
                s.gameRunning=true; s.gameStatus="Game running";
            } else if (command=="change") {
                if (!s.settingsError.empty()) throw std::runtime_error(s.settingsError);
                auto next=s.config; Change(next,request.at("key"),request.at("value"));
                s.config=std::move(next); s.settingsDirty=true;
            } else if (command=="save") save();
            else if (command=="reload") {
                auto defaults=Kirby::Settings{s.root/"disc/DATA"};
                if (const auto selected=Setup::ReadInstallation(s.root)) defaults.gameData=selected->dataRoot;
                s.config=Kirby::LoadOrMigrateSettings(s.settingsFile,s.root/"local/user/launcher.cfg",s.root,defaults);
                s.settingsError.clear(); s.settingsDirty=false; s.dataVerified=verify(s.config.gameData,s.dataStatus);
            } else if (command=="data" || command=="verify") {
                if (s.gameRunning) throw std::runtime_error("Game running");
                if (command=="data") {
                    auto data=FromUtf8(request.at("path").get<std::string>());
                    std::string status;
                    if (!verify(data,status)) throw std::runtime_error(status);
                    if (!s.settingsError.empty()) throw std::runtime_error(s.settingsError);
                    auto next=s.config; next.gameData=data; Kirby::SaveSettings(s.settingsFile,next);
                    s.config=std::move(next); s.settingsDirty=false;
                }
                s.dataVerified=verify(s.config.gameData,s.dataStatus);
            } else if (command=="importSave") {
                if (s.gameRunning) throw std::runtime_error("Game running");
                s.saveImportStatus=ImportSave(s.root,FromUtf8(request.at("path").get<std::string>()));
            } else if (command=="exportSave") {
                if (s.gameRunning) throw std::runtime_error("Game running");
                const auto path=FromUtf8(request.at("path").get<std::string>());
                s.saveImportStatus = request.value("folder",false) ? ExportSaveFolder(s.root,path)
                                                                   : ExportSaveBin(s.root,path);
            } else if (command=="restoreBackup") {
                if (s.gameRunning) throw std::runtime_error("Game running");
                s.saveImportStatus=RestoreBackup(s.root,request.at("name").get<std::string>());
            } else if (command=="deleteBackup") {
                if (s.gameRunning) throw std::runtime_error("Game running");
                s.saveImportStatus=DeleteBackup(s.root,request.at("name").get<std::string>());
            } else if (command=="setup") {
                if (s.gameRunning) throw std::runtime_error("Game running");
                const auto args=Setup::ReadSetupCommand(s.root);
                std::vector<const char*> argv;
                for (auto& arg:args) argv.push_back(arg.c_str());
                argv.push_back(nullptr);
                auto* process=SDL_CreateProcess(argv.data(),false);
                if (!process) throw std::runtime_error(SDL_GetError());
                SDL_DestroyProcess(process);
            } else if (command=="refresh") refresh();
            else if (command=="lang") {
                const int value=request.at("value");
                if (value<0 || value>2) throw std::runtime_error("Invalid language");
                SetLanguage(static_cast<Language>(value));
                if (!WriteLanguage(s.settingsFile.parent_path(),CurrentLanguage()))
                    throw std::runtime_error("Could not save the language preference.");
            } else if (command=="bind" || command=="clear") {
                const auto action=request.at("action").get<size_t>();
                if (action>=Kirby::GameActions.size()) throw std::runtime_error("Unknown action");
                const auto device=request.value("keyboard",false)?Kirby::BindingDevice::Keyboard:Kirby::BindingDevice::Gamepad;
                if (command=="bind") s.bindingCapture.Begin(action,device,SDL_GetTicks());
                else s.bindingCapture.Clear(s.config.controls,action,device,s.settingsDirty);
            } else if (command=="key") {
                SDL_Event e{}; e.type=request.value("down",true)?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
                e.key.scancode=SDL_GetScancodeFromName(request.at("key").get<std::string>().c_str());
                s.bindingCapture.Handle(e,s.config.controls,s.settingsDirty);
            } else if (command=="cancel") s.bindingCapture.Cancel();
            else if (command=="swap") s.bindingCapture.Swap(s.config.controls,s.settingsDirty);
            else if (command=="escape") s.bindingCapture.BindEscape(s.config.controls,s.settingsDirty);
            else if (command=="resetControls") { s.config.controls={}; s.settingsDirty=true; }
            else if (command=="pad") input.selected=request.at("id");
            else if (command=="recenter") s.motionPreview.device.state.Recenter();
            else if (command=="calibrate" && s.motionPreview.device.gyroRead && s.motionPreview.device.accelRead)
                s.motionPreview.device.state.Calibrate();
            else if (command=="resetMotion") { s.config.motion={}; s.motionPreview.Close(); s.settingsDirty=true; }
            const auto live=input.Tick(s,request.value("focused",false),request.value("controls",false));
            Json bindings=Json::array();
            for (size_t i=0;i<Kirby::GameActions.size();++i) {
                bindings.push_back({Kirby::GameActions[i].label,Kirby::GamepadBindingLabel(s.config.controls.gamepad[i]),
                    s.config.controls.keyboard[i]=="disabled"?"Unmapped":s.config.controls.keyboard[i]});
            }
            Json backups=Json::array();
            for (const auto& backup : ListBackups(s.root))
                backups.push_back({{"name",backup.name},{"timestamp",backup.timestamp}});
            Json state={{"config",Config(s.config)},{"bindings",bindings},{"lang",int(CurrentLanguage())},
                {"dirty",s.settingsDirty},{"running",s.gameRunning},{"gameStatus",s.gameStatus},
                {"verified",s.dataVerified},{"dataStatus",s.dataStatus},{"settingsError",s.settingsError},
                {"installationError",s.installationError},{"installed",std::filesystem::is_regular_file(s.executable)},
                {"data",Utf8(s.config.gameData)},{"settings",Utf8(s.settingsFile)},{"executable",Utf8(s.executable)},
                {"saves",Utf8(saves)},{"saveFound",std::filesystem::is_regular_file(saves/"FLF.bin")},
                {"saveImportStatus",s.saveImportStatus},{"backups",std::move(backups)},
                {"setupAvailable",std::filesystem::is_regular_file(s.root/"local/setup-command.json")},
                {"build",Build(s.launcherBuild)},{"runtime",s.runtimeBuild?Build(*s.runtimeBuild):Json(nullptr)},
                {"match",s.runtimeBuild && Kirby::SameBuildInputs(s.launcherBuild,*s.runtimeBuild)},
                {"buildError",probe.Error()},{"checking",probe.Status()==BuildProbeStatus::Checking}};
            if (state!=previous || command!="poll") reply["state"]=state;
            previous=std::move(state);
            reply["input"]=live;
        } catch (const std::exception& e) { reply["error"]=e.what(); }
        reply["cmd"]=command;
        std::cout<<reply.dump(-1,' ',false,Json::error_handler_t::replace)<<'\n'<<std::flush;
    }
    s.motionPreview.Close();
    if (s.settingsDirty && s.settingsError.empty()) {
        try { save(); } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    }
    return 0;
}
}
