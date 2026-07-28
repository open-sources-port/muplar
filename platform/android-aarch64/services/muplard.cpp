#include "muplard_protocol.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <poll.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __APPLE__
#include <sys/ucred.h>
#endif

namespace fs = std::filesystem;
using muplar::services::MessageHeader;
using muplar::services::Opcode;

namespace
{

std::atomic<bool> running{true};

void handle_signal(int)
{
    running.store(false);
}

struct Client {
    int fd = -1;
    pid_t pid = 0;
    uid_t uid = 0;
    bool package_subscriber = false;
    bool device_action_subscriber = false;
    bool device_input_subscriber = false;
    std::unordered_set<std::string> owned_services;
    std::string app_package;
    std::string app_activity;
};

bool send_message(int fd,
                  Opcode opcode,
                  uint64_t request_id,
                  const std::string &payload,
                  bool reply = true)
{
    MessageHeader header;
    header.opcode = static_cast<uint16_t>(opcode) |
                    (reply ? muplar::services::kReplyFlag : 0);
    header.payload_size = static_cast<uint32_t>(payload.size());
    header.request_id = request_id;
    std::vector<uint8_t> packet(sizeof(header) + payload.size());
    std::memcpy(packet.data(), &header, sizeof(header));
    if (!payload.empty())
        std::memcpy(packet.data() + sizeof(header), payload.data(),
                    payload.size());
    size_t sent = 0;
    while (sent < packet.size()) {
        ssize_t count =
            send(fd, packet.data() + sent, packet.size() - sent, MSG_NOSIGNAL);
        if (count <= 0)
            return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

bool receive_exact(int fd, void *output, size_t size)
{
    auto *bytes = static_cast<uint8_t *>(output);
    size_t received = 0;
    while (received < size) {
        ssize_t count = recv(fd, bytes + received, size - received, 0);
        if (count <= 0)
            return false;
        received += static_cast<size_t>(count);
    }
    return true;
}

bool send_message_with_fd(int socket_fd,
                          Opcode opcode,
                          uint64_t request_id,
                          int passed_fd)
{
    MessageHeader header;
    header.opcode = static_cast<uint16_t>(opcode);
    header.request_id = request_id;
    iovec io{&header, sizeof(header)};
    char control[CMSG_SPACE(sizeof(int))]{};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &passed_fd, sizeof(passed_fd));
    return sendmsg(socket_fd, &message, MSG_NOSIGNAL) == sizeof(header);
}

bool receive_header_with_fd(int socket_fd,
                            MessageHeader &header,
                            int &passed_fd)
{
    passed_fd = -1;
    iovec io{&header, sizeof(header)};
    char control[CMSG_SPACE(sizeof(int))]{};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t count = recvmsg(socket_fd, &message, MSG_WAITALL);
    if (count != sizeof(header))
        return false;
    for (cmsghdr *cmsg = CMSG_FIRSTHDR(&message); cmsg;
         cmsg = CMSG_NXTHDR(&message, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
            std::memcpy(&passed_fd, CMSG_DATA(cmsg), sizeof(passed_fd));
            break;
        }
    }
    return true;
}

bool receive_message(int fd, MessageHeader &header, std::string &payload)
{
    if (!receive_exact(fd, &header, sizeof(header)) ||
        header.magic != muplar::services::kProtocolMagic ||
        header.version != muplar::services::kProtocolVersion ||
        header.payload_size > muplar::services::kMaxPayloadSize)
        return false;
    payload.assign(header.payload_size, '\0');
    return header.payload_size == 0 ||
           receive_exact(fd, payload.data(), payload.size());
}

std::string service_list(const std::unordered_map<std::string, int> &services)
{
    std::vector<std::string> names;
    names.reserve(services.size());
    for (const auto &entry : services)
        names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    std::string result;
    for (const auto &name : names) {
        if (!result.empty())
            result.push_back('\n');
        result += name;
    }
    return result;
}

std::unordered_map<std::string, int> builtin_services()
{
    return {
        {"activity", -1},     {"package", -1},        {"launcherapps", -1},
        {"shortcut", -1},     {"appwidget", -1},      {"window", -1},
        {"input_method", -1}, {"permission", -1},     {"user", -1},
        {"settings", -1},     {"surfaceflinger", -1},
    };
}

std::string framework_service_state(
    const std::unordered_map<std::string, int> &services,
    uint64_t generation,
    std::chrono::steady_clock::time_point started)
{
    auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - started)
                      .count();
    return "state=ready\ngeneration=" + std::to_string(generation) +
           "\nuptimeMillis=" + std::to_string(uptime) +
           "\nservices=" + std::to_string(services.size()) +
           "\nserviceNames=" + service_list(services);
}

int run_lifecycle_self_test()
{
    auto services = builtin_services();
    std::string state =
        framework_service_state(services, 42, std::chrono::steady_clock::now());
    if (state.find("state=ready") == std::string::npos ||
        state.find("generation=42") == std::string::npos ||
        state.find("activity") == std::string::npos ||
        state.find("permission") == std::string::npos ||
        state.find("surfaceflinger") == std::string::npos)
        return 1;
    std::cout << "frameworkHost=ready\nserviceCatalog=" << services.size()
              << "\nrestartGeneration=tracked\n";
    return 0;
}

uint64_t registry_stamp(const fs::path &path)
{
    std::error_code ec;
    if (!fs::exists(path, ec))
        return 0;
    auto size = fs::file_size(path, ec);
    auto time = fs::last_write_time(path, ec).time_since_epoch().count();
    return static_cast<uint64_t>(size) ^ static_cast<uint64_t>(time);
}

std::string read_text_file(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return "count=0\n";
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::unordered_map<std::string, std::string> load_settings(const fs::path &path)
{
    std::unordered_map<std::string, std::string> result;
    std::ifstream input(path);
    std::string key;
    std::string value;
    while (input >> std::quoted(key) >> std::quoted(value))
        result[key] = value;
    return result;
}

bool save_settings(const fs::path &path,
                   const std::unordered_map<std::string, std::string> &settings)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path temporary = path;
    temporary += ".tmp-" + std::to_string(getpid());
    std::ofstream output(temporary, std::ios::trunc);
    if (!output)
        return false;
    std::vector<std::string> keys;
    for (const auto &entry : settings)
        keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());
    for (const auto &key : keys)
        output << std::quoted(key) << ' ' << std::quoted(settings.at(key))
               << '\n';
    output.close();
    if (!output)
        return false;
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temporary, path, ec);
    }
    return !ec;
}

