#pragma once

#include <cstdint>

namespace muplar::services
{

constexpr uint32_t kProtocolMagic = 0x4d555044;  // MUPD
constexpr uint16_t kProtocolVersion = 1;
constexpr uint16_t kReplyFlag = 0x8000;
constexpr uint32_t kMaxPayloadSize = 1024 * 1024;

enum class Opcode : uint16_t {
    Ping = 1,
    ListServices = 2,
    RegisterService = 3,
    CheckService = 4,
    SubscribePackages = 5,
    PackageChanged = 6,
    ClientInfo = 7,
    QueryPackages = 8,
    AppSession = 9,
    QueryTasks = 10,
    FdEcho = 11,
    GetSetting = 12,
    PutSetting = 13,
    BinderTransact = 14,
    BinderIncoming = 15,
    BinderReply = 16,
    ServiceState = 17,
    QueryUsers = 18,
    CheckPermission = 19,
    SetPermission = 20,
    ApplySurfaceTransaction = 21,
    QuerySurfaces = 22,
};

struct MessageHeader {
    uint32_t magic = kProtocolMagic;
    uint16_t version = kProtocolVersion;
    uint16_t opcode = 0;
    uint32_t payload_size = 0;
    uint64_t request_id = 0;
};

static_assert(sizeof(MessageHeader) == 24, "muplard protocol header changed");

}  // namespace muplar::services
