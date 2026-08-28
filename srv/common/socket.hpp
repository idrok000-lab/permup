#ifndef PERMUP_SOCKET_HPP
#define PERMUP_SOCKET_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace permup {

class UnixSocket {
public:
    UnixSocket();
    ~UnixSocket();
    
    bool createServer(const std::string& socketPath, mode_t mode = 0600);
    bool connectClient(const std::string& socketPath);
    int acceptConnection();
    void closeSocket();
    void removeSocketFile();
    
    bool sendMessage(const std::vector<uint8_t>& data);
    bool sendMessageWithFd(const std::vector<uint8_t>& data, int fd);
    bool receiveMessage(std::vector<uint8_t>& data);
    bool receiveMessageWithFd(std::vector<uint8_t>& data, int& fd);
    
    bool isConnected() const { return fd_ >= 0; }
    int getFd() const { return fd_; }
    
private:
    int fd_;
    std::string socketPath_;
    
    bool sendFd(int fd, int targetFd);
    int receiveFd(int fd);
};

} // namespace permup

#endif // PERMUP_SOCKET_HPP