#include "muplard_protocol.h"

#include <algorithm>
#include <atomic>
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

namespace {

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
    std::unordered_set<std::string> owned_services;
    std::string app_package;
    std::string app_activity;
};

bool send_message(int fd, Opcode opcode, uint64_t request_id,
                  const std::string& payload, bool reply = true)
{
    MessageHeader header;
    header.opcode = static_cast<uint16_t>(opcode) |
        (reply ? muplar::services::kReplyFlag : 0);
    header.payload_size = static_cast<uint32_t>(payload.size());
    header.request_id = request_id;
    std::vector<uint8_t> packet(sizeof(header) + payload.size());
    std::memcpy(packet.data(), &header, sizeof(header));
    if (!payload.empty())
        std::memcpy(packet.data() + sizeof(header), payload.data(), payload.size());
    size_t sent = 0;
    while (sent < packet.size()) {
        ssize_t count = send(fd, packet.data() + sent, packet.size() - sent,
                             MSG_NOSIGNAL);
        if (count <= 0) return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

bool receive_exact(int fd, void* output, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(output);
    size_t received = 0;
    while (received < size) {
        ssize_t count = recv(fd, bytes + received, size - received, 0);
        if (count <= 0) return false;
        received += static_cast<size_t>(count);
    }
    return true;
}

bool send_message_with_fd(int socket_fd, Opcode opcode, uint64_t request_id,
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
    cmsghdr* cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &passed_fd, sizeof(passed_fd));
    return sendmsg(socket_fd, &message, MSG_NOSIGNAL) == sizeof(header);
}

bool receive_header_with_fd(int socket_fd, MessageHeader& header, int& passed_fd)
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
    if (count != sizeof(header)) return false;
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&message); cmsg;
         cmsg = CMSG_NXTHDR(&message, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
            std::memcpy(&passed_fd, CMSG_DATA(cmsg), sizeof(passed_fd));
            break;
        }
    }
    return true;
}

bool receive_message(int fd, MessageHeader& header, std::string& payload)
{
    if (!receive_exact(fd, &header, sizeof(header)) ||
        header.magic != muplar::services::kProtocolMagic ||
        header.version != muplar::services::kProtocolVersion ||
        header.payload_size > muplar::services::kMaxPayloadSize) return false;
    payload.assign(header.payload_size, '\0');
    return header.payload_size == 0 ||
        receive_exact(fd, payload.data(), payload.size());
}

std::string service_list(const std::unordered_map<std::string, int>& services)
{
    std::vector<std::string> names;
    names.reserve(services.size());
    for (const auto& entry : services)
        names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    std::string result;
    for (const auto& name : names) {
        if (!result.empty()) result.push_back('\n');
        result += name;
    }
    return result;
}

uint64_t registry_stamp(const fs::path& path)
{
    std::error_code ec;
    if (!fs::exists(path, ec)) return 0;
    auto size = fs::file_size(path, ec);
    auto time = fs::last_write_time(path, ec).time_since_epoch().count();
    return static_cast<uint64_t>(size) ^ static_cast<uint64_t>(time);
}

std::string read_text_file(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return "count=0\n";
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::unordered_map<std::string, std::string> load_settings(const fs::path& path)
{
    std::unordered_map<std::string, std::string> result;
    std::ifstream input(path);
    std::string key;
    std::string value;
    while (input >> std::quoted(key) >> std::quoted(value))
        result[key] = value;
    return result;
}

bool save_settings(const fs::path& path,
                   const std::unordered_map<std::string, std::string>& settings)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path temporary = path;
    temporary += ".tmp-" + std::to_string(getpid());
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    std::vector<std::string> keys;
    for (const auto& entry : settings) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());
    for (const auto& key : keys)
        output << std::quoted(key) << ' ' << std::quoted(settings.at(key)) << '\n';
    output.close();
    if (!output) return false;
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temporary, path, ec);
    }
    return !ec;
}

void identify_client(Client& client)
{
#ifdef __APPLE__
    uid_t euid = 0;
    gid_t egid = 0;
    if (getpeereid(client.fd, &euid, &egid) == 0)
        client.uid = euid;
    pid_t peer_pid = 0;
    socklen_t size = sizeof(peer_pid);
    if (getsockopt(client.fd, SOL_LOCAL, LOCAL_PEERPID,
                   &peer_pid, &size) == 0) {
        client.pid = peer_pid;
    }
#else
    struct ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (getsockopt(client.fd, SOL_SOCKET, SO_PEERCRED,
                   &credentials, &size) == 0) {
        client.pid = credentials.pid;
        client.uid = credentials.uid;
    }
#endif
}

