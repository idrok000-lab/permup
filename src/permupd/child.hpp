#ifndef PERMUP_CHILD_HPP
#define PERMUP_CHILD_HPP

#include <string>
#include <vector>
#include <memory>
#include "../common/config.hpp"
#include "../common/utils.hpp"

namespace permup {

class Child {
public:
    Child();
    ~Child();
    
    bool run(int clientFd, 
             const std::string& targetUser,
             const std::string& authUser,
             const std::string& command,
             const std::string& callingUser,
             const Config& config);
    
private:
    int clientFd_;
    int masterPty_;
    int slavePty_;
    pid_t childPid_;
    pid_t commandPid_;
    
    bool setupPty();
    bool executeCommand(const std::string& targetUser, const std::string& command);
    bool checkShellPatterns(const Config& config, const std::string& authUser);
    bool sendPtyToClient();
    bool handleTimeout(const Config& config, const std::string& command);
    void cleanup();
};

} // namespace permup

#endif // PERMUP_CHILD_HPP