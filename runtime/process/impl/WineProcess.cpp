#include "WineProcess.h"
#include <cstdlib>
#include <iostream>

WineProcess::WineProcess(const std::string& exePath)
    : exePath_(exePath) {}

void WineProcess::start() {
    std::string cmd = "wine \"" + exePath_ + "\"";

    std::cout << "[WineProcess] Running: " << cmd << std::endl;

    running_ = true;
    system(cmd.c_str());
    running_ = false;
}

void WineProcess::stop() {
    // TODO: kill wine process properly
    std::cout << "[WineProcess] Stop not implemented\n";
}

bool WineProcess::isRunning() const {
    return running_;
}

std::string WineProcess::name() const {
    return "WineProcess";
}
