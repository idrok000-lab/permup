#include "child.hpp"
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>
#include <arpa/inet.h>
#include <errno.h>

namespace permup {

Child::Child() : clientFd_(-1), masterPty_(-1), slavePty_(-1), 
                childPid_(-1), commandPid_(-1) {}

Child::~Child() {
    cleanup();
}

bool Child::run(int clientFd, 
               const std::string& targetUser,
               const std::string& authUser,
               const std::string& command,
               const std::string& callingUser,
               const Config& config) {
    (void)callingUser;
    
    clientFd_ = clientFd;
    
    if (!setupPty()) {
        return false;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        ::close(masterPty_);
        executeCommand(targetUser, command);
        exit(0);
    } else if (pid < 0) {
        cleanup();
        return false;
    }
    
    commandPid_ = pid;
    
    if (!isRoot(authUser)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        
        if (!checkShellPatterns(config, authUser)) {
            kill(commandPid_, SIGTERM);
            cleanup();
            return false;
        }
    }
    
    if (!sendPtyToClient()) {
        kill(commandPid_, SIGTERM);
        cleanup();
        return false;
    }
    
    if (!handleTimeout(config, command)) {
        kill(commandPid_, SIGTERM);
        cleanup();
        return false;
    }
    
    int status;
    waitpid(commandPid_, &status, 0);
    
    cleanup();
    return true;
}

bool Child::setupPty() {
    if (openpty(&masterPty_, &slavePty_, nullptr, nullptr, nullptr) < 0) {
        return false;
    }
    
    struct termios tty;
    if (tcgetattr(slavePty_, &tty) == 0) {
        cfmakeraw(&tty);
        tcsetattr(slavePty_, TCSANOW, &tty);
    }
    
    return true;
}

bool Child::executeCommand(const std::string& targetUser, const std::string& command) {
    ::close(masterPty_);
    
    setsid();
    if (ioctl(slavePty_, TIOCSCTTY, 0) < 0) {
        return false;
    }
    
    dup2(slavePty_, STDIN_FILENO);
    dup2(slavePty_, STDOUT_FILENO);
    dup2(slavePty_, STDERR_FILENO);
    ::close(slavePty_);
    
    std::string cmd = "runuser -l " + targetUser + " -c \"" + command + "; exit\"";
    execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
    
    return false;
}

bool Child::checkShellPatterns(const Config& config, const std::string& authUser) {
    (void)authUser;
    
    char buffer[4096];
    ssize_t n = read(masterPty_, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        return true;
    }
    buffer[n] = '\0';
    
    std::string output(buffer);
    const auto& patterns = config.getShellPatterns();
    
    for (const std::string& pattern : patterns) {
        if (output.find(pattern) != std::string::npos) {
            return false;
        }
    }
    
    return true;
}

bool Child::sendPtyToClient() {
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
    if (cmsg == nullptr) {
        return false;
    }
    
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    *(int*)CMSG_DATA(cmsg) = masterPty_;
    
    ssize_t n = sendmsg(clientFd_, &msg, 0);
    return n == 1;
}

bool Child::handleTimeout(const Config& config, const std::string& command) {
    Seconds timeout = config.getCommandTimeout(command);
    
    fd_set fds;
    struct timeval tv;
    tv.tv_sec = timeout.count();
    tv.tv_usec = 0;
    
    FD_ZERO(&fds);
    FD_SET(clientFd_, &fds);
    
    int ret = select(clientFd_ + 1, &fds, nullptr, nullptr, &tv);
    
    if (ret < 0) {
        return false;
    } else if (ret == 0) {
        return false;
    }
    
    return true;
}

void Child::cleanup() {
    if (masterPty_ >= 0) {
        ::close(masterPty_);
        masterPty_ = -1;
    }
    if (slavePty_ >= 0) {
        ::close(slavePty_);
        slavePty_ = -1;
    }
    if (commandPid_ > 0) {
        commandPid_ = -1;
    }
}

} // namespace permup
