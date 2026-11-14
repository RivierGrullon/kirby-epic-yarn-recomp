// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/app/build_info.h"
#include "KirbyBuildInfo.h"
#include <toml.hpp>
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace Kirby {
std::string BuildInformation(BuildProgram program) {
    return std::string("schema_version = 1\nprogram = \"") +
        (program == BuildProgram::Runtime ? "runtime" : "launcher") + "\"\n" + Generated::BuildInfoDocument;
}
BuildInfo ParseBuildInformation(std::string_view document) {
    if (document.empty() || document.size()>16384 || document.find('\0')!=std::string_view::npos)
        throw std::runtime_error("Build information must be nonempty text of at most 16 KiB");
    std::istringstream input{std::string(document)};
    const auto parsed=toml::parse(input,"build information");
    if (toml::find<int64_t>(parsed,"schema_version")!=1)
        throw std::runtime_error("Unsupported build information version");
    BuildInfo info;
    info.program=toml::find<std::string>(parsed,"program");
    if (info.program!="runtime" && info.program!="launcher")
        throw std::runtime_error("Unknown build information program");
    const auto field=[&](const char* key) { return toml::find<std::string>(parsed,"build",key); };
    info.sourceRevision=field("source_revision"); info.sourceSha256=field("source_sha256");
    info.translatorRevision=field("translator_revision"); info.auroraRevision=field("aurora_revision");
    info.compiler=field("compiler"); info.buildType=field("build_type");
    info.compilerOptions=field("compiler_options"); info.gameId=field("game_id");
    info.dolSha1=field("dol_sha1");
    info.sourceModified=toml::find<bool>(parsed,"build","source_modified");
    if (info.gameId!="RK5E01" || toml::find<int64_t>(parsed,"build","game_revision")!=0 ||
        info.dolSha1!=Generated::SupportedDolSha1)
        throw std::runtime_error("Build information describes an unsupported game revision");
    const auto hex=[](const std::string& value,size_t size) {
        return value.size()==size && std::ranges::all_of(value,[](char c) {
            return (c>='0' && c<='9') || (c>='a' && c<='f'); });
    };
    if (!hex(info.sourceRevision,40) || !hex(info.sourceSha256,64) ||
        !hex(info.translatorRevision,40) || !hex(info.auroraRevision,40))
        throw std::runtime_error("Build information has an invalid source identifier");
    return info;
}
bool SameBuildInputs(const BuildInfo& first,const BuildInfo& second) {
    const auto inputs=[](const BuildInfo& value) {
        return std::tie(value.sourceRevision,value.sourceSha256,value.sourceModified,
            value.translatorRevision,value.auroraRevision,value.compiler,value.buildType,
            value.compilerOptions,value.gameId,value.gameRevision,value.dolSha1);
    };
    return inputs(first)==inputs(second);
}
} // namespace Kirby
