#include "client.hpp"
#include "../common/protocol.hpp"
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/socket.h>
#include <cstring>
#include <poll.h>
#include <arpa/inet.h>

namespace permup {

Client::Client() {
    config_ = std::make_unique<Config>();
    socket_ = std::make_unique<UnixSocket>();
    sessionManager_ = std::make_unique<SessionManager>();
    rateLimiter_ = std::make_unique<RateLimiter>();
}

Client::~Client() {}

bool Client::initialize(const std::string& configDir) {
    if (!config_->load(configDir)) {
        std::cerr << "Failed to load configuration\n";
        return false;
    }
    
    username_ = getUsername(getuid());
    if (username_.empty()) {
        std::cerr << "Failed to get username\n";
        return false;
    }
    
    return true;
}

int Client::run(const std::string& targetUser,
                const std::string& authUser,
                const std::string& command,
                bool listOnly) {
    std::cerr << "DEBUG: Client::run() START\n";
    std::cerr << "DEBUG: targetUser=" << targetUser << " authUser=" << authUser << " command=" << command << " listOnly=" << listOnly << "\n";
    
    if (listOnly) {
        if (!fetchAllowedUsers()) {
            return 1;
        }
        printAllowedUsers();
        return 0;
    }
    
    if (!connectToDaemon()) {
        std::cerr << "Failed to connect to permupd\n";
        return 1;
    }
    
    if (!fetchAllowedUsers()) {
        return 1;
    }
    
    if (!checkPermissions(targetUser, authUser)) {
        return 1;
    }
    
    if (!checkRateLimit()) {
        return 1;
    }
    
    std::string password = getPassword(targetUser, authUser);
    if (password.empty()) {
        return 1;
    }
    
    if (!executeCommand(targetUser, authUser, command, password)) {
        return 1;
    }
    
    return 0;
}

bool Client::connectToDaemon() {
    return socket_->connectClient("/run/permup/socket");
}

bool Client::fetchAllowedUsers() {
    if (!socket_->isConnected()) {
        if (!connectToDaemon()) {
            return false;
        }
    }
    
    std::vector<uint8_t> request = Protocol::buildListUsersRequest();
    if (!socket_->sendMessage(request)) {
        return false;
    }
    
    int fd = socket_->getFd();
    
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 5000);
    if (ret <= 0) {
        return false;
    }
    
    MessageHeader header;
    size_t received = 0;
    while (received < sizeof(MessageHeader)) {
        ssize_t n = read(fd, reinterpret_cast<uint8_t*>(&header) + received, 
                        sizeof(MessageHeader) - received);
        if (n <= 0) return false;
        received += n;
    }
    
    header.magic = ntohl(header.magic);
    header.type = static_cast<MessageType>(ntohl(static_cast<uint32_t>(header.type)));
    header.length = ntohl(header.length);
    header.flags = ntohl(header.flags);
    
    if (header.magic != Protocol::MAGIC) {
        return false;
    }
    
    if (header.length == 0 || header.length > 1024 * 1024) {
        return false;
    }
    
    std::vector<uint8_t> payload(header.length);
    received = 0;
    while (received < header.length) {
        ssize_t n = read(fd, payload.data() + received, header.length - received);
        if (n <= 0) return false;
        received += n;
    }
    
    if (header.type == MessageType::ERROR_RESPONSE) {
        ErrorCode code;
        std::string message;
        Protocol::parseErrorResponse(payload, code, message);
        std::cerr << "Error: " << message << "\n";
        return false;
    }
    
    if (header.type != MessageType::LIST_USERS_RESPONSE) {
        return false;
    }
    
    return Protocol::parseListUsersResponse(payload, allowedTargetUsers_, allowedAuthUsers_);
}

bool Client::checkPermissions(const std::string& targetUser, const std::string& authUser) {
    if (isRoot(username_)) {
        return true;
    }
    
    bool targetAllowed = false;
    bool authAllowed = false;
    
    for (const std::string& user : allowedTargetUsers_) {
        if (user == targetUser) {
            targetAllowed = true;
            break;
        }
    }
    
    for (const std::string& user : allowedAuthUsers_) {
        if (user == authUser) {
            authAllowed = true;
            break;
        }
    }
    
    if (!targetAllowed && targetUser == "root") targetAllowed = true;
    if (!authAllowed && authUser == "root") authAllowed = true;
    
    if (!targetAllowed && !authAllowed) {
        std::cerr << "You do not have permission as " << targetUser 
                  << " nor to authenticate as " << authUser << "\n";
        return false;
    }
    
    if (!targetAllowed) {
        std::cerr << "You do not have permission as " << targetUser << "\n";
        return false;
    }
    
    if (!authAllowed) {
        std::cerr << "You do not have permission to authenticate as " << authUser << "\n";
        return false;
    }
    
    return true;
}

bool Client::checkRateLimit() {
    RateLimitConfig config = config_->getRateLimitConfig(username_);
    return rateLimiter_->checkLimit(username_, config);
}

