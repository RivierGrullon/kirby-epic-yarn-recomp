// SPDX-License-Identifier: GPL-3.0-only
#include "setup/job.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

namespace fs=std::filesystem;
using Json=nlohmann::json;
using namespace Launcher::Setup;
namespace {
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void Finish(Job& job) {
    const auto start=std::chrono::steady_clock::now();
    while (job.Current().Busy()) {
        const auto tick=std::chrono::steady_clock::now();job.Poll();
        Check(std::chrono::steady_clock::now()-tick<std::chrono::milliseconds(100),"Setup polling blocked the UI");
        Check(std::chrono::steady_clock::now()-start<std::chrono::seconds(5),"Setup worker did not finish");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int Child(const std::string& mode,const fs::path& workspace) {
    std::string line;std::getline(std::cin,line);const auto request=Json::parse(line);
    uint64_t sequence=0;
    const auto emit=[&](Json event) {
        event["version"]=1;event["job_id"]=request.at("id");event["sequence"]=++sequence;
        std::cout << event.dump() << '\n' << std::flush;
    };
    emit({{"stage","inspect"},{"state","started"},{"log_path","/tmp/source.log"}});
    if (mode.starts_with("install-")) {
        Check(request.at("operation")=="install","Install client sent the wrong operation");
        if (mode=="install-late-cancel") {
            std::getline(std::cin,line);
            Check(Json::parse(line).at("operation")=="cancel","Late cancellation was not sent");
        }
        emit({{"stage","compile"},{"state","started"}});
        const std::string id(64,'a');
        Json result={{"source",request.at("source")},{"data_root","/tmp/export/DATA"},
          {"installation_id",id},{"committed",true},{"build_reused",false},{"translation_reused",true},
          {"executable",(mode=="install-wrong-path" ? fs::path("/different") : workspace/"installations"/id/"WiiCompiled").string()}};
        emit({{"stage","install"},{"state","complete"},{"result",result}});
        return mode=="install-nonzero" ? 7 : 0;
    }
    if (mode=="hang") { std::this_thread::sleep_for(std::chrono::seconds(10));return 0; }
    if (mode=="cancel") {
        std::getline(std::cin,line);
        Check(Json::parse(line).at("operation")=="cancel","Client did not send cancellation");
        emit({{"stage","inspect"},{"state","cancelled"},{"error","Inspection cancelled"}});return 2;
    }
    if (mode=="bad-json") { std::cout << "not JSON\n" << std::flush;return 0; }
    if (mode=="overflow") { std::cout << std::string(70000,'x') << std::flush;return 0; }
    if (mode=="failure") { emit({{"stage","inspect"},{"state","failed"},{"error","Missing files"}});return 1; }
    if (mode=="missing-final") return 0;
    emit({{"stage","hash"},{"state","progress"},{"completed",128},{"total",512}});
    Json result={{"source",mode=="wrong-source" ? "/different.wbfs" : request.at("source").get<std::string>()},
                 {"source_kind","directory"},{"data_root","/tmp/export/DATA"},{"game_id","RK5E01"},
                 {"revision",0},{"dol_sha1","f4280a2b0709bfde4132be22e9f7fa8ecfa2a130"},
                 {"entry_point",2147500112u},{"asset_files",12},{"asset_bytes",1024},
                 {"validation","dol-sha1-and-data-layout"}};
    emit({{"stage","inspect"},{"state","complete"},{"result",result}});
    if (mode=="late-event") emit({{"stage","hash"},{"state","complete"}});
    if (mode=="delayed") std::this_thread::sleep_for(std::chrono::milliseconds(150));
    return mode=="nonzero" ? 7 : 0;
}
}
int main(int argc,char** argv) {
    if (argc==4 && std::string(argv[2])=="--workspace") return Child(fs::path(argv[1]).filename().string(),argv[3]);
    std::string directory=(fs::temp_directory_path()/"kirby's setup client.XXXXXX").string();
    Check(mkdtemp(directory.data())!=nullptr,"Could not create test directory");
    struct Cleanup { fs::path path;~Cleanup() { fs::remove_all(path); } } cleanup{directory};
    const auto self=fs::canonical(argv[0]).string();
    const auto source=fs::path(directory)/"selected copy's folder";fs::create_directory(source);
    const auto fixture=[&](const std::string& mode) { const auto path=fs::path(directory)/mode;std::ofstream(path).put('\n');return path; };
    Job job(100);
    Check(job.Start(fixture("delayed"),source,fs::path(directory)/"workspace",self),"Could not start setup worker");
    Check(!job.Start(fixture("failure"),source,directory,self),"Concurrent setup worker was started");
    const auto before=std::chrono::steady_clock::now();
    Finish(job);
    Check(job.Current().status==Status::Ready && job.Current().result.has_value(),"Successful inspection was not accepted");
    Check(std::chrono::steady_clock::now()-before>=std::chrono::milliseconds(140),"Result accepted before successful process exit");
    Check(job.Current().result->source==source,"Client substituted a different selected source");
    const auto savedInput=dup(STDIN_FILENO);Check(savedInput>=0,"Could not preserve test stdin");
    close(STDIN_FILENO);
    const bool startedWithoutInput=job.Start(fixture("valid"),source,directory,self);
    dup2(savedInput,STDIN_FILENO);close(savedInput);
    Check(startedWithoutInput,"Launcher with closed stdin could not start setup");Finish(job);
    Check(job.Current().status==Status::Ready,"Closed parent stdin broke the worker control channel");
    for (const auto mode : {"bad-json","overflow","failure","missing-final","nonzero","wrong-source","late-event"}) {
        Check(job.Start(fixture(mode),source,directory,self),"Failure fixture did not start");Finish(job);
        Check(job.Current().status==Status::Failed && !job.Current().result && !job.Current().message.empty(),
              "Invalid setup worker result was accepted");
    }
    for (const auto mode : {"cancel","hang"}) {
        Check(job.Start(fixture(mode),source,directory,self),"Cancellation fixture did not start");
        job.Cancel();Finish(job);
        Check(job.Current().status==Status::Cancelled && !job.Current().result,"Cancelled inspection changed the selection");
    }
    for (const auto mode : {"install-ready","install-late-cancel","install-wrong-path","install-nonzero"}) {
        Check(job.Start(fixture(mode),source,directory,self,Operation::Install),"Install fixture did not start");
        if (std::string(mode)=="install-late-cancel") job.Cancel();
        Finish(job);
        const bool success=std::string(mode)=="install-ready" || std::string(mode)=="install-late-cancel";
        Check(job.Current().status==(success ? Status::Ready : Status::Failed),"Install completion/exit/cancellation was misclassified");
        Check(job.Current().installation.has_value()==success,"Unverified installation result was accepted");
    }
    { Job owned(100);Check(owned.Start(fixture("hang"),source,directory,self),"Destructor fixture did not start"); }
    Check(!job.Start(fixture("failure"),source,directory,"/missing/python"),"Missing Python was accepted");
    int status=0;errno=0;
    Check(waitpid(-1,&status,WNOHANG)==-1 && errno==ECHILD,"Setup client leaked a child or zombie");
    std::cout << "Setup client: asynchronous progress, actual source, exit validation, cancellation and cleanup verified\n";
}
