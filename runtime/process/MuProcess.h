#pragma once

#include <string>

class MuProcess {
public:
    virtual ~MuProcess() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual bool isRunning() const = 0;

    virtual std::string name() const = 0;
};
