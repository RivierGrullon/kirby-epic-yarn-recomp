// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/platform/command_line.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
namespace {
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main(int argc,char** argv) {
    const auto originalArgv=argv;
    Kirby::CommandLineArguments arguments;arguments.Normalize(argc,argv);
    Check(argv[argc]==nullptr,"Normalized command line lost its null terminator");
#ifdef _WIN32
    (void)originalArgv;
    if (argc==5 && std::string_view(argv[1])=="--read") {
        const auto config=Kirby::CommandLinePath(argv[2]);
        const auto data=Kirby::CommandLinePath(argv[3]);
        const auto log=Kirby::CommandLinePath(argv[4]);
        Check(Kirby::CommandLinePath(argv[0]).filename()==fs::path(u8"ユニコード test.exe"),"Unicode executable argv[0] was lost");
        Check(config.filename()==fs::path(u8"設定 'quoted' file.toml"),"Unicode config argument was lost");
        Check(data==config.parent_path()/fs::path(u8"資料 DATA"),"Unicode DATA argument was lost");
        Check(log==data/fs::path(u8"記録 log.csv"),"Unicode frame-log argument was lost");
        std::ifstream input(config);std::string text;std::getline(input,text);
        Check(text=="unicode-path-fixture" && fs::is_directory(data),"Unicode CLI paths cannot be opened");
        std::ofstream output(log);output<<"opened through UTF-8 arguments";output.close();
        Check(bool(output),"Cannot write Unicode frame-log path");return 0;
    }
    auto directoryName=fs::path(u8"Kirby 設定 ");
    directoryName+=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory=fs::temp_directory_path()/directoryName;
    Check(fs::create_directories(directory),"Cannot create Unicode CLI fixture");
    struct Cleanup { fs::path directory;~Cleanup() { std::error_code error;fs::remove_all(directory,error); } } cleanup{directory};
    const auto self=fs::canonical(Kirby::CommandLinePath(argv[0]));
    const auto executable=directory/fs::path(u8"ユニコード test.exe");
    fs::copy_file(self,executable);
    for (const auto& file : fs::directory_iterator(self.parent_path()))
        if (file.path().extension()==L".dll") fs::copy_file(file.path(),directory/file.path().filename(),fs::copy_options::skip_existing);
    const auto config=directory/fs::path(u8"設定 'quoted' file.toml");
    const auto data=directory/fs::path(u8"資料 DATA");
    const auto log=data/fs::path(u8"記録 log.csv");
    fs::create_directory(data);std::ofstream(config)<<"unicode-path-fixture\n";
    auto command=L"\""+executable.wstring()+L"\" --read \""+config.wstring()+L"\" \""+data.wstring()+L"\" \""+log.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    Check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                         nullptr,nullptr,&startup,&child),"Cannot launch Unicode CLI fixture");
    CloseHandle(child.hThread);
    const auto wait=WaitForSingleObject(child.hProcess,10000);
    if (wait!=WAIT_OBJECT_0) { TerminateProcess(child.hProcess,1);WaitForSingleObject(child.hProcess,1000); }
    DWORD code=1;GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hProcess);
    Check(wait==WAIT_OBJECT_0 && code==0,"Unicode CLI child rejected its paths");
    Check(fs::is_regular_file(log),"Unicode frame-log was not written");
#else
    Check(argv==originalArgv,"POSIX argv was unexpectedly replaced");
    Check(Kirby::CommandLinePath("DATA with spaces")==fs::path("DATA with spaces"),"POSIX CLI path changed");
#endif
    std::cout<<"Command line: native Unicode paths and argument ownership verified\n";
}
