#include "linux_run_options.hpp"
#include <charconv>
#include <filesystem>
#include <stdexcept>

namespace helengine::linux {
LinuxRunOptions::LinuxRunOptions(int argc,char** argv) {
    for (int index=1;index<argc;++index) {
        const std::string argument=argv[index];
        if (argument=="--offscreen") { Offscreen=true; }
        else if (argument=="--validation") { Validation=true; }
        else if (argument=="--content" || argument=="--scene" || argument=="--frames" || argument=="--input-script") {
            if (++index>=argc) { throw std::invalid_argument("Missing value for Linux player flag: " + argument); }
            const std::string value=argv[index];
            if (argument=="--content") { ContentRoot=std::filesystem::absolute(value).string(); }
            else if (argument=="--scene") { SceneId=value; }
            else if (argument=="--input-script") { InputScript=std::filesystem::absolute(value).string(); if (!std::filesystem::is_regular_file(InputScript)) { throw std::invalid_argument("--input-script must name an existing SDL input replay script."); } }
            else { auto result=std::from_chars(value.data(),value.data()+value.size(),Frames); if (result.ec!=std::errc() || result.ptr!=value.data()+value.size() || Frames==0) { throw std::invalid_argument("--frames must be a positive integer."); } }
        } else { throw std::invalid_argument("Unknown Linux player flag: " + argument); }
    }
    if (ContentRoot.empty() || !std::filesystem::is_directory(ContentRoot)) { throw std::invalid_argument("Linux player requires --content with an existing cooked package directory."); }
    if (Offscreen && !Frames) { throw std::invalid_argument("Offscreen core validation requires an explicit positive --frames limit."); }
}
}
