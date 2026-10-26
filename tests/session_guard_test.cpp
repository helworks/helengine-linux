#include "linux_session_guard.hpp"
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>

/// <summary>Exercises the real private stdin gate in a child without initializing SDL or GPU resources.</summary>
void CheckGate(const std::string& message,bool accepted) {
    int channel[2]; if (pipe(channel)) { throw std::runtime_error("pipe failed"); }
    const auto parent=getpid(); const auto child=fork();
    if (child<0) { throw std::runtime_error("fork failed"); }
    if (!child) {
        close(channel[1]); dup2(channel[0],STDIN_FILENO); close(channel[0]);
        try { helengine::linux::LinuxSessionGuard::Activate("0123456789abcdef0123456789abcdef",parent); _exit(0); }
        catch (...) { _exit(1); }
    }
    close(channel[0]); if (!message.empty()) { if (write(channel[1],message.data(),message.size())!=static_cast<ssize_t>(message.size())) { throw std::runtime_error("gate write failed"); } } close(channel[1]);
    int status=0; if (waitpid(child,&status,0)!=child || !WIFEXITED(status) || ((WEXITSTATUS(status)==0)!=accepted)) { throw std::runtime_error("Guarded start gate accepted/rejected the wrong message."); }
}

/// <summary>Requires the kernel to terminate the exact guarded child when its independently forked supervisor dies.</summary>
void CheckParentDeath() {
    int startup[2],identity[2]; if (pipe(startup) || pipe(identity)) { throw std::runtime_error("Parent-death test pipe failed."); }
    const auto supervisor=fork();
    if (supervisor<0) { throw std::runtime_error("Parent-death supervisor fork failed."); }
    if (!supervisor) {
        close(identity[0]); const auto parent=getpid(); const auto player=fork();
        if (player<0) { _exit(2); }
        if (!player) {
            close(startup[1]); dup2(startup[0],STDIN_FILENO); close(startup[0]);
            try {
                helengine::linux::LinuxSessionGuard::Activate("0123456789abcdef0123456789abcdef",parent);
                const auto self=getpid(); if (write(identity[1],&self,sizeof(self))!=sizeof(self)) { _exit(3); }
                close(identity[1]); for (;;) { pause(); }
            } catch (...) { _exit(4); }
        }
        close(startup[0]); close(identity[1]);
        const std::string message="HELENGINE_START 0123456789abcdef0123456789abcdef\n";
        if (write(startup[1],message.data(),message.size())!=static_cast<ssize_t>(message.size())) { _exit(5); }
        close(startup[1]); for (;;) { pause(); }
    }
    close(startup[0]); close(startup[1]); close(identity[1]);
    pollfd receipt {identity[0],POLLIN,0}; const int available=poll(&receipt,1,2000); pid_t player=0;
    const bool identified=available>0 && read(identity[0],&player,sizeof(player))==sizeof(player); close(identity[0]);
    const int owned=identified?static_cast<int>(syscall(SYS_pidfd_open,player,0)):-1;
    kill(supervisor,SIGKILL); int status=0; waitpid(supervisor,&status,0);
    if (owned<0) { throw std::runtime_error("Could not retain exact guarded child identity."); }
    pollfd exited {owned,POLLIN,0}; const int stopped=poll(&exited,1,2000);
    if (stopped<=0) { syscall(SYS_pidfd_send_signal,owned,SIGKILL,nullptr,0); close(owned); throw std::runtime_error("Guarded child survived supervisor death."); } close(owned);
}

/// <summary>Rejects EOF, malformed commands, wrong nonce and excessive input, accepting only authenticated startup.</summary>
int main() {
    try {
        CheckParentDeath();
        CheckGate("HELENGINE_START 0123456789abcdef0123456789abcdef\n",true);
        CheckGate("",false); CheckGate("HELENGINE_START 00000000000000000000000000000000\n",false);
        CheckGate("HELENGINE_START 0123456789abcdef0123456789abcdef extra\n",false); CheckGate(std::string(100,'x')+"\n",false);
        std::cout << "PASS bounded authenticated parent start gate and kernel parent-death containment\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
