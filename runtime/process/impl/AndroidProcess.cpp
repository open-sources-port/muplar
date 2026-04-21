#include "AndroidProcess.h"
#include <iostream>

AndroidProcess::AndroidProcess(const std::string& apkPath)
    : apkPath_(apkPath) {}

void AndroidProcess::start() {
    std::cout << "[AndroidProcess] Pretend to run APK: " << apkPath_ << std::endl;
    running_ = true;

    // TODO: integrate APK parsing + runtime later

    running_ = false;
}

void AndroidProcess::stop() {
    std::cout << "[AndroidProcess] Stop not implemented\n";
}

bool AndroidProcess::isRunning() const {
    return running_;
}

std::string AndroidProcess::name() const {
    return "AndroidProcess";
}
