#include "linux_player.hpp"
#include <exception>
#include <iostream>

/// <summary>Starts the actual Linux generated-core player and preserves contextual runtime failures as a nonzero process status.</summary>
int main(int argc,char** argv) {
    try { return helengine::linux::LinuxPlayer::Run(helengine::linux::LinuxRunOptions(argc,argv)); }
    catch (const std::exception& error) { std::cerr << "Linux player failed: " << error.what() << '\n'; return 1; }
    catch (const std::exception* error) { std::cerr << "Linux player failed: " << (error ? error->what() : "null generated exception") << '\n'; delete error; return 1; }
    catch (...) { std::cerr << "Linux player failed with an unknown native exception.\n"; return 1; }
}
