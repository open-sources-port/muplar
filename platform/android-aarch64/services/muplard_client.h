#pragma once

#include <cstdint>
#include <string>

namespace muplar::services {

enum class ServiceState {
    Missing,
    Declared,
    Owned,
};

class MuplardClient {
public:
    explicit MuplardClient(std::string socket_path);

    bool available() const;
    ServiceState check_service(const std::string& name) const;
    bool transact(const std::string& service,
                  const std::string& request,
                  std::string& reply) const;

private:
    bool request(uint16_t opcode,
                 const std::string& payload,
                 std::string& reply) const;

    std::string socket_path_;
};

} // namespace muplar::services
