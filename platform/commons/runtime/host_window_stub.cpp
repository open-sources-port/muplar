#include "host_window.h"

namespace muplar::runtime {

HostWindow::HostWindow(int, int) {}
HostWindow::~HostWindow() = default;

bool HostWindow::valid() const { return false; }
bool HostWindow::closed() const { return true; }

void HostWindow::present_rgba(const uint8_t*, int, int, int) {}
void HostWindow::pump_events() {}
std::vector<HostInputEvent> HostWindow::take_input_events() { return {}; }
void HostWindow::run_for_ms(int) {}
void HostWindow::run_until_closed() {}

} // namespace muplar::runtime
