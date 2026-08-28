#include "daemon.hpp"
#include "child.hpp"
#include "../common/protocol.hpp"
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <wait.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <vector>
#include <map>
#include <fcntl.h>

namespace permup {

static Daemon* globalDaemon = nullptr;

Daemon::Daemon() : running_(false), socketPath_("/run/permup/socket") {
    config_ = std::make_unique<Config>();
    serverSocket_ = std::make_unique<UnixSocket>();
}

Daemon::~Daemon() {
    stop();
}

bool Daemon::initialize(const std::string& configDir) {
    if (!config_->load(configDir)) {
        std::cerr << "WARNING: Failed to load configuration, using defaults\n";
    }
    
    mkdir("/run/permup", 0755);
    chmod("/run/permup", 0755);
    
    if (!serverSocket_->createServer(socketPath_, 0666)) {
        std::cerr << "ERROR: Failed to create socket: " << strerror(errno) << "\n";
        return false;
    }
    
    std::cout << "permupd started, listening on " << socketPath_ << "\n";
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    
    globalDaemon = this;
    running_ = true;
    return true;
}

int Daemon::run() {
    if (!running_) return 1;
    
    std::vector<struct pollfd> fds;
    struct pollfd pfd;
    pfd.fd = serverSocket_->getFd();
    pfd.events = POLLIN;
    pfd.revents = 0;
    fds.push_back(pfd);
    
    std::map<int, std::vector<uint8_t>> clientBuffers;
    
    while (running_) {
        int ret = poll(fds.data(), fds.size(), 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll error: " << strerror(errno) << "\n";
            continue;
        }
        
        if (fds[0].revents & POLLIN) {
            int clientFd = serverSocket_->acceptConnection();
            if (clientFd >= 0) {
                std::cout << "DEBUG: New connection fd=" << clientFd << "\n";
                struct pollfd newPfd;
                newPfd.fd = clientFd;
                newPfd.events = POLLIN;
                newPfd.revents = 0;
                fds.push_back(newPfd);
                clientBuffers[clientFd] = std::vector<uint8_t>();
            }
        }
        
        for (size_t i = 1; i < fds.size(); ) {
            int fd = fds[i].fd;
            
            if (fds[i].revents & (POLLHUP | POLLERR)) {
                std::cout << "DEBUG: Client disconnected fd=" << fd << "\n";
                ::close(fd);
                clientBuffers.erase(fd);
                fds.erase(fds.begin() + i);
                continue;
            }
            
            if (fds[i].revents & POLLIN) {
                char buffer[4096];
                ssize_t n = read(fd, buffer, sizeof(buffer));
                if (n <= 0) {
                    std::cout << "DEBUG: Read error or EOF fd=" << fd << "\n";
                    ::close(fd);
                    clientBuffers.erase(fd);
                    fds.erase(fds.begin() + i);
                    continue;
                }
                
                std::cout << "DEBUG: Read " << n << " bytes from fd=" << fd << "\n";
                clientBuffers[fd].insert(clientBuffers[fd].end(), buffer, buffer + n);
                processClientData(fd, clientBuffers[fd]);
            }
            
            ++i;
        }
    }
    
    return 0;
}

void Daemon::processClientData(int fd, std::vector<uint8_t>& buffer) {
    while (true) {
        if (buffer.size() < sizeof(MessageHeader)) {
            return;
        }
        
        MessageHeader header;
        memcpy(&header, buffer.data(), sizeof(MessageHeader));
        
        header.magic = ntohl(header.magic);
        header.type = static_cast<MessageType>(ntohl(static_cast<uint32_t>(header.type)));
        header.length = ntohl(header.length);
        header.flags = ntohl(header.flags);
        
        if (header.magic != Protocol::MAGIC) {
            std::cerr << "ERROR: Invalid magic number fd=" << fd << "\n";
            ::close(fd);
            return;
        }
        
        if (header.length > 1024 * 1024) {
            std::cerr << "ERROR: Length too large " << header.length << " fd=" << fd << "\n";
            ::close(fd);
            return;
        }
        
        size_t totalMsgSize = sizeof(MessageHeader) + header.length;
        if (buffer.size() < totalMsgSize) {
            return;
        }
        
        std::vector<uint8_t> payload(header.length);
        memcpy(payload.data(), buffer.data() + sizeof(MessageHeader), header.length);
        buffer.erase(buffer.begin(), buffer.begin() + totalMsgSize);
        
        if (header.type == MessageType::LIST_USERS_REQUEST) {
            handleListUsersRequest(fd);
        } else if (header.type == MessageType::EXECUTE_REQUEST) {
            handleExecuteRequest(fd, payload);
        } else {
            std::cerr << "ERROR: Unknown message type " << (int)header.type << " fd=" << fd << "\n";
            ::close(fd);
            return;
        }
        
        if (fcntl(fd, F_GETFD) == -1) {
            return;
        }
    }
}

void Daemon::handleListUsersRequest(int fd) {
    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        ::close(fd);
        return;
    }
    