bool user_exists(const std::unordered_map<std::string, std::string> &settings,
                 const std::string &user)
{
    auto found = settings.find("users");
    std::string users = found == settings.end() ? "0" : found->second;
    size_t start = 0;
    while (start <= users.size()) {
        size_t end = users.find(',', start);
        if (users.substr(start, end - start) == user)
            return true;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return false;
}

std::string permission_key(const std::string &package,
                           const std::string &user,
                           const std::string &permission)
{
    return "permission:" + user + ":" + package + ":" + permission;
}

bool permission_granted(
    const std::unordered_map<std::string, std::string> &settings,
    const std::string &package,
    const std::string &user,
    const std::string &permission)
{
    static const std::unordered_set<std::string> dangerous = {
        "android.permission.CAMERA",
        "android.permission.RECORD_AUDIO",
        "android.permission.READ_CONTACTS",
        "android.permission.WRITE_CONTACTS",
        "android.permission.ACCESS_FINE_LOCATION",
        "android.permission.ACCESS_COARSE_LOCATION",
        "android.permission.READ_EXTERNAL_STORAGE",
        "android.permission.WRITE_EXTERNAL_STORAGE",
    };
    auto override = settings.find(permission_key(package, user, permission));
    if (override != settings.end())
        return override->second == "grant";
    return user_exists(settings, user) && !package.empty() &&
           !permission.empty() && !dangerous.count(permission);
}

int run_policy_self_test(const fs::path &settings_path)
{
    auto settings = load_settings(settings_path);
    settings["users"] = "0,10";
    const std::string package = "com.muplar.policytest";
    const std::string camera = "android.permission.CAMERA";
    if (permission_granted(settings, package, "10", camera) ||
        !permission_granted(settings, package, "10",
                            "android.permission.INTERNET") ||
        permission_granted(settings, package, "11",
                           "android.permission.INTERNET"))
        return 1;
    settings[permission_key(package, "10", camera)] = "grant";
    if (!save_settings(settings_path, settings))
        return 1;
    auto reloaded = load_settings(settings_path);
    if (!permission_granted(reloaded, package, "10", camera) ||
        !user_exists(reloaded, "0") || !user_exists(reloaded, "10"))
        return 1;
    std::cout << "policyPersistence=ok\nusers=0,10\n"
              << "dangerousDefault=denied\nexplicitGrant=ok\n";
    return 0;
}

struct SurfaceState {
    std::string name;
    int width = 0;
    int height = 0;
    int layer = 0;
    float alpha = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    int crop_width = 0;
    int crop_height = 0;
    bool visible = false;
};

struct DeviceState {
    uint64_t generation = 0;
    std::string action;
    std::string tab;
    std::string apk;
    std::string package_name;
    std::string activity;
    std::string application;
};

struct DeviceInputState {
    uint64_t generation = 0;
    std::string tab;
    int32_t type = 0;
    int32_t action = 0;
    int32_t source = 0;
    int32_t device_id = 0;
    int32_t key_code = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct TabFinishedState {
    uint64_t generation = 0;
    std::string tab;
};

bool parse_int32(const std::string &value, int32_t &out)
{
    try {
        size_t parsed = 0;
        long converted = std::stol(value, &parsed);
        if (parsed != value.size() || converted < INT32_MIN ||
            converted > INT32_MAX)
            return false;
        out = static_cast<int32_t>(converted);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_float(const std::string &value, float &out)
{
    try {
        size_t parsed = 0;
        float converted = std::stof(value, &parsed);
        if (parsed != value.size())
            return false;
        out = converted;
        return true;
    } catch (...) {
        return false;
    }
}

bool valid_device_action(const std::string &action)
{
    static const std::unordered_set<std::string> actions = {
        "back",        "home",      "recents",   "settings",
        "install-apk", "focus-tab", "close-tab",
    };
    return actions.count(action) != 0;
}

bool apply_device_action(DeviceState &state, const std::string &payload)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= payload.size()) {
        size_t end = payload.find('\n', start);
        fields.push_back(payload.substr(start, end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    std::string action = fields.size() > 0 ? fields[0] : std::string();
    std::string tab = fields.size() > 1 ? fields[1] : std::string();
    if (!valid_device_action(action))
        return false;
    state.action = std::move(action);
    state.tab = std::move(tab);
    state.apk = fields.size() > 2 ? fields[2] : std::string();
    state.package_name = fields.size() > 3 ? fields[3] : std::string();
    state.activity = fields.size() > 4 ? fields[4] : std::string();
    state.application = fields.size() > 5 ? fields[5] : std::string();
    ++state.generation;
    return true;
}

bool apply_device_input(DeviceInputState &state, const std::string &payload)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= payload.size()) {
        size_t end = payload.find('\n', start);
        fields.push_back(payload.substr(start, end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    if (fields.size() != 8)
        return false;
    DeviceInputState next;
    next.tab = fields[0];
    if (!parse_int32(fields[1], next.type) ||
        !parse_int32(fields[2], next.action) ||
        !parse_int32(fields[3], next.source) ||
        !parse_int32(fields[4], next.device_id) ||
        !parse_int32(fields[5], next.key_code) ||
        !parse_float(fields[6], next.x) || !parse_float(fields[7], next.y))
        return false;
    if (next.type != 1 && next.type != 2)
        return false;
    next.generation = state.generation + 1;
    state = std::move(next);
    return true;
}

std::string device_state_reply(const DeviceState &state)
{
    return "generation=" + std::to_string(state.generation) +
           "\naction=" + state.action + "\ntab=" + state.tab +
           "\napk=" + state.apk + "\npackage=" + state.package_name +
           "\nactivity=" + state.activity +
           "\napplication=" + state.application;
}

std::string device_input_reply(const DeviceInputState &state)
{
    return "generation=" + std::to_string(state.generation) +
           "\ntab=" + state.tab + "\ntype=" + std::to_string(state.type) +
           "\naction=" + std::to_string(state.action) +
           "\nsource=" + std::to_string(state.source) +
           "\ndeviceId=" + std::to_string(state.device_id) +
           "\nkeyCode=" + std::to_string(state.key_code) +
           "\nx=" + std::to_string(state.x) + "\ny=" + std::to_string(state.y);
}

bool apply_tab_finished(TabFinishedState &state, const std::string &payload)
{
    if (payload.empty())
        return false;
    state.tab = payload;
    ++state.generation;
    return true;
}

std::string tab_finished_reply(const TabFinishedState &state)
{
    return "generation=" + std::to_string(state.generation) +
           "\ntab=" + state.tab;
}

bool apply_surface_transaction(
    std::unordered_map<uint64_t, SurfaceState> &surfaces,
    const std::string &payload)
{
    auto updated = surfaces;
    bool valid = true;
    size_t start = 0;
    while (valid && start < payload.size()) {
        size_t end = payload.find('\n', start);
        std::string line = payload.substr(start, end - start);
        std::vector<std::string> fields;
        size_t field_start = 0;
        while (field_start <= line.size()) {
            size_t tab = line.find('\t', field_start);
            fields.push_back(line.substr(field_start, tab - field_start));
            if (tab == std::string::npos)
                break;
            field_start = tab + 1;
        }
        try {
            if (fields.size() >= 2 && fields[0] == "remove") {
                updated.erase(std::stoull(fields[1]));
            } else if (fields.size() == 5 && fields[0] == "create") {
                uint64_t id = std::stoull(fields[1]);
                SurfaceState surface;
                surface.name = fields[2];
                surface.width = std::stoi(fields[3]);
                surface.height = std::stoi(fields[4]);
                surface.crop_width = surface.width;
                surface.crop_height = surface.height;
                valid = id != 0 && surface.width >= 0 && surface.height >= 0 &&
                        !updated.count(id);
                if (valid)
                    updated[id] = std::move(surface);
            } else if (fields.size() >= 2) {
                uint64_t id = std::stoull(fields[1]);
                auto surface = updated.find(id);
                valid = surface != updated.end();
                if (valid && fields[0] == "show")
                    surface->second.visible = true;
                else if (valid && fields[0] == "hide")
                    surface->second.visible = false;
                else if (valid && fields.size() == 3 && fields[0] == "layer")
                    surface->second.layer = std::stoi(fields[2]);
                else if (valid && fields.size() == 3 && fields[0] == "alpha")
                    surface->second.alpha = std::stof(fields[2]);
                else if (valid && fields.size() == 4 &&
                         fields[0] == "position") {
                    surface->second.x = std::stof(fields[2]);
                    surface->second.y = std::stof(fields[3]);
                } else if (valid && fields.size() == 4 && fields[0] == "crop") {
                    surface->second.crop_width = std::stoi(fields[2]);
                    surface->second.crop_height = std::stoi(fields[3]);
                } else if (valid && fields[0] != "show" &&
                           fields[0] != "hide") {
                    valid = false;
                }
            } else {
                valid = false;
            }
        } catch (const std::exception &) {
            valid = false;
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    if (valid)
        surfaces = std::move(updated);
    return valid;
}

std::string surface_list(
    const std::unordered_map<uint64_t, SurfaceState> &surfaces,
    uint64_t generation)
{
    std::vector<std::pair<uint64_t, SurfaceState>> ordered(surfaces.begin(),
                                                           surfaces.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto &left, const auto &right) {
                  if (left.second.layer != right.second.layer)
                      return left.second.layer < right.second.layer;
                  return left.first < right.first;
              });
    std::string result = "generation=" + std::to_string(generation);
    for (const auto &entry : ordered) {
        result += "\n" + std::to_string(entry.first) + "\t" +
                  entry.second.name + "\t" +
                  std::to_string(entry.second.layer) + "\t" +
                  (entry.second.visible ? "visible" : "hidden") + "\t" +
                  std::to_string(entry.second.alpha) + "\t" +
                  std::to_string(entry.second.x) + "," +
                  std::to_string(entry.second.y) + "\t" +
                  std::to_string(entry.second.crop_width) + "x" +
                  std::to_string(entry.second.crop_height);
    }
    return result;
}

int run_surface_self_test()
{
    std::unordered_map<uint64_t, SurfaceState> surfaces;
    if (!apply_surface_transaction(
            surfaces,
            "create\t1\tbackground\t800\t600\n"
            "create\t2\tlauncher\t400\t300\n"
            "layer\t1\t0\nlayer\t2\t10\nshow\t2\n"
            "alpha\t2\t0.75\nposition\t2\t20\t30\ncrop\t2\t320\t240"))
        return 1;
    auto before = surfaces;
    if (apply_surface_transaction(surfaces, "show\t1\nlayer\t999\t4") ||
        surfaces.size() != before.size() || surfaces[1].visible)
        return 1;
    std::string list = surface_list(surfaces, 1);
    if (list.find("2\tlauncher\t10\tvisible") == std::string::npos ||
        list.find("0.750000") == std::string::npos ||
        list.find("320x240") == std::string::npos)
        return 1;
    std::cout << "surfaceAtomicity=ok\nlayerOrdering=ok\n"
              << "visibilityAlphaCrop=ok\n";
    return 0;
}

int run_device_self_test()
{
    DeviceState state;
    DeviceInputState input;
    if (apply_device_action(state, "invalid\nlauncher"))
        return 1;
    if (!apply_device_action(
            state, "focus-tab\nlauncher\n/apk.apk\npkg\n.Main\npkg.App") ||
        state.generation != 1 || state.action != "focus-tab" ||
        state.tab != "launcher" || state.apk != "/apk.apk" ||
        state.package_name != "pkg" || state.activity != ".Main" ||
        state.application != "pkg.App")
        return 1;
    if (!apply_device_action(state, "back\nlauncher") ||
        state.generation != 2 || state.action != "back")
        return 1;
    std::string reply = device_state_reply(state);
    if (reply.find("generation=2") == std::string::npos ||
        reply.find("action=back") == std::string::npos ||
        reply.find("tab=launcher") == std::string::npos)
        return 1;
    if (!apply_device_input(input, "launcher\n2\n0\n4098\n1\n0\n120.5\n240") ||
        input.generation != 1 || input.type != 2 || input.action != 0 ||
        input.x != 120.5f)
        return 1;
    if (apply_device_input(input, "launcher\n9\n0\n4098\n1\n0\n0\n0"))
        return 1;
    std::string input_reply = device_input_reply(input);
    if (input_reply.find("generation=1") == std::string::npos ||
        input_reply.find("type=2") == std::string::npos ||
        input_reply.find("x=120.500000") == std::string::npos)
        return 1;
    std::cout << "deviceActionValidation=ok\n"
              << "deviceActionGeneration=ok\n"
              << "deviceStateQuery=ok\n"
              << "deviceInputValidation=ok\n"
              << "deviceInputGeneration=ok\n";
    return 0;
}

void identify_client(Client &client)
{
#ifdef __APPLE__
    uid_t euid = 0;
    gid_t egid = 0;
    if (getpeereid(client.fd, &euid, &egid) == 0)
        client.uid = euid;
    pid_t peer_pid = 0;
    socklen_t size = sizeof(peer_pid);
    if (getsockopt(client.fd, SOL_LOCAL, LOCAL_PEERPID, &peer_pid, &size) ==
        0) {
        client.pid = peer_pid;
    }
#else
    struct ucred credentials = {};
    socklen_t size = sizeof(credentials);
    if (getsockopt(client.fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) ==
        0) {
        client.pid = credentials.pid;
        client.uid = credentials.uid;
    }
#endif
}

int run_client(const fs::path &socket_path,
               const std::string &operation,
               const std::string &request_payload,
               const fs::path &fd_path)
{
    if (socket_path.string().size() >=
        sizeof(static_cast<sockaddr_un *>(nullptr)->sun_path))
        return 2;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return 1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(),
                 sizeof(address.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) <
        0) {
        close(fd);
        return 1;
    }

    if (operation == "binder-serve") {
        if (!send_message(fd, Opcode::RegisterService, 1, request_payload,
                          false)) {
            close(fd);
            return 1;
        }
        MessageHeader registered;
        std::string registration;
        if (!receive_message(fd, registered, registration) ||
            registration != "1") {
            close(fd);
            return 1;
        }
        std::cout << "registered" << std::endl;
        while (true) {
            MessageHeader incoming;
            std::string payload;
            if (!receive_message(fd, incoming, payload))
                break;
            if (incoming.opcode ==
                static_cast<uint16_t>(Opcode::BinderIncoming)) {
                if (!send_message(fd, Opcode::BinderReply, incoming.request_id,
                                  payload, false))
                    break;
            }
        }
        close(fd);
        return 0;
    }

    Opcode opcode =
        operation == "subscribe-packages"    ? Opcode::SubscribePackages
        : operation == "list-services"       ? Opcode::ListServices
        : operation == "query-packages"      ? Opcode::QueryPackages
        : operation == "app-session"         ? Opcode::AppSession
        : operation == "query-tasks"         ? Opcode::QueryTasks
        : operation == "fd-echo"             ? Opcode::FdEcho
        : operation == "settings-get"        ? Opcode::GetSetting
        : operation == "settings-put"        ? Opcode::PutSetting
        : operation == "check-service"       ? Opcode::CheckService
        : operation == "service-state"       ? Opcode::ServiceState
        : operation == "query-users"         ? Opcode::QueryUsers
        : operation == "check-permission"    ? Opcode::CheckPermission
        : operation == "set-permission"      ? Opcode::SetPermission
        : operation == "surface-transaction" ? Opcode::ApplySurfaceTransaction
        : operation == "query-surfaces"      ? Opcode::QuerySurfaces
        : operation == "device-action"       ? Opcode::DeviceAction
        : operation == "query-device-state"  ? Opcode::QueryDeviceState
        : operation == "tab-finished"        ? Opcode::TabFinished
        : operation == "query-tab-finished"  ? Opcode::QueryTabFinished
        : operation == "device-input"        ? Opcode::DeviceInput
        : operation == "subscribe-device-inputs" ? Opcode::SubscribeDeviceInputs
        : operation == "subscribe-device-actions"
            ? Opcode::SubscribeDeviceActions
        : operation == "binder-transact" ? Opcode::BinderTransact
                                         : Opcode::Ping;
    int passed_fd = -1;
    bool sent = false;
    if (opcode == Opcode::FdEcho) {
        passed_fd = open(fd_path.c_str(), O_RDONLY);
        sent = passed_fd >= 0 && send_message_with_fd(fd, opcode, 1, passed_fd);
        if (passed_fd >= 0)
            close(passed_fd);
    } else {
        sent = send_message(fd, opcode, 1, request_payload, false);
    }
    if (!sent) {
        close(fd);
        return 1;
    }
    while (true) {
        MessageHeader header;
        if (!receive_exact(fd, &header, sizeof(header)))
            break;
        if (header.magic != muplar::services::kProtocolMagic ||
            header.version != muplar::services::kProtocolVersion ||
            header.payload_size > muplar::services::kMaxPayloadSize)
            break;
        std::string payload(header.payload_size, '\0');
        if (header.payload_size != 0 &&
            !receive_exact(fd, payload.data(), payload.size()))
            break;
        std::cout << payload << std::endl;
        if (operation != "subscribe-packages" && operation != "app-session" &&
            operation != "subscribe-device-actions" &&
            operation != "subscribe-device-inputs")
            break;
    }
    close(fd);
    return 0;
}

}  // namespace

int main(int argc, char **argv)
{
    fs::path socket_path;
    fs::path registry_path;
    fs::path pid_path;
    fs::path settings_path;
    std::string client_operation;
    std::string client_payload;
    fs::path client_fd_path;
    bool policy_self_test = false;
    bool surface_self_test = false;
    bool lifecycle_self_test = false;
    bool device_self_test = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--socket" && i + 1 < argc)
            socket_path = argv[++i];
        else if (arg == "--registry" && i + 1 < argc)
            registry_path = argv[++i];
        else if (arg == "--pid-file" && i + 1 < argc)
            pid_path = argv[++i];
        else if (arg == "--settings" && i + 1 < argc)
            settings_path = argv[++i];
        else if (arg == "--client" && i + 1 < argc)
            client_operation = argv[++i];
        else if (arg == "--payload" && i + 1 < argc)
            client_payload = argv[++i];
        else if (arg == "--fd" && i + 1 < argc)
            client_fd_path = argv[++i];
        else if (arg == "--self-test-policy")
            policy_self_test = true;
        else if (arg == "--self-test-surface")
            surface_self_test = true;
        else if (arg == "--self-test-lifecycle")
            lifecycle_self_test = true;
        else if (arg == "--self-test-device")
            device_self_test = true;
    }
    if (policy_self_test)
        return run_policy_self_test(settings_path);
    if (surface_self_test)
        return run_surface_self_test();
    if (lifecycle_self_test)
        return run_lifecycle_self_test();
    if (device_self_test)
        return run_device_self_test();
    if (socket_path.empty()) {
        std::cerr << "muplard: --socket is required\n";
        return 2;
    }
    if (!client_operation.empty())
        return run_client(socket_path, client_operation, client_payload,
                          client_fd_path);
    if (socket_path.string().size() >=
        sizeof(static_cast<sockaddr_un *>(nullptr)->sun_path)) {
        std::cerr << "muplard: socket path is too long\n";
        return 2;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);
    std::error_code ec;
    fs::create_directories(socket_path.parent_path(), ec);
    fs::remove(socket_path, ec);

    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        std::cerr << "muplard: socket failed: " << std::strerror(errno) << "\n";
        return 1;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(),
                 sizeof(address.sun_path) - 1);
    if (bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) <
            0 ||
        listen(server, 32) < 0) {
        std::cerr << "muplard: bind/listen failed: " << std::strerror(errno)
                  << "\n";
        close(server);
        return 1;
    }
    chmod(socket_path.c_str(), 0600);
    if (!pid_path.empty()) {
        fs::create_directories(pid_path.parent_path(), ec);
        std::ofstream(pid_path, std::ios::trunc) << getpid() << "\n";
    }

    std::unordered_map<std::string, int> services = builtin_services();
    struct PendingTransaction {
        int caller_fd;
        int owner_fd;
        uint64_t caller_request_id;
    };
    std::unordered_map<uint64_t, PendingTransaction> pending_transactions;
    std::unordered_map<uint64_t, SurfaceState> surfaces;
    uint64_t surface_generation = 0;
    DeviceState device_state;
    DeviceInputState device_input_state;
    TabFinishedState tab_finished_state;
    uint64_t next_transaction_id = 1;
    std::vector<Client> clients;
    uint64_t package_generation = 1;
    uint64_t previous_registry_stamp = registry_stamp(registry_path);
    std::unordered_map<std::string, std::string> settings =
        load_settings(settings_path);
    const auto service_started = std::chrono::steady_clock::now();
    const uint64_t service_generation = static_cast<uint64_t>(getpid());
    if (!settings.count("users"))
        settings["users"] = "0";
    std::cerr << "[muplard] listening on " << socket_path << "\n";

    while (running.load()) {
        std::vector<pollfd> pollfds;
        pollfds.push_back({server, POLLIN, 0});
        for (const auto &client : clients)
            pollfds.push_back({client.fd, POLLIN, 0});
        int rc = poll(pollfds.data(), pollfds.size(), 500);
        if (rc < 0 && errno != EINTR)
            break;

        if (rc > 0 && (pollfds[0].revents & POLLIN)) {
            int fd = accept(server, nullptr, nullptr);
            if (fd >= 0) {
                Client client;
                client.fd = fd;
                identify_client(client);
                clients.push_back(std::move(client));
            }
        }

        for (size_t i = clients.size(); i-- > 0;) {
            short events = pollfds.size() > i + 1 ? pollfds[i + 1].revents : 0;
            bool remove_client = (events & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            if (!remove_client && (events & POLLIN)) {
                MessageHeader header;
                int passed_fd = -1;
                if (!receive_header_with_fd(clients[i].fd, header, passed_fd)) {
                    remove_client = true;
                } else if (header.magic != muplar::services::kProtocolMagic ||
                           header.version !=
                               muplar::services::kProtocolVersion ||
                           header.payload_size >
                               muplar::services::kMaxPayloadSize) {
                    remove_client = true;
                } else {
                    std::string payload(header.payload_size, '\0');
                    if (header.payload_size != 0 &&
                        !receive_exact(clients[i].fd, payload.data(),
                                       header.payload_size)) {
                        remove_client = true;
                    } else {
                        Opcode opcode = static_cast<Opcode>(header.opcode);
                        if (opcode == Opcode::Ping) {
                            send_message(clients[i].fd, opcode,
                                         header.request_id, "pong");
                        } else if (opcode == Opcode::ListServices) {
                            send_message(clients[i].fd, opcode,
                                         header.request_id,
                                         service_list(services));
                        } else if (opcode == Opcode::CheckService) {
                            auto service = services.find(payload);
                            send_message(clients[i].fd, opcode,
                                         header.request_id,
                                         service == services.end() ? "0"
                                         : service->second >= 0    ? "2"
                                                                   : "1");
                        } else if (opcode == Opcode::RegisterService) {
                            bool available =
                                !payload.empty() && (!services.count(payload) ||
                                                     services[payload] < 0);
                            if (available) {
                                services[payload] = clients[i].fd;
                                clients[i].owned_services.insert(payload);
                            }
                            send_message(clients[i].fd, opcode,
                                         header.request_id,
                                         available ? "1" : "0");
                        } else if (opcode == Opcode::SubscribePackages) {
                            clients[i].package_subscriber = true;
                            send_message(clients[i].fd, opcode,
                                         header.request_id,
                                         std::to_string(package_generation));
                        } else if (opcode == Opcode::ClientInfo) {
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                "pid=" + std::to_string(clients[i].pid) +
                                    "\nuid=" + std::to_string(clients[i].uid));
                        } else if (opcode == Opcode::QueryPackages) {
                            send_message(clients[i].fd, opcode,
                                         header.request_id,
                                         read_text_file(registry_path));
                        } else if (opcode == Opcode::AppSession) {
                            size_t separator = payload.find('\n');
                            clients[i].app_package =
                                payload.substr(0, separator);
                            clients[i].app_activity =
                                separator == std::string::npos
                                    ? std::string()
                                    : payload.substr(separator + 1);
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                clients[i].app_package.empty() ? "0" : "1");
                        } else if (opcode == Opcode::QueryTasks) {
                            std::string tasks;
                            for (const auto &client : clients) {
                                if (client.app_package.empty())
                                    continue;
                                if (!tasks.empty())
                                    tasks.push_back('\n');
                                tasks += client.app_package + "\t" +
                                         client.app_activity + "\t" +
                                         std::to_string(client.pid) + "\t" +
                                         std::to_string(client.uid);
                            }
                            send_message(clients[i].fd, opcode,
                                         header.request_id, tasks);
                        } else if (opcode == Opcode::FdEcho) {
                            struct stat info = {};
                            bool valid =
                                passed_fd >= 0 && fstat(passed_fd, &info) == 0;
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                valid ? std::to_string(info.st_size) : "-1");
                        } else if (opcode == Opcode::GetSetting) {
                            auto found = settings.find(payload);
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                found == settings.end() ? std::string()
                                                        : found->second);
                        } else if (opcode == Opcode::PutSetting) {
                            size_t separator = payload.find('\n');
                            bool valid = separator != std::string::npos &&
                                         !payload.substr(0, separator).empty();
                            if (valid) {
                                std::string key = payload.substr(0, separator);
                                std::string value =
                                    payload.substr(separator + 1);
                                settings[key] = value;
                                valid = save_settings(settings_path, settings);
                            }
                            send_message(clients[i].fd, opcode,
                                         header.request_id, valid ? "1" : "0");
                        } else if (opcode == Opcode::ServiceState) {
                            send_message(clients[i].fd, opcode,
                                         header.request_id,
                                         framework_service_state(
                                             services, service_generation,
                                             service_started));
                        } else if (opcode == Opcode::QueryUsers) {
                            send_message(clients[i].fd, opcode,
                                         header.request_id, settings["users"]);
                        } else if (opcode == Opcode::CheckPermission) {
                            size_t first = payload.find('\n');
                            size_t second = first == std::string::npos
                                                ? std::string::npos
                                                : payload.find('\n', first + 1);
                            std::string package = payload.substr(0, first);
                            std::string user =
                                first == std::string::npos
                                    ? std::string()
                                    : payload.substr(first + 1,
                                                     second - first - 1);
                            std::string permission =
                                second == std::string::npos
                                    ? std::string()
                                    : payload.substr(second + 1);
                            bool granted = permission_granted(settings, package,
                                                              user, permission);
                            send_message(clients[i].fd, opcode,
                                         header.request_id,
                                         granted ? "0" : "-1");
                        } else if (opcode == Opcode::SetPermission) {
                            size_t first = payload.find('\n');
                            size_t second = first == std::string::npos
                                                ? std::string::npos
                                                : payload.find('\n', first + 1);
                            size_t third = second == std::string::npos
                                               ? std::string::npos
                                               : payload.find('\n', second + 1);
                            bool valid = first != std::string::npos &&
                                         second != std::string::npos &&
                                         third != std::string::npos;
                            if (valid) {
                                std::string package = payload.substr(0, first);
                                std::string user = payload.substr(
                                    first + 1, second - first - 1);
                                std::string permission = payload.substr(
                                    second + 1, third - second - 1);
                                std::string value = payload.substr(third + 1);
                                valid = !package.empty() && !user.empty() &&
                                        !permission.empty() &&
                                        (value == "grant" || value == "deny");
                                if (valid) {
                                    settings[permission_key(
                                        package, user, permission)] = value;
                                    valid =
                                        save_settings(settings_path, settings);
                                }
                            }
                            send_message(clients[i].fd, opcode,
                                         header.request_id, valid ? "1" : "0");
                        } else if (opcode == Opcode::ApplySurfaceTransaction) {
                            bool valid =
                                apply_surface_transaction(surfaces, payload);
                            if (valid)
                                ++surface_generation;
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                valid ? std::to_string(surface_generation)
                                      : "0");
                        } else if (opcode == Opcode::QuerySurfaces) {
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                surface_list(surfaces, surface_generation));
                        } else if (opcode == Opcode::SubscribeDeviceActions) {
                            clients[i].device_action_subscriber = true;
                            send_message(clients[i].fd, opcode,
                                         header.request_id,
                                         device_state_reply(device_state));
                        } else if (opcode == Opcode::SubscribeDeviceInputs) {
                            clients[i].device_input_subscriber = true;
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                device_input_reply(device_input_state));
                        } else if (opcode == Opcode::DeviceAction) {
                            bool valid =
                                apply_device_action(device_state, payload);
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                valid ? std::to_string(device_state.generation)
                                      : "0");
                            if (valid) {
                                std::string state =
                                    device_state_reply(device_state);
                                for (const auto &client : clients) {
                                    if (client.device_action_subscriber)
                                        send_message(
                                            client.fd,
                                            Opcode::DeviceActionChanged, 0,
                                            state, false);
                                }
                            }
                        } else if (opcode == Opcode::DeviceInput) {
                            bool valid =
                                apply_device_input(device_input_state, payload);
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                valid ? std::to_string(
                                            device_input_state.generation)
                                      : "0");
                            if (valid) {
                                std::string state =
                                    device_input_reply(device_input_state);
                                for (const auto &client : clients) {
                                    if (client.device_input_subscriber)
                                        send_message(client.fd,
                                                     Opcode::DeviceInputChanged,
                                                     0, state, false);
                                }
                            }
                        } else if (opcode == Opcode::QueryDeviceState) {
                            send_message(clients[i].fd, opcode,
                                         header.request_id,
                                         device_state_reply(device_state));
                        } else if (opcode == Opcode::TabFinished) {
                            bool valid =
                                apply_tab_finished(tab_finished_state, payload);
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                valid ? std::to_string(
                                            tab_finished_state.generation)
                                      : "0");
                        } else if (opcode == Opcode::QueryTabFinished) {
                            send_message(
                                clients[i].fd, opcode, header.request_id,
                                tab_finished_reply(tab_finished_state));
                        } else if (opcode == Opcode::BinderTransact) {
                            size_t separator = payload.find('\n');
                            std::string service = payload.substr(0, separator);
                            auto owner = services.find(service);
                            if (separator == std::string::npos ||
                                owner == services.end() || owner->second < 0) {
                                send_message(clients[i].fd, opcode,
                                             header.request_id, "DEAD_OBJECT");
                            } else {
                                uint64_t transaction_id = next_transaction_id++;
                                pending_transactions[transaction_id] = {
                                    clients[i].fd, owner->second,
                                    header.request_id};
                                if (!send_message(
                                        owner->second, Opcode::BinderIncoming,
                                        transaction_id,
                                        payload.substr(separator + 1), false)) {
                                    pending_transactions.erase(transaction_id);
                                    send_message(clients[i].fd, opcode,
                                                 header.request_id,
                                                 "DEAD_OBJECT");
                                }
                            }
                        } else if (opcode == Opcode::BinderReply) {
                            auto pending =
                                pending_transactions.find(header.request_id);
                            if (pending != pending_transactions.end() &&
                                pending->second.owner_fd == clients[i].fd) {
                                send_message(pending->second.caller_fd,
                                             Opcode::BinderTransact,
                                             pending->second.caller_request_id,
                                             payload);
                                pending_transactions.erase(pending);
                            }
                        }
                    }
                }
                if (passed_fd >= 0)
                    close(passed_fd);
            }
            if (remove_client) {
                for (auto transaction = pending_transactions.begin();
                     transaction != pending_transactions.end();) {
                    if (transaction->second.owner_fd == clients[i].fd) {
                        send_message(transaction->second.caller_fd,
                                     Opcode::BinderTransact,
                                     transaction->second.caller_request_id,
                                     "DEAD_OBJECT");
                        transaction = pending_transactions.erase(transaction);
                    } else if (transaction->second.caller_fd == clients[i].fd) {
                        transaction = pending_transactions.erase(transaction);
                    } else {
                        ++transaction;
                    }
                }
                for (const auto &name : clients[i].owned_services) {
                    auto found = services.find(name);
                    if (found != services.end() &&
                        found->second == clients[i].fd)
                        services.erase(found);
                }
                close(clients[i].fd);
                clients.erase(clients.begin() + i);
            }
        }

        uint64_t current_stamp = registry_stamp(registry_path);
        if (current_stamp != previous_registry_stamp) {
            previous_registry_stamp = current_stamp;
            ++package_generation;
            for (auto &client : clients) {
                if (client.package_subscriber) {
                    send_message(client.fd, Opcode::PackageChanged, 0,
                                 std::to_string(package_generation), false);
                }
            }
        }
    }

    for (auto &client : clients)
        close(client.fd);
    close(server);
    fs::remove(socket_path, ec);
    if (!pid_path.empty())
        fs::remove(pid_path, ec);
    return 0;
}
