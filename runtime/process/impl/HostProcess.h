#pragma once

#include "../MuProcess.h"
#include <string>

class HostProcess : public MuProcess {
public:
    explicit HostProcess(const std::string& command);

    void start() override;
    void stop() override;
    bool isRunning() const override;
    std::string name() const override;

private:
    std::string command_;
    bool running_ = false;
};
