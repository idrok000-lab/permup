#ifndef PERMUP_DAEMON_HPP
#define PERMUP_DAEMON_HPP

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <cstdint>
#include "../common/config.hpp"
#include "../common/socket.hpp"

namespace permup {

class Daemon {
public:
    Daemon();
    ~Daemon();
    
    bool initialize(const std::string& configDir = "/etc/permup");
    int run();
    void stop();
    
private:
    std::unique_ptr<Config> config_;
    std::unique_ptr<UnixSocket> serverSocket_;
    bool running_;
    std::string socketPath_;
    
    // NOWA WERSJA – 2 ARGUMENTY
    void processClientData(int fd, std::vector<uint8_t>& buffer);
    void handleListUsersRequest(int fd);
    void handleExecuteRequest(int fd, const std::vector<uint8_t>& payload);
    
    // STARE – nieużywane, ale zostawiam dla kompatybilności
    void handleClient(int clientFd);
    void handleListUsersRequest(int clientFd, const std::string& username);
    void handleExecuteRequest(int clientFd, const std::string& username,
                            const std::string& targetUser,
                            const std::string& authUser,
                            const std::string& command,
                            const std::string& password);
    
    bool authenticateUser(const std::string& username, const std::string& password);
    void forkChild(int clientFd, const std::string& targetUser,
                  const std::string& authUser,
                  const std::string& command,
                  const std::string& username);
    
    void reapChildren();
    static void signalHandler(int sig);
};

} // namespace permup

#endif // PERMUP_DAEMON_HPP