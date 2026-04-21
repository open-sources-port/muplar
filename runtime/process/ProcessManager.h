#pragma once

#include <memory>
#include <string>

class MuProcess;

class ProcessManager {
public:
    std::shared_ptr<MuProcess> createHostProcess(const std::string& command);
    std::shared_ptr<MuProcess> createWindowsProcess(const std::string& exePath);
    std::shared_ptr<MuProcess> createAndroidProcess(const std::string& apkPath);
};