int run_client(const fs::path& socket_path, const std::string& operation,
               const std::string& request_payload, const fs::path& fd_path)
{
    if (socket_path.string().size() >=
        sizeof(static_cast<sockaddr_un*>(nullptr)->sun_path)) return 2;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(),
                 sizeof(address.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close(fd);
        return 1;
    }

    if (operation == "binder-serve") {
        if (!send_message(fd, Opcode::RegisterService, 1, request_payload, false)) {
            close(fd);
            return 1;
        }
        MessageHeader registered;
        std::string registration;
        if (!receive_message(fd, registered, registration) || registration != "1") {
            close(fd);
            return 1;
        }
        std::cout << "registered" << std::endl;
        while (true) {
            MessageHeader incoming;
            std::string payload;
            if (!receive_message(fd, incoming, payload)) break;
            if (incoming.opcode == static_cast<uint16_t>(Opcode::BinderIncoming)) {
                if (!send_message(fd, Opcode::BinderReply, incoming.request_id,
                                  payload, false)) break;
            }
        }
        close(fd);
        return 0;
    }

    Opcode opcode = operation == "subscribe-packages"
        ? Opcode::SubscribePackages
        : operation == "list-services" ? Opcode::ListServices
        : operation == "query-packages" ? Opcode::QueryPackages
        : operation == "app-session" ? Opcode::AppSession
        : operation == "query-tasks" ? Opcode::QueryTasks
        : operation == "fd-echo" ? Opcode::FdEcho
        : operation == "settings-get" ? Opcode::GetSetting
        : operation == "settings-put" ? Opcode::PutSetting
        : operation == "check-service" ? Opcode::CheckService
        : operation == "binder-transact" ? Opcode::BinderTransact
        : Opcode::Ping;
    int passed_fd = -1;
    bool sent = false;
    if (opcode == Opcode::FdEcho) {
        passed_fd = open(fd_path.c_str(), O_RDONLY);
        sent = passed_fd >= 0 && send_message_with_fd(fd, opcode, 1, passed_fd);
        if (passed_fd >= 0) close(passed_fd);
    } else {
        sent = send_message(fd, opcode, 1, request_payload, false);
    }
    if (!sent) {
        close(fd);
        return 1;
    }
    while (true) {
        MessageHeader header;
        if (!receive_exact(fd, &header, sizeof(header))) break;
        if (header.magic != muplar::services::kProtocolMagic ||
            header.version != muplar::services::kProtocolVersion ||
            header.payload_size > muplar::services::kMaxPayloadSize) break;
        std::string payload(header.payload_size, '\0');
        if (header.payload_size != 0 &&
            !receive_exact(fd, payload.data(), payload.size())) break;
        std::cout << payload << std::endl;
        if (operation != "subscribe-packages" && operation != "app-session")
            break;
    }
    close(fd);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    fs::path socket_path;
    fs::path registry_path;
    fs::path pid_path;
    fs::path settings_path;
    std::string client_operation;
    std::string client_payload;
    fs::path client_fd_path;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--socket" && i + 1 < argc) socket_path = argv[++i];
        else if (arg == "--registry" && i + 1 < argc) registry_path = argv[++i];
        else if (arg == "--pid-file" && i + 1 < argc) pid_path = argv[++i];
        else if (arg == "--settings" && i + 1 < argc) settings_path = argv[++i];
        else if (arg == "--client" && i + 1 < argc) client_operation = argv[++i];
        else if (arg == "--payload" && i + 1 < argc) client_payload = argv[++i];
        else if (arg == "--fd" && i + 1 < argc) client_fd_path = argv[++i];
    }
    if (socket_path.empty()) {
        std::cerr << "muplard: --socket is required\n";
        return 2;
    }
    if (!client_operation.empty())
        return run_client(socket_path, client_operation, client_payload,
                          client_fd_path);
    if (socket_path.string().size() >=
        sizeof(static_cast<sockaddr_un*>(nullptr)->sun_path)) {
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
    std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1);
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(server, 32) < 0) {
        std::cerr << "muplard: bind/listen failed: " << std::strerror(errno) << "\n";
        close(server);
        return 1;
    }
    chmod(socket_path.c_str(), 0600);
    if (!pid_path.empty()) {
        fs::create_directories(pid_path.parent_path(), ec);
        std::ofstream(pid_path, std::ios::trunc) << getpid() << "\n";
    }

    std::unordered_map<std::string, int> services = {
        {"activity", -1}, {"package", -1}, {"launcherapps", -1},
        {"shortcut", -1}, {"appwidget", -1}, {"window", -1},
    };
    struct PendingTransaction {
        int caller_fd;
        int owner_fd;
        uint64_t caller_request_id;
    };
    std::unordered_map<uint64_t, PendingTransaction> pending_transactions;
    uint64_t next_transaction_id = 1;
    std::vector<Client> clients;
    uint64_t package_generation = 1;
    uint64_t previous_registry_stamp = registry_stamp(registry_path);
    std::unordered_map<std::string, std::string> settings =
        load_settings(settings_path);
    std::cerr << "[muplard] listening on " << socket_path << "\n";

    while (running.load()) {
        std::vector<pollfd> pollfds;
        pollfds.push_back({server, POLLIN, 0});
        for (const auto& client : clients)
            pollfds.push_back({client.fd, POLLIN, 0});
        int rc = poll(pollfds.data(), pollfds.size(), 500);
        if (rc < 0 && errno != EINTR) break;

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
                           header.version != muplar::services::kProtocolVersion ||
                           header.payload_size > muplar::services::kMaxPayloadSize) {
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
                            send_message(clients[i].fd, opcode, header.request_id, "pong");
                        } else if (opcode == Opcode::ListServices) {
                            send_message(clients[i].fd, opcode, header.request_id,
                                service_list(services));
                        } else if (opcode == Opcode::CheckService) {
                            auto service = services.find(payload);
                            send_message(clients[i].fd, opcode, header.request_id,
                                service == services.end() ? "0" :
                                service->second >= 0 ? "2" : "1");
                        } else if (opcode == Opcode::RegisterService) {
                            bool available = !payload.empty() &&
                                (!services.count(payload) || services[payload] < 0);
                            if (available) {
                                services[payload] = clients[i].fd;
                                clients[i].owned_services.insert(payload);
                            }
                            send_message(clients[i].fd, opcode, header.request_id,
                                available ? "1" : "0");
                        } else if (opcode == Opcode::SubscribePackages) {
                            clients[i].package_subscriber = true;
                            send_message(clients[i].fd, opcode, header.request_id,
                                std::to_string(package_generation));
                        } else if (opcode == Opcode::ClientInfo) {
                            send_message(clients[i].fd, opcode, header.request_id,
                                "pid=" + std::to_string(clients[i].pid) +
                                "\nuid=" + std::to_string(clients[i].uid));
                        } else if (opcode == Opcode::QueryPackages) {
                            send_message(clients[i].fd, opcode, header.request_id,
                                read_text_file(registry_path));
                        } else if (opcode == Opcode::AppSession) {
                            size_t separator = payload.find('\n');
                            clients[i].app_package = payload.substr(0, separator);
                            clients[i].app_activity = separator == std::string::npos
                                ? std::string() : payload.substr(separator + 1);
                            send_message(clients[i].fd, opcode, header.request_id,
                                clients[i].app_package.empty() ? "0" : "1");
                        } else if (opcode == Opcode::QueryTasks) {
                            std::string tasks;
                            for (const auto& client : clients) {
                                if (client.app_package.empty()) continue;
                                if (!tasks.empty()) tasks.push_back('\n');
                                tasks += client.app_package + "\t" +
                                    client.app_activity + "\t" +
                                    std::to_string(client.pid) + "\t" +
                                    std::to_string(client.uid);
                            }
                            send_message(clients[i].fd, opcode, header.request_id,
                                tasks);
                        } else if (opcode == Opcode::FdEcho) {
                            struct stat info{};
                            bool valid = passed_fd >= 0 && fstat(passed_fd, &info) == 0;
                            send_message(clients[i].fd, opcode, header.request_id,
                                valid ? std::to_string(info.st_size) : "-1");
                        } else if (opcode == Opcode::GetSetting) {
                            auto found = settings.find(payload);
                            send_message(clients[i].fd, opcode, header.request_id,
                                found == settings.end() ? std::string() : found->second);
                        } else if (opcode == Opcode::PutSetting) {
                            size_t separator = payload.find('\n');
                            bool valid = separator != std::string::npos &&
                                !payload.substr(0, separator).empty();
                            if (valid) {
                                std::string key = payload.substr(0, separator);
                                std::string value = payload.substr(separator + 1);
                                settings[key] = value;
                                valid = save_settings(settings_path, settings);
                            }
                            send_message(clients[i].fd, opcode, header.request_id,
                                valid ? "1" : "0");
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
                                    clients[i].fd, owner->second, header.request_id};
                                if (!send_message(owner->second,
                                        Opcode::BinderIncoming, transaction_id,
                                        payload.substr(separator + 1), false)) {
                                    pending_transactions.erase(transaction_id);
                                    send_message(clients[i].fd, opcode,
                                        header.request_id, "DEAD_OBJECT");
                                }
                            }
                        } else if (opcode == Opcode::BinderReply) {
                            auto pending = pending_transactions.find(header.request_id);
                            if (pending != pending_transactions.end() &&
                                pending->second.owner_fd == clients[i].fd) {
                                send_message(pending->second.caller_fd,
                                    Opcode::BinderTransact,
                                    pending->second.caller_request_id, payload);
                                pending_transactions.erase(pending);
                            }
                        }
                    }
                }
                if (passed_fd >= 0) close(passed_fd);
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
                for (const auto& name : clients[i].owned_services) {
                    auto found = services.find(name);
                    if (found != services.end() && found->second == clients[i].fd)
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
            for (auto& client : clients) {
                if (client.package_subscriber) {
                    send_message(client.fd, Opcode::PackageChanged, 0,
                        std::to_string(package_generation), false);
                }
            }
        }
    }

    for (auto& client : clients) close(client.fd);
    close(server);
    fs::remove(socket_path, ec);
    if (!pid_path.empty()) fs::remove(pid_path, ec);
    return 0;
}
