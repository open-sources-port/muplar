#pragma once

#include "../MuProcess.h"
#include <string>

class AndroidProcess : public MuProcess {
public:
    explicit AndroidProcess(const std::string& apkPath);

    void start() override;
    void stop() override;
    bool isRunning() const override;
    std::string name() const override;

private:
    std::string apkPath_;
    bool running_ = false;
};