std::string Client::getPassword(const std::string& targetUser, const std::string& authUser) {
    if (isRoot(authUser)) {
        std::string password;
        std::cout << "Password for " << authUser << ": ";
        std::cin >> password;
        return password;
    }
    
    if (sessionManager_->hasValidSession(username_, targetUser, authUser)) {
        std::string password = sessionManager_->getPassword(username_, targetUser, authUser);
        if (!password.empty()) return password;
    }
    
    std::string password;
    std::cout << "Password for " << authUser << ": ";
    std::cin >> password;
    
    Seconds sessionTime = config_->getSessionTime(username_);
    if (sessionTime.count() > 0) {
        sessionManager_->storeSession(username_, targetUser, authUser, password, sessionTime);
    }
    
    return password;
}

bool Client::executeCommand(const std::string& targetUser,
                           const std::string& authUser,
                           const std::string& command,
                           const std::string& password) {
    std::cerr << "DEBUG: executeCommand() START\n";
    
    socket_->closeSocket();
    if (!connectToDaemon()) {
        std::cerr << "Failed to reconnect to daemon\n";
        return false;
    }
    
    std::vector<uint8_t> request = Protocol::buildExecuteRequest(
        targetUser, authUser, command, password
    );
    
    if (!socket_->sendMessage(request)) {
        std::cerr << "Failed to send request\n";
        return false;
    }
    
    int fd = socket_->getFd();
    
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 5000);
    if (ret <= 0) {
        std::cerr << "Timeout waiting for response\n";
        return false;
    }
    
    // ============================================================
    // RĘCZNY ODBIÓR NAGŁÓWKA I PAYLOADU – TAK SAMO JAK W fetchAllowedUsers
    // ============================================================
    MessageHeader header;
    size_t received = 0;
    while (received < sizeof(MessageHeader)) {
        ssize_t n = read(fd, reinterpret_cast<uint8_t*>(&header) + received, 
                        sizeof(MessageHeader) - received);
        if (n <= 0) {
            std::cerr << "Failed to read header\n";
            return false;
        }
        received += n;
    }
    
    header.magic = ntohl(header.magic);
    header.type = static_cast<MessageType>(ntohl(static_cast<uint32_t>(header.type)));
    header.length = ntohl(header.length);
    header.flags = ntohl(header.flags);
    
    if (header.magic != Protocol::MAGIC) {
        std::cerr << "Invalid magic number\n";
        return false;
    }
    
    if (header.length == 0 || header.length > 1024 * 1024) {
        std::cerr << "Invalid length: " << header.length << "\n";
        return false;
    }
    
    std::vector<uint8_t> payload(header.length);
    received = 0;
    while (received < header.length) {
        ssize_t n = read(fd, payload.data() + received, header.length - received);
        if (n <= 0) {
            std::cerr << "Failed to read payload\n";
            return false;
        }
        received += n;
    }
    
    if (header.type == MessageType::ERROR_RESPONSE) {
        ErrorCode code;
        std::string message;
        Protocol::parseErrorResponse(payload, code, message);
        std::cerr << "Error: " << message << "\n";
        rateLimiter_->recordFailedAttempt(username_);
        return false;
    }
    
    if (header.type != MessageType::EXECUTE_RESPONSE) {
        std::cerr << "Unexpected response type\n";
        return false;
    }
    
    rateLimiter_->resetAttempts(username_);
    
    // ODBIÓR PTY
    struct msghdr msg = {};
    struct iovec iov = {};
    char buffer[1] = {};
    char control[CMSG_SPACE(sizeof(int))] = {};
    
    iov.iov_base = buffer;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    
    ssize_t n = recvmsg(socket_->getFd(), &msg, 0);
    if (n <= 0) {
        std::cerr << "Failed to receive PTY\n";
        return false;
    }
    
    int masterPty = -1;
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        masterPty = *(int*)CMSG_DATA(cmsg);
    }
    
    if (masterPty < 0) {
        std::cerr << "Failed to receive PTY fd\n";
        return false;
    }
    
    handlePty(masterPty);
    return true;
}

void Client::handlePty(int masterPty) {
    if (masterPty < 0) return;
    
    struct termios orig_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    
    struct sigaction sa;
    sa.sa_handler = [](int) { exit(1); };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    
    struct termios raw = orig_termios;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    
    fd_set fds;
    char buffer[4096];
    
    while (true) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(masterPty, &fds);
        
        int maxFd = std::max(STDIN_FILENO, masterPty);
        int ret = select(maxFd + 1, &fds, nullptr, nullptr, nullptr);
        if (ret < 0) break;
        
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n <= 0) break;
            write(masterPty, buffer, n);
        }
        
        if (FD_ISSET(masterPty, &fds)) {
            ssize_t n = read(masterPty, buffer, sizeof(buffer));
            if (n <= 0) break;
            write(STDOUT_FILENO, buffer, n);
        }
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    close(masterPty);
}

void Client::printAllowedUsers() const {
    std::cout << "Allowed target users (-u):\n";
    for (const auto& user : allowedTargetUsers_) {
        std::cout << "  " << user << "\n";
    }
    std::cout << "\nAllowed authenticating users (-h):\n";
    for (const auto& user : allowedAuthUsers_) {
        std::cout << "  " << user << "\n";
    }
}

} // namespace permup
