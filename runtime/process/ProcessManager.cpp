#include "ProcessManager.h"

#include "MuProcess.h"
#include "impl/HostProcess.h"
#include "impl/WineProcess.h"
#include "impl/AndroidProcess.h"

std::shared_ptr<MuProcess> ProcessManager::createHostProcess(const std::string& command) {
    return std::make_shared<HostProcess>(command);
}

std::shared_ptr<MuProcess> ProcessManager::createWindowsProcess(const std::string& exePath) {
    return std::make_shared<WineProcess>(exePath);
}

std::shared_ptr<MuProcess> ProcessManager::createAndroidProcess(const std::string& apkPath) {
    return std::make_shared<AndroidProcess>(apkPath);
}
