#pragma once

#include <cstdint>
#include <vector>

namespace muplar::runtime
{

struct HostInputEvent {
    int32_t type = 0;
    int32_t action = 0;
    int32_t source = 0;
    int32_t device_id = 0;
    int32_t key_code = 0;
    float x = 0.0f;
    float y = 0.0f;
};

class HostWindow
{
public:
    struct Impl;

    HostWindow(int width, int height);
    ~HostWindow();

    HostWindow(const HostWindow &) = delete;
    HostWindow &operator=(const HostWindow &) = delete;

    bool valid() const;
    bool closed() const;

    void present_rgba(const uint8_t *pixels,
                      int width,
                      int height,
                      int stride_pixels);
    void pump_events();
    std::vector<HostInputEvent> take_input_events();
    void run_for_ms(int milliseconds);
    void run_until_closed();

private:
    Impl *impl_ = nullptr;
};

}  // namespace muplar::runtime
