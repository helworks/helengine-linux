#include "linux_run_options.hpp"
#include <charconv>
#include <algorithm>
#include <array>
#include <climits>
#include <filesystem>
#include <stdexcept>

namespace helengine::linux {
bool LinuxRunOptions::TagsEqual(const std::string& left,const std::string& right) {
    return left.size()==right.size() && std::equal(left.begin(),left.end(),right.begin(),[](unsigned char first,unsigned char second) { return (first>='A' && first<='Z'?first+('a'-'A'):first)==(second>='A' && second<='Z'?second+('a'-'A'):second); });
}

LinuxRunOptions::LinuxRunOptions(int argc,char** argv) {
    for (int index=1;index<argc;++index) {
        const std::string argument=argv[index];
        if (argument=="--offscreen") { Offscreen=true; }
        else if (argument=="--noninteractive") { Noninteractive=true; }
        else if (argument=="--validation") { Validation=true; }
        else if (argument=="--require-tracer-progress") { RequireTracerProgress=true; }
        else if (argument=="--require-shadow-reception") { RequireShadowReception=true; }
        else if (argument=="--content" || argument=="--scene" || argument=="--frames" || argument=="--input-script" || argument=="--output" || argument=="--session" || argument=="--parent-pid") {
            if (++index>=argc) { throw std::invalid_argument("Missing value for Linux player flag: " + argument); }
            const std::string value=argv[index];
            if (argument=="--content") { ContentRoot=std::filesystem::absolute(value).string(); }
            else if (argument=="--session") { if (value.size()!=32) { throw std::invalid_argument("Session requires exactly 32 lowercase hexadecimal characters."); } Session=value; }
            else if (argument=="--parent-pid") { auto result=std::from_chars(value.data(),value.data()+value.size(),ParentPid); if (result.ec!=std::errc() || result.ptr!=value.data()+value.size() || !ParentPid || ParentPid>INT_MAX) { throw std::invalid_argument("--parent-pid requires a positive Linux PID."); } }
            else if (argument=="--output") {
                std::array<std::string,4> fields; size_t start=0;
                for (size_t field=0;field<4;++field) { const size_t end=value.find(',',start); if ((field<3 && end==std::string::npos) || (field==3 && end!=std::string::npos)) { throw std::invalid_argument("--output requires TAG,WIDTH,HEIGHT,opaque|transparent."); } fields[field]=value.substr(start,end==std::string::npos?end:end-start); start=end+1; }
                LinuxOutputOptions output; output.Tag=fields[0];
                if (output.Tag.empty() || output.Tag.size()>32 || !std::all_of(output.Tag.begin(),output.Tag.end(),[](unsigned char c) { return (c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='-'; })) { throw std::invalid_argument("Output tags require 1..32 ASCII letters, digits or hyphens."); }
                if (Outputs.empty() && !TagsEqual(output.Tag,"main")) { throw std::invalid_argument("The first output must retain the Core main tag (case-insensitive)."); }
                if (std::any_of(Outputs.begin(),Outputs.end(),[&output](const auto& existing) { return TagsEqual(existing.Tag,output.Tag); })) { throw std::invalid_argument("Duplicate output tag: "+output.Tag); }
                auto width=std::from_chars(fields[1].data(),fields[1].data()+fields[1].size(),output.Width); auto height=std::from_chars(fields[2].data(),fields[2].data()+fields[2].size(),output.Height);
                if (width.ec!=std::errc() || width.ptr!=fields[1].data()+fields[1].size() || height.ec!=std::errc() || height.ptr!=fields[2].data()+fields[2].size() || !output.Width || !output.Height || output.Width>16384 || output.Height>16384) { throw std::invalid_argument("Output dimensions must be positive integers up to 16384."); }
                if (fields[3]!="opaque" && fields[3]!="transparent") { throw std::invalid_argument("Output alpha must be opaque or transparent."); } output.Transparent=fields[3]=="transparent"; Outputs.push_back(std::move(output));
            }
            else if (argument=="--scene") { SceneId=value; }
            else if (argument=="--input-script") { InputScript=std::filesystem::absolute(value).string(); if (!std::filesystem::is_regular_file(InputScript)) { throw std::invalid_argument("--input-script must name an existing SDL input replay script."); } }
            else { auto result=std::from_chars(value.data(),value.data()+value.size(),Frames); if (result.ec!=std::errc() || result.ptr!=value.data()+value.size() || Frames==0) { throw std::invalid_argument("--frames must be a positive integer."); } }
        } else { throw std::invalid_argument("Unknown Linux player flag: " + argument); }
    }
    if (Session.empty()!=(!ParentPid)) { throw std::invalid_argument("--session and --parent-pid are required together."); }
    if (!Session.empty() && (Session.size()!=32 || !std::all_of(Session.begin(),Session.end(),[](char c) { return (c>='0' && c<='9') || (c>='a' && c<='f'); }))) { throw std::invalid_argument("Session requires exactly 32 lowercase hexadecimal characters."); }
    if (ContentRoot.empty() || !std::filesystem::is_directory(ContentRoot)) { throw std::invalid_argument("Linux player requires --content with an existing cooked package directory."); }
    if (Offscreen && !Frames) { throw std::invalid_argument("Offscreen core validation requires an explicit positive --frames limit."); }
}
}
