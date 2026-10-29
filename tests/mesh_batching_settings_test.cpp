#include "vulkan_mesh_batching_settings.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace helengine::vulkan;

/// <summary>Verifies native build selections and rejects invalid runtime bounds without requiring a GPU.</summary>
class MeshBatchingSettingsTest final {
public:
    /// <summary>Parses the expected CMake policy without accepting signs, overflow or trailing input.</summary>
    static uint64_t Read(const char* text) {
        std::string_view input(text); uint64_t value=0;
        const auto result=std::from_chars(input.data(),input.data()+input.size(),value);
        if(result.ec!=std::errc() || result.ptr!=input.data()+input.size()) throw std::invalid_argument("Expected native configuration value must be unsigned decimal.");
        return value;
    }
    /// <summary>Compares compiled settings with independently supplied expected values and checks every required bound.</summary>
    static void Run(int argc,char** argv) {
        if(argc!=6) throw std::invalid_argument("Expected static flag/bytes, dynamic flag/max vertices/bytes.");
        const auto actual=helengine::vulkan::VulkanMeshBatchingSettings::FromBuild();
        if(actual.StaticEnabled!=(Read(argv[1])!=0) || actual.StaticBudgetBytes!=Read(argv[2]) || actual.DynamicEnabled!=(Read(argv[3])!=0) || actual.DynamicMaxVertices!=Read(argv[4]) || actual.DynamicBudgetBytes!=Read(argv[5])) throw std::runtime_error("Compiled batching configuration diverged from the selected builder policy.");
        for(int index=0;index<3;++index) {
            auto invalid=actual;
            if(index==0) invalid.StaticBudgetBytes=0;
            else if(index==1) invalid.DynamicMaxVertices=0;
            else invalid.DynamicBudgetBytes=0;
            bool rejected=false;
            try { invalid.Validate(); } catch(const std::invalid_argument&) { rejected=true; }
            if(!rejected) throw std::runtime_error("Native batching accepted a zero required bound.");
        }
    }
};

/// <summary>Reports a failing process status if native policy propagation or required limits are wrong.</summary>
int main(int argc,char** argv) {
    try { MeshBatchingSettingsTest::Run(argc,argv); std::cout<<"PASS native batching compiled policy and bounds\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
