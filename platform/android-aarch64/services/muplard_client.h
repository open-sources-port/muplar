#pragma once

#include <cstdint>
#include <string>

namespace muplar::services
{

enum class ServiceState {
    Missing,
    Declared,
    Owned,
};

class MuplardClient
{
public:
    explicit MuplardClient(std::string socket_path);

    bool available() const;
    ServiceState check_service(const std::string &name) const;
    bool transact(const std::string &service,
                  const std::string &request,
                  std::string &reply) const;
    bool device_action(const std::string &action,
                       const std::string &tab,
                       std::string &generation) const;
    bool device_action(const std::string &action,
                       const std::string &tab,
                       const std::string &apk,
                       const std::string &package_name,
                       const std::string &activity,
                       const std::string &application,
                       std::string &generation) const;
    bool device_input(const std::string &tab,
                      int32_t type,
                      int32_t action,
                      int32_t source,
                      int32_t device_id,
                      int32_t key_code,
                      float x,
                      float y,
                      std::string &generation) const;
    bool query_device_state(std::string &state) const;

private:
    bool request(uint16_t opcode,
                 const std::string &payload,
                 std::string &reply) const;

    std::string socket_path_;
};

}  // namespace muplar::services
