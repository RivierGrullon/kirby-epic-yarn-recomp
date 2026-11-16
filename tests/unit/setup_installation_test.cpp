// SPDX-License-Identifier: GPL-3.0-only
#include "setup/installation.h"
#include "kirby/platform/command_line.h"
#include "launcher_test_paths.h"
#include "../windows_fixtures.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>


namespace fs=std::filesystem;
using Json=nlohmann::json;
using namespace Launcher::Setup;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    Kirby::CommandLineArguments arguments;arguments.Normalize(argc,argv);
    if (argc==3 && std::string(argv[1])=="--read") {
        try {
            const auto selected=ReadInstallation(Launcher::FromUtf8(argv[2]));
            if (!selected) return 2;
            std::cout<<Json{{"id",selected->identifier},{"executable",Launcher::Utf8(selected->executable)},{"data_root",Launcher::Utf8(selected->dataRoot)}}<<'\n';
            return 0;
        } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
    }
    const auto directory=TemporaryLauncherDirectory("kirby's selected build ");
    struct Cleanup { fs::path path;~Cleanup() { fs::remove_all(path); } } cleanup{directory};
    Check(!ReadInstallation(directory),"Developer build unexpectedly has an installation");
    const auto pointer=fs::path(directory)/"local/user/installation.json";
    fs::create_directories(pointer.parent_path());
    std::ofstream(pointer)<<Json{{"version",1},{"current","../untrusted"},{"previous",nullptr}};
    bool rejected=false;
    try { ReadInstallation(directory); } catch (const std::exception&) { rejected=true; }
    Check(rejected,"Untrusted selection ID accepted");
    fs::remove(pointer);const auto keep=fs::path(directory)/"keep.json";std::ofstream(keep)<<"{}";
    std::error_code symlinkError;fs::create_symlink(keep,pointer,symlinkError);
#ifdef _WIN32
    // Exercise reparse-point rejection even without the symlink privilege.
    // libc++ may preserve the Win32 error instead of mapping permission_denied.
    if (symlinkError==std::errc::permission_denied ||
        symlinkError==std::error_code(ERROR_PRIVILEGE_NOT_HELD,std::system_category())) {
        WindowsTest::FileLink(keep,pointer);
        symlinkError.clear();
    }
#endif
    if (symlinkError) throw std::system_error(symlinkError);
#ifdef _WIN32
    std::error_code statusError;
    const auto linkStatus=fs::symlink_status(pointer,statusError);
    const bool wine=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version")!=nullptr;
    if (wine && linkStatus.type()==fs::file_type::not_found) {
        std::cout<<"SKIP: linked-selection subcase: Wine reported symlink creation success without creating a link. "
                   "Absent and invalid selection checks passed.\n";return 0;
    }
    const auto attributes=GetFileAttributesW(pointer.c_str());
    Check(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT),
          "Windows did not create the linked-selection fixture");
#endif
    rejected=false;
    try { ReadInstallation(directory); } catch (const std::exception&) { rejected=true; }
    Check(rejected && fs::is_regular_file(keep),"Linked selection was accepted or changed");
    std::cout<<"Installed build reader: absent, invalid and linked selections verified\n";
}
