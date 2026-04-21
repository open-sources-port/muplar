#include "HostProcess.h"
#include <cstdlib>
#include <iostream>

HostProcess::HostProcess(const std::string& command)
    : command_(command) {}

void HostProcess::start() {
    std::cout << "[HostProcess] Running: " << command_ << std::endl;
    running_ = true;
    system(command_.c_str());
    running_ = false;
}

void HostProcess::stop() {
    // TODO: Implement proper process termination
    std::cout << "[HostProcess] Stop not implemented\n";
}

bool HostProcess::isRunning() const {
    return running_;
}

std::string HostProcess::name() const {
    return "HostProcess";
}
