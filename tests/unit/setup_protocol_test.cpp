// SPDX-License-Identifier: GPL-3.0-only
#include "setup/protocol.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void Reject(std::string text) {
    try { Launcher::Setup::ParseEvent(text,"job-1",1); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid setup event accepted");
}
}
int main() {
    using namespace Launcher::Setup;
    const auto progress=ParseEvent(R"({"version":1,"job_id":"job-1","sequence":1,"stage":"hash","state":"progress","completed":128,"total":512})","job-1",1);
    Check(progress.completed==128 && progress.total==512,"Genuine byte progress was lost");
    const auto extraction=ParseEvent(R"({"version":1,"job_id":"job-1","sequence":1,"stage":"extract","state":"started"})","job-1",1);
    Check(!extraction.completed && !extraction.total,"Unknown extraction progress was invented");
    const auto ready=ParseEvent(R"({"version":1,"job_id":"job-1","sequence":1,"stage":"inspect","state":"complete","result":{"source":"/selected's copy.wbfs","source_kind":"wbfs","source_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","cached":false,"data_root":"/setup/export/DATA","game_id":"RK5E01","revision":0,"dol_sha1":"f4280a2b0709bfde4132be22e9f7fa8ecfa2a130","entry_point":2147500112,"asset_files":12,"asset_bytes":1024,"validation":"dol-sha1-and-data-layout"}})","job-1",1);
    Check(ready.result && ready.result->dataRoot=="/setup/export/DATA" && ready.result->assetFiles==12,
          "Successful source identity was not parsed");
    Reject("{}");Reject("garbage");Reject("[]");Reject(std::string(65537,'x'));
    for (const auto field : {"version","sequence","completed","total"}) {
        const std::string valid=R"({"version":1,"job_id":"job-1","sequence":1,"stage":"hash","state":"progress","completed":1,"total":4})";
        for (const auto bad : {"-1","true","1.5","\"1\""}) {
            auto text=valid;const auto start=text.find("\""+std::string(field)+"\":")+std::string(field).size()+3;
            text.replace(start,1,bad);Reject(text);
        }
    }
    Reject(R"({"version":1,"job_id":"other","sequence":1,"stage":"hash","state":"progress"})");
    Reject(R"({"version":1,"job_id":"job-1","sequence":2,"stage":"hash","state":"progress"})");
    Reject(R"({"version":1,"job_id":"job-1","sequence":1,"stage":"hash","state":"progress","completed":5,"total":4})");
    Reject(R"({"version":1,"job_id":"job-1","sequence":1,"stage":"hash","state":"progress","completed":1})");
    Reject(R"({"version":1,"job_id":"job-1","sequence":1,"stage":"inspect","state":"complete"})");
    const auto failure=ParseEvent(R"({"version":1,"job_id":"job-1","sequence":1,"stage":"inspect","state":"failed","error":"Selected source is incomplete"})","job-1",1);
    Check(failure.error=="Selected source is incomplete","Actionable setup error was lost");
    std::cout << "Setup protocol: selected-source identity, strict types, sequencing and byte progress verified\n";
}
