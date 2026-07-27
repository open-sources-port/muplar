#include "muplard_client.h"

#include "muplard_protocol.h"

#include <cstring>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace muplar::services
{
namespace
{

bool transfer_all(int fd, void *data, size_t size, bool sending)
{
    auto *bytes = static_cast<uint8_t *>(data);
    size_t offset = 0;
    while (offset < size) {
        ssize_t count =
            sending ? send(fd, bytes + offset, size - offset, MSG_NOSIGNAL)
                    : recv(fd, bytes + offset, size - offset, 0);
        if (count <= 0)
            return false;
        offset += static_cast<size_t>(count);
    }
    return true;
}

}  // namespace

MuplardClient::MuplardClient(std::string socket_path)
    : socket_path_(std::move(socket_path))
{
}

bool MuplardClient::available() const
{
    std::string reply;
    return request(static_cast<uint16_t>(Opcode::Ping), {}, reply) &&
           reply == "pong";
}

ServiceState MuplardClient::check_service(const std::string &name) const
{
    std::string reply;
    if (!request(static_cast<uint16_t>(Opcode::CheckService), name, reply))
        return ServiceState::Missing;
    if (reply == "2")
        return ServiceState::Owned;
    if (reply == "1")
        return ServiceState::Declared;
    return ServiceState::Missing;
}

bool MuplardClient::transact(const std::string &service,
                             const std::string &request_payload,
                             std::string &reply) const
{
    return request(static_cast<uint16_t>(Opcode::BinderTransact),
                   service + '\n' + request_payload, reply) &&
           reply != "DEAD_OBJECT";
}

bool MuplardClient::device_action(const std::string &action,
                                  const std::string &tab,
                                  std::string &generation) const
{
    return device_action(action, tab, {}, {}, {}, {}, generation);
}

bool MuplardClient::device_action(const std::string &action,
                                  const std::string &tab,
                                  const std::string &apk,
                                  const std::string &package_name,
                                  const std::string &activity,
                                  const std::string &application,
                                  std::string &generation) const
{
    return request(static_cast<uint16_t>(Opcode::DeviceAction),
                   action + '\n' + tab + '\n' + apk + '\n' + package_name +
                       '\n' + activity + '\n' + application,
                   generation) &&
           generation != "0";
}

bool MuplardClient::query_device_state(std::string &state) const
{
    return request(static_cast<uint16_t>(Opcode::QueryDeviceState), {}, state);
}

bool MuplardClient::request(uint16_t opcode,
                            const std::string &payload,
                            std::string &reply) const
{
    if (socket_path_.empty() ||
        socket_path_.size() >=
            sizeof(static_cast<sockaddr_un *>(nullptr)->sun_path) ||
        payload.size() > kMaxPayloadSize)
        return false;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path_.c_str(),
                socket_path_.size() + 1);
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) <
        0) {
        close(fd);
        return false;
    }

    MessageHeader header;
    header.opcode = opcode;
    header.payload_size = static_cast<uint32_t>(payload.size());
    header.request_id = 1;
    std::vector<uint8_t> packet(sizeof(header) + payload.size());
    std::memcpy(packet.data(), &header, sizeof(header));
    if (!payload.empty())
        std::memcpy(packet.data() + sizeof(header), payload.data(),
                    payload.size());

    bool ok = transfer_all(fd, packet.data(), packet.size(), true);
    MessageHeader response;
    if (ok)
        ok = transfer_all(fd, &response, sizeof(response), false);
    if (ok) {
        ok = response.magic == kProtocolMagic &&
             response.version == kProtocolVersion &&
             response.opcode == (opcode | kReplyFlag) &&
             response.payload_size <= kMaxPayloadSize;
    }
    reply.assign(ok ? response.payload_size : 0, '\0');
    if (ok && !reply.empty())
        ok = transfer_all(fd, reply.data(), reply.size(), false);
    close(fd);
    return ok;
}

}  // namespace muplar::services
