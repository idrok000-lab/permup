#ifndef PERMUP_CLIENT_HPP
#define PERMUP_CLIENT_HPP

#include <string>
#include <vector>
#include <memory>
#include "../common/config.hpp"
#include "../common/socket.hpp"
#include "session.hpp"
#include "ratelimit.hpp"

namespace permup {

class Client {
public:
    Client();
    ~Client();
    
    bool initialize(const std::string& configDir = "/etc/permup");
    int run(const std::string& targetUser,
            const std::string& authUser,
            const std::string& command,
            bool listOnly = false);
    
private:
    std::unique_ptr<Config> config_;
    std::unique_ptr<UnixSocket> socket_;
    std::unique_ptr<SessionManager> sessionManager_;
    std::unique_ptr<RateLimiter> rateLimiter_;
    
    std::string username_;
    std::vector<std::string> allowedTargetUsers_;
    std::vector<std::string> allowedAuthUsers_;
    
    bool connectToDaemon();
    bool fetchAllowedUsers();
    bool checkPermissions(const std::string& targetUser, const std::string& authUser);
    bool checkRateLimit();
    std::string getPassword(const std::string& targetUser, const std::string& authUser);
    bool executeCommand(const std::string& targetUser,
                        const std::string& authUser,
                        const std::string& command,
                        const std::string& password);
    void handlePty(int masterPty);
    void printAllowedUsers() const;
};

} // namespace permup

#endif // PERMUP_CLIENT_HPP