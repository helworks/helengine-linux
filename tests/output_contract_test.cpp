#include "linux_run_options.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

/// <summary>Parses real player arguments with a caller-owned mutable argv array.</summary>
helengine::linux::LinuxRunOptions Parse(std::vector<std::string> arguments) {
    std::vector<char*> values;
    for (auto& argument : arguments) { values.push_back(argument.data()); }
    return helengine::linux::LinuxRunOptions(static_cast<int>(values.size()),values.data());
}

/// <summary>Requires invalid output or guarded-session arguments to fail before any graphics initialization.</summary>
void Reject(std::vector<std::string> arguments) {
    try { Parse(std::move(arguments)); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid output/session arguments were accepted.");
}

/// <summary>Exercises exact output tags, dimensions, uniqueness and guarded execution without desktop or Vulkan access.</summary>
int main() {
    try {
        const std::string root=std::filesystem::current_path().string();
        const auto options=Parse({"player","--content",root,"--output","main,640,480,opaque","--output","glass,320,200,transparent","--noninteractive","--session","0123456789abcdef0123456789abcdef","--parent-pid","123"});
        if (options.Outputs.size()!=2 || options.Outputs[0].Tag!="main" || options.Outputs[1].Width!=320 || !options.Outputs[1].Transparent || !options.Noninteractive || options.ParentPid!=123) { throw std::runtime_error("Valid multi-output options were not preserved."); }
        for (const auto& spec : {"main,0,1,opaque","main,1,16385,opaque","main,1,1,invalid","a_b,1,1,opaque",",1,1,opaque","main,1,1,opaque,extra","main,-1,1,opaque", "main,1,1,transparent\n"}) { Reject({"player","--content",root,"--output",spec}); }
        Reject({"player","--content",root,"--output","main,1,1,opaque","--output","main,2,2,transparent"});
        Reject({"player","--content",root,"--session","0123456789abcdef0123456789abcdef"});
        Reject({"player","--content",root,"--parent-pid","123"});
        Reject({"player","--content",root,"--session","0123456789ABCDEF0123456789abcdef","--parent-pid","123"});
        Reject({"player","--content",root,"--session","0123456789abcdef0123456789abcdef","--parent-pid","0"});
        Reject({"player","--content",root,"--session",""});
        Reject({"player","--content",root,"--offscreen"});
        const auto legacy=Parse({"player","--content",root,"--offscreen","--frames","1"});
        if (!legacy.Outputs.empty() || !legacy.Offscreen || legacy.Frames!=1) { throw std::runtime_error("Legacy single-output options changed."); }
        std::cout << "PASS output specs, guarded session and legacy options\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
