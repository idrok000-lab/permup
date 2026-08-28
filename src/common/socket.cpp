#include "socket.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <cstdint>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>

namespace permup {

UnixSocket::UnixSocket() : fd_(-1) {}

UnixSocket::~UnixSocket() {
    closeSocket();
}

bool UnixSocket::createServer(const std::string& socketPath, mode_t mode) {
    socketPath_ = socketPath;
    unlink(socketPath.c_str());
    
    fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) return false;
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    
    if (listen(fd_, 32) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    
    chmod(socketPath.c_str(), mode);
    return true;
}

bool UnixSocket::connectClient(const std::string& socketPath) {
    fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) return false;
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

int UnixSocket::acceptConnection() {
    return accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
}

void UnixSocket::closeSocket() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void UnixSocket::removeSocketFile() {
    if (!socketPath_.empty()) {
        unlink(socketPath_.c_str());
        socketPath_.clear();
    }
}

bool UnixSocket::sendMessage(const std::vector<uint8_t>& data) {
    return sendMessageWithFd(data, -1);
}

bool UnixSocket::sendMessageWithFd(const std::vector<uint8_t>& data, int fd) {
    if (fd_ < 0) return false;
    if (fd >= 0) return sendFd(fd, fd_);
    
    size_t totalSent = 0;
    while (totalSent < data.size()) {
        ssize_t sent = write(fd_, data.data() + totalSent, data.size() - totalSent);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        totalSent += sent;
    }
    return true;
}

bool UnixSocket::receiveMessage(std::vector<uint8_t>& data) {
    if (fd_ < 0) return false;
    
    // Czekaj na dane z timeoutem 10 sekund
    struct pollfd pfd;
    pfd.fd = fd_;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 10000);  // 10 sekund timeout
    
    if (ret < 0) {
        std::cerr << "poll error: " << strerror(errno) << "\n";
        return false;
    }
    if (ret == 0) {
        std::cerr << "Timeout waiting for data\n";
        return false;
    }
    
    // Odczytaj 4 bajty długości
    uint32_t length = 0;
    size_t received = 0;
    while (received < sizeof(length)) {
        ssize_t n = read(fd_, reinterpret_cast<uint8_t*>(&length) + received, 
                        sizeof(length) - received);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::cerr << "read error: " << strerror(errno) << "\n";
            return false;
        }
        if (n == 0) {
            std::cerr << "Connection closed\n";
            return false;
        }
        received += n;
    }
    length = ntohl(length);
    
    if (length == 0 || length > 1024 * 1024) {
        std::cerr << "Invalid length: " << length << "\n";
        return false;
    }
    
    data.resize(length);
    received = 0;
    while (received < length) {
        ssize_t n = read(fd_, data.data() + received, length - received);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::cerr << "read error: " << strerror(errno) << "\n";
            return false;
        }
        if (n == 0) {
            std::cerr << "Connection closed during data read\n";
            return false;
        }
        received += n;
    }
    
    return true;
}

bool UnixSocket::receiveMessageWithFd(std::vector<uint8_t>& data, int& fd) {
    if (!receiveMessage(data)) return false;
    fd = receiveFd(fd_);
    return true;
}

bool UnixSocket::sendFd(int fd, int targetFd) {
    struct msghdr msg = {};
    struct iovec iov = {};
    char buffer[1] = {0};
    char control[CMSG_SPACE(sizeof(int))] = {};
    
    iov.iov_base = buffer;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == nullptr) return false;
    
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    *(int*)CMSG_DATA(cmsg) = fd;
    
    ssize_t n = sendmsg(targetFd, &msg, 0);
    return n == 1;
}

int UnixSocket::receiveFd(int fd) {
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
    
    ssize_t n = recvmsg(fd, &msg, 0);
    if (n != 1) return -1;
    
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        return *(int*)CMSG_DATA(cmsg);
    }
    return -1;
}

} // namespace permup