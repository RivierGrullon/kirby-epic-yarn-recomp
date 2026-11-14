// SPDX-License-Identifier: GPL-3.0-only
#include "setup/installation.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace fs=std::filesystem;
using Json=nlohmann::json;
using namespace Launcher::Setup;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    if (argc==3 && std::string(argv[1])=="--read") {
        try {
            const auto selected=ReadInstallation(argv[2]);
            if (!selected) return 2;
            std::cout<<Json{{"id",selected->identifier},{"executable",selected->executable.string()},{"data_root",selected->dataRoot.string()}}<<'\n';
            return 0;
        } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
    }
    std::string directory=(fs::temp_directory_path()/"kirby's selected build.XXXXXX").string();
    Check(mkdtemp(directory.data())!=nullptr,"Cannot create fixture");
    struct Cleanup { fs::path path;~Cleanup() { fs::remove_all(path); } } cleanup{directory};
    Check(!ReadInstallation(directory),"Developer build unexpectedly has an installation");
    const auto pointer=fs::path(directory)/"local/user/installation.json";
    fs::create_directories(pointer.parent_path());
    std::ofstream(pointer)<<Json{{"version",1},{"current","../untrusted"},{"previous",nullptr}};
    bool rejected=false;
    try { ReadInstallation(directory); } catch (const std::exception&) { rejected=true; }
    Check(rejected,"Untrusted selection ID accepted");
    fs::remove(pointer);const auto keep=fs::path(directory)/"keep.json";std::ofstream(keep)<<"{}";
    fs::create_symlink(keep,pointer);rejected=false;
    try { ReadInstallation(directory); } catch (const std::exception&) { rejected=true; }
    Check(rejected && fs::is_regular_file(keep),"Linked selection was accepted or changed");
    std::cout<<"Installed build reader: absent, invalid and linked selections verified\n";
}
