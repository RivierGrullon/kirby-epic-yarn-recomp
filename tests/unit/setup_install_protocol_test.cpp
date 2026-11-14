// SPDX-License-Identifier: GPL-3.0-only
#include "setup/protocol.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>

using namespace Launcher::Setup;
using Json=nlohmann::json;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    Json object={{"version",1},{"job_id","install-1"},{"sequence",1},{"stage","install"},{"state","complete"},
      {"result",{{"source","/selected's copy.wbfs"},{"data_root","/export/DATA"},
        {"installation_id",std::string(64,'a')},{"executable","/setup/installations/"+std::string(64,'a')+"/WiiCompiled"},
        {"committed",true},{"build_reused",false},{"translation_reused",true}}}};
    const auto event=ParseEvent(object.dump(),"install-1",1);
    Check(event.installation && event.installation->committed,"Committed installation was lost");
    Check(event.installation->source=="/selected's copy.wbfs","Selected source identity was lost");
    Check(!event.installation->buildReused && event.installation->translationReused,"Cache outcomes were lost");
    for (const auto field : {"committed","installation_id","executable","source","data_root"}) {
        auto bad=object;
        if (std::string(field)=="committed") bad["result"][field]=false;
        else bad["result"][field]="../untrusted";
        bool rejected=false;
        try { ParseEvent(bad.dump(),"install-1",1); } catch (const std::exception&) { rejected=true; }
        Check(rejected,"Invalid installation completion was accepted");
    }
    for (const auto stage : {"prepare","translate","compile","verify","publish","select"}) {
        const auto progress=ParseEvent(Json{{"version",1},{"job_id","install-1"},{"sequence",1},
          {"stage",stage},{"state","started"}}.dump(),"install-1",1);
        Check(progress.stage==stage,"Build stage was rejected");
    }
    const auto request=Json::parse(InstallRequest("install-1","/selected's copy.wbfs"));
    Check(request.at("operation")=="install" && request.at("source")=="/selected's copy.wbfs","Install request changed its source");
    std::cout << "Install protocol: committed identity, literal source, cache reuse and build stages verified\n";
}
