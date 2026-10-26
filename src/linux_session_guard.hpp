#pragma once
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/prctl.h>
#include <unistd.h>

namespace helengine::linux {
/// <summary>Contains the player within its authenticated direct supervisor before graphics initialization.</summary>
class LinuxSessionGuard final {
public:
    /// <summary>Installs kernel parent-death containment, verifies the direct parent, and waits for one bounded authenticated start line.</summary>
    static void Activate(const std::string& session,uint32_t parentPid) {
        if (prctl(PR_SET_PDEATHSIG,SIGKILL)!=0) { throw std::runtime_error("Cannot install Linux supervisor parent-death containment."); }
        if (getppid()!=static_cast<pid_t>(parentPid)) { throw std::runtime_error("Linux supervisor parent changed before guarded initialization."); }
        const std::string expected="HELENGINE_START "+session+"\n";
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        std::string received;
        while (received.size()<expected.size()) {
            if (std::chrono::steady_clock::now()>=deadline) { throw std::runtime_error("Timed out awaiting authenticated Linux player startup."); }
            pollfd input {STDIN_FILENO,POLLIN,0}; const int ready=poll(&input,1,100);
            if (ready<0) { if (errno==EINTR) { continue; } throw std::runtime_error("Cannot poll supervisor start channel."); }
            if (!ready) { continue; }
            char value=0; const auto count=read(STDIN_FILENO,&value,1);
            if (count<0 && errno==EINTR) { continue; }
            if (count!=1) { throw std::runtime_error("Supervisor start channel closed before authenticated startup."); }
            received.push_back(value);
            if (value!=expected[received.size()-1]) { throw std::runtime_error("Malformed or unauthenticated Linux player startup."); }
        }
    }
};
}
