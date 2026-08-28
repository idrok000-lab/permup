#ifndef PERMUP_CONFIG_HPP
#define PERMUP_CONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <chrono>
#include "utils.hpp"

namespace permup {

struct GroupConfig {
    enum class Mode {
        DARKLIST,
        WHITELIST
    };
    
    Mode mode;
    std::vector<std::string> commands;
    std::vector<std::string> except;
    std::vector<std::string> blockedUsers;
    std::vector<std::string> allowedUsers;
    
    GroupConfig() : mode(Mode::DARKLIST) {}
};

struct TimeConfig {
    std::string timeRange;
    std::string dayRange;
};

struct SessionConfig {
    Seconds sessionTime;
    SessionConfig() : sessionTime(Seconds(0)) {}
};

struct RateLimitConfig {
    int maxAttempts;
    Seconds blockTime;
    RateLimitConfig() : maxAttempts(3), blockTime(Seconds(30)) {}
};

class Config {
public:
    Config();
    ~Config() = default;
    
    bool load(const std::string& configDir = "/etc/permup");
    
    bool isCommandAllowed(const std::string& username, 
                          const std::string& command,
                          const std::string& targetUser) const;
    
    bool isTargetUserAllowed(const std::string& username,
                             const std::string& targetUser) const;
    
    bool isAuthUserAllowed(const std::string& username,
                           const std::string& authUser) const;
    
    std::vector<std::string> getAllowedTargetUsers(const std::string& username) const;
    std::vector<std::string> getAllowedAuthUsers(const std::string& username) const;
    
    bool isTimeAllowed(const std::string& username) const;
    Seconds getCommandTimeout(const std::string& command) const;
    
    Seconds getSessionTime(const std::string& username) const;
    
    RateLimitConfig getRateLimitConfig(const std::string& username) const;
    
    const std::vector<std::string>& getShellPatterns() const;
    
private:
    std::map<std::string, GroupConfig> groupConfigs;
    std::map<std::string, TimeConfig> timeConfigs;
    std::map<std::string, SessionConfig> sessionConfigs;
    std::map<std::string, RateLimitConfig> rateLimitConfigs;
    std::map<std::string, Seconds> commandTimeouts;
    Seconds defaultTimeout;
    std::vector<std::string> shellPatterns;
    
    bool loadPermissions(const std::string& path);
    bool loadTimeouts(const std::string& path);
    bool loadShellPatterns(const std::string& path);
    bool loadTimeConfigs(const std::string& path);
    bool loadSessionConfigs(const std::string& path);
    bool loadRateLimitConfigs(const std::string& path);
    
    std::vector<std::string> getUserGroups(const std::string& username) const;
    bool checkTimeConfig(const std::string& groupName, const TimeConfig& config) const;
};

} // namespace permup

#endif // PERMUP_CONFIG_HPP