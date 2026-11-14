// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/app/build_info.h"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks;
void Check(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
std::string Replace(std::string text, const std::string& before, const std::string& after) {
    const auto offset=text.find(before);
    Check(offset!=std::string::npos,"Build test input is missing its field");
    text.replace(offset,before.size(),after); return text;
}
void Reject(const std::string& text) {
    try { (void)Kirby::ParseBuildInformation(text); }
    catch (const std::exception&) { ++checks; return; }
    throw std::runtime_error("Malformed build information was accepted");
}
}
int main() {
    using namespace Kirby;
    const auto text=BuildInformation(BuildProgram::Runtime);
    auto runtime=ParseBuildInformation(text);
    auto launcher=ParseBuildInformation(BuildInformation(BuildProgram::Launcher));
    Check(runtime.program=="runtime" && launcher.program=="launcher","Program identities are confused");
    Check(runtime.sourceRevision.size()==40 && runtime.sourceSha256.size()==64,"Source identity is missing");
    Check(runtime.gameId=="RK5E01" && runtime.gameRevision==0,"Game revision is missing");
    Check(SameBuildInputs(runtime,launcher),"Matching inputs were treated as different products");
    for (auto* field : {&launcher.sourceRevision,&launcher.sourceSha256,&launcher.translatorRevision,
                       &launcher.auroraRevision,&launcher.compiler,&launcher.buildType,&launcher.compilerOptions,
                       &launcher.gameId,&launcher.dolSha1}) {
        const auto saved=*field;*field+="changed";
        Check(!SameBuildInputs(runtime,launcher),"A changed build input was not reported");*field=saved;
    }
    launcher.sourceModified=!runtime.sourceModified;
    Check(!SameBuildInputs(runtime,launcher),"Modified source state was ignored");
    launcher.sourceModified=runtime.sourceModified;launcher.gameRevision=1;
    Check(!SameBuildInputs(runtime,launcher),"Changed game revision was ignored");
    Reject(Replace(text,"schema_version = 1","schema_version = 2"));
    Reject(Replace(text,"program = \"runtime\"","program = \"other\""));
    Reject(Replace(text,"game_id = \"RK5E01\"","game_id = \"OTHER\""));
    Reject(Replace(text,"game_revision = 0","game_revision = 1"));
    Reject(Replace(text,runtime.sourceRevision,"short"));
    Reject(Replace(text,runtime.sourceSha256,std::string(64,'z')));
    Reject(Replace(text,runtime.translatorRevision,"bad"));
    Reject(Replace(text,runtime.auroraRevision,"bad"));
    Reject(Replace(text,runtime.dolSha1,"bad"));
    Reject(Replace(text,runtime.dolSha1,std::string(40,'a')));
    Reject(Replace(text,"source_modified = "+std::string(runtime.sourceModified?"true":"false"),
                   "source_modified = \"false\""));
    Reject(text.substr(text.find("[build]")));
    Reject(text+std::string(20000,' '));
    Reject(text+std::string(1,'\0'));
    Reject("[invalid TOML");
    std::cout << checks << " build information checks passed\n";
}
