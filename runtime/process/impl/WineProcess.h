#pragma once

#include "../MuProcess.h"
#include <string>

class WineProcess : public MuProcess {
public:
    explicit WineProcess(const std::string& exePath);

    void start() override;
    void stop() override;
    bool isRunning() const override;
    std::string name() const override;

private:
    std::string exePath_;
    bool running_ = false;
};