    std::string username = getUsername(cred.uid);
    if (username.empty()) {
        ::close(fd);
        return;
    }
    
    std::vector<std::string> targetUsers = config_->getAllowedTargetUsers(username);
    std::vector<std::string> authUsers = config_->getAllowedAuthUsers(username);
    
    if (targetUsers.empty()) targetUsers.push_back("root");
    if (authUsers.empty()) authUsers.push_back("root");
    
    std::vector<uint8_t> response = Protocol::buildListUsersResponse(targetUsers, authUsers);
    
    size_t totalSent = 0;
    while (totalSent < response.size()) {
        ssize_t written = write(fd, response.data() + totalSent, response.size() - totalSent);
        if (written <= 0) {
            ::close(fd);
            return;
        }
        totalSent += written;
    }
    
    ::close(fd);
}

void Daemon::handleExecuteRequest(int fd, const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(ExecuteRequest)) {
        ::close(fd);
        return;
    }
    
    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        ::close(fd);
        return;
    }
    
    std::string callingUser = getUsername(cred.uid);
    if (callingUser.empty()) {
        ::close(fd);
        return;
    }
    
    const ExecuteRequest* req = reinterpret_cast<const ExecuteRequest*>(payload.data());
    
    uint32_t targetLen = ntohl(req->targetUserLen);
    uint32_t authLen = ntohl(req->authUserLen);
    uint32_t cmdLen = ntohl(req->commandLen);
    uint32_t passLen = ntohl(req->passwordLen);
    
    size_t offset = sizeof(ExecuteRequest);
    
    if (offset + targetLen + authLen + cmdLen + passLen > payload.size()) {
        ::close(fd);
        return;
    }
    
    std::string targetUser(reinterpret_cast<const char*>(payload.data() + offset), targetLen);
    offset += targetLen;
    
    std::string authUser(reinterpret_cast<const char*>(payload.data() + offset), authLen);
    offset += authLen;
    
    std::string command(reinterpret_cast<const char*>(payload.data() + offset), cmdLen);
    offset += cmdLen;
    
    std::string password(reinterpret_cast<const char*>(payload.data() + offset), passLen);
    
    (void)password;
    
    // ============================================================
    // ZGODNIE Z DOKUMENTACJĄ: Jeśli -h root → pomiń wszystkie sprawdzenia
    // ============================================================
    if (!isRoot(authUser)) {
        if (!config_->isCommandAllowed(callingUser, command, targetUser)) {
            std::vector<uint8_t> error = Protocol::buildErrorResponse(
                ErrorCode::PERMISSION_DENIED,
                "You do not have permission to execute this command."
            );
            
            size_t totalSent = 0;
            while (totalSent < error.size()) {
                ssize_t written = write(fd, error.data() + totalSent, error.size() - totalSent);
                if (written <= 0) {
                    ::close(fd);
                    return;
                }
                totalSent += written;
            }
            ::close(fd);
            return;
        }
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        Child child;
        child.run(fd, targetUser, authUser, command, callingUser, *config_);
        exit(0);
    } else if (pid > 0) {
        std::vector<uint8_t> response = Protocol::buildExecuteResponse(pid, 0);
        size_t totalSent = 0;
        while (totalSent < response.size()) {
            ssize_t written = write(fd, response.data() + totalSent, response.size() - totalSent);
            if (written <= 0) {
                ::close(fd);
                return;
            }
            totalSent += written;
        }
        ::close(fd);
    } else {
        ::close(fd);
    }
}

void Daemon::handleClient(int clientFd) {
    (void)clientFd;
}

void Daemon::handleListUsersRequest(int clientFd, const std::string& username) {
    (void)clientFd;
    (void)username;
}

void Daemon::handleExecuteRequest(int clientFd, const std::string& username,
                                 const std::string& targetUser,
                                 const std::string& authUser,
                                 const std::string& command,
                                 const std::string& password) {
    (void)clientFd;
    (void)username;
    (void)targetUser;
    (void)authUser;
    (void)command;
    (void)password;
}

bool Daemon::authenticateUser(const std::string& username, const std::string& password) {
    (void)username;
    (void)password;
    return true;
}

void Daemon::forkChild(int clientFd, const std::string& targetUser,
                      const std::string& authUser,
                      const std::string& command,
                      const std::string& username) {
    (void)clientFd;
    (void)targetUser;
    (void)authUser;
    (void)command;
    (void)username;
}

void Daemon::reapChildren() {
    while (true) {
        pid_t pid = waitpid(-1, nullptr, WNOHANG);
        if (pid <= 0) break;
    }
}

void Daemon::stop() {
    running_ = false;
    if (serverSocket_) {
        serverSocket_->closeSocket();
        serverSocket_->removeSocketFile();
    }
    globalDaemon = nullptr;
}

void Daemon::signalHandler(int sig) {
    if (sig == SIGCHLD) {
        if (globalDaemon) {
            globalDaemon->reapChildren();
        }
    } else if (sig == SIGTERM || sig == SIGINT) {
        if (globalDaemon) {
            globalDaemon->stop();
        }
    }
}

} // namespace permup