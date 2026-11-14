// SPDX-License-Identifier: GPL-3.0-only
#include "protocol.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>

namespace Launcher::Setup {
namespace {
using Json=nlohmann::json;
std::string Text(const Json& object,const char* key,size_t limit=8192) {
    const auto& item=object.at(key);
    if (!item.is_string()) throw std::runtime_error("Setup event has a non-string field");
    const auto value=item.get<std::string>();
    if (value.empty() || value.size()>limit || value.find('\0')!=std::string::npos)
        throw std::runtime_error("Setup event has an invalid string field");
    return value;
}
uint64_t Number(const Json& object,const char* key) {
    const auto& item=object.at(key);
    if (!item.is_number_unsigned()) throw std::runtime_error("Setup event requires a nonnegative integer");
    return item.get<uint64_t>();
}
std::filesystem::path Path(const Json& object,const char* key) {
    const std::filesystem::path value=Text(object,key);
    if (!value.is_absolute()) throw std::runtime_error("Setup event requires an absolute path");
    return value;
}
bool Boolean(const Json& object,const char* key) {
    if (!object.at(key).is_boolean()) throw std::runtime_error("Setup event requires a boolean");
    return object.at(key).get<bool>();
}
}
Event ParseEvent(std::string_view line,std::string_view identifier,uint64_t sequence) {
    if (line.empty() || line.size()>65536 || line.find('\0')!=std::string_view::npos)
        throw std::runtime_error("Setup event is empty or exceeds 64 KiB");
    const auto object=Json::parse(line,[](int depth,Json::parse_event_t,Json&) {
        if (depth>16) throw std::runtime_error("Setup event is too deeply nested");
        return true;
    });
    if (!object.is_object() || Number(object,"version")!=1 || Text(object,"job_id",80)!=identifier ||
        Number(object,"sequence")!=sequence) throw std::runtime_error("Setup event identity or sequence mismatch");
    Event event;event.stage=Text(object,"stage",32);event.state=Text(object,"state",32);
    const auto oneOf=[](std::string_view value,std::initializer_list<std::string_view> items) {
        return std::find(items.begin(),items.end(),value)!=items.end();
    };
    if (!oneOf(event.stage,{"inspect","hash","unpack","extract","validate","request",
                           "install","prepare","translate","compile","verify","publish","select"}) ||
        !oneOf(event.state,{"started","progress","complete","failed","cancelled","rejected"}))
        throw std::runtime_error("Unknown setup stage or state");
    if (object.contains("completed") || object.contains("total")) {
        event.completed=Number(object,"completed");event.total=Number(object,"total");
        if (*event.completed>*event.total) throw std::runtime_error("Setup byte progress exceeds its total");
    }
    if (object.contains("log_path")) event.logPath=Path(object,"log_path");
    if (object.contains("error")) event.error=Text(object,"error",8192);
    if (event.stage=="inspect" && event.state=="complete") {
        const auto& result=object.at("result");
        if (Text(result,"game_id")!="RK5E01" || Number(result,"revision")!=0 ||
            Text(result,"dol_sha1")!="f4280a2b0709bfde4132be22e9f7fa8ecfa2a130" ||
            Number(result,"entry_point")!=0x80004050 || Text(result,"validation")!="dol-sha1-and-data-layout")
            throw std::runtime_error("Setup result does not identify RK5E01 Rev 0");
        Inspection info;info.source=Path(result,"source");info.dataRoot=Path(result,"data_root");
        info.sourceKind=Text(result,"source_kind",16);
        if (!oneOf(info.sourceKind,{"directory","7z","wbfs","iso","rvz"}))
            throw std::runtime_error("Unknown setup source kind");
        info.assetFiles=Number(result,"asset_files");info.assetBytes=Number(result,"asset_bytes");
        if (info.sourceKind!="directory") {
            const auto digest=Text(result,"source_sha256",64);
            if (digest.size()!=64 || digest.find_first_not_of("0123456789abcdef")!=std::string::npos ||
                !result.at("cached").is_boolean()) throw std::runtime_error("Invalid setup source hash or reuse flag");
            info.cached=result.at("cached").get<bool>();
        }
        event.result=std::move(info);
    } else if (event.stage=="install" && event.state=="complete") {
        const auto& result=object.at("result");
        Installation info;info.identifier=Text(result,"installation_id",64);
        if (info.identifier.size()!=64 || info.identifier.find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("Invalid native installation identifier");
        info.source=Path(result,"source");info.dataRoot=Path(result,"data_root");info.executable=Path(result,"executable");
        info.committed=Boolean(result,"committed");info.buildReused=Boolean(result,"build_reused");
        info.translationReused=Boolean(result,"translation_reused");
        if (!info.committed) throw std::runtime_error("Native installation was not committed");
        event.installation=std::move(info);
    } else if (object.contains("result")) throw std::runtime_error("Setup result arrived before completion");
    if ((event.stage=="inspect" || event.stage=="install") && (event.state=="failed" || event.state=="cancelled") && event.error.empty())
        throw std::runtime_error("Setup failure has no explanation");
    return event;
}
std::string InspectRequest(std::string_view identifier,const std::filesystem::path& source) {
    return Json{{"version",1},{"id",identifier},{"operation","inspect"},{"source",source.string()}}.dump()+"\n";
}
std::string InstallRequest(std::string_view identifier,const std::filesystem::path& source) {
    return Json{{"version",1},{"id",identifier},{"operation","install"},{"source",source.string()}}.dump()+"\n";
}
std::string CancelRequest(std::string_view identifier) {
    return Json{{"version",1},{"id",identifier},{"operation","cancel"}}.dump()+"\n";
}
} // namespace Launcher::Setup
