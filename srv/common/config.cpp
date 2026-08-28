#include "config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <cstdlib>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <algorithm>
#include <unistd.h>
#include <cstring>

namespace permup {

Config::Config() {
    defaultTimeout = Seconds(120);
}

bool Config::load(const std::string& configDir) {
    bool success = true;
    
    success &= loadPermissions(configDir + "/permup.cfg");
    success &= loadTimeouts(configDir + "/permdown.cfg");
    success &= loadShellPatterns(configDir + "/shell.rc");
    success &= loadTimeConfigs(configDir + "/permup.time");
    success &= loadSessionConfigs(configDir + "/permup.session");
    success &= loadRateLimitConfigs(configDir + "/permup.ratelimit");
    
    std::cerr << "DEBUG: Loaded " << groupConfigs.size() << " groups\n";
    for (const auto& g : groupConfigs) {
        std::cerr << "DEBUG: Group: " << g.first << "\n";
    }
    
    return success;
}

bool Config::loadPermissions(const std::string& path) {
    std::string content = readFile(path);
    if (content.empty()) {
        GroupConfig defaultConfig;
        defaultConfig.mode = GroupConfig::Mode::DARKLIST;
        groupConfigs["adm"] = defaultConfig;
        groupConfigs["wheel"] = defaultConfig;
        return true;
    }
    
    std::istringstream stream(content);
    std::string line;
    std::string currentGroup;
    GroupConfig currentConfig;
    bool inGroup = false;
    
    auto parseList = [&](const std::string& str, std::vector<std::string>& out) {
        size_t start = str.find('{');
        size_t end = str.find('}');
        if (start == std::string::npos || end == std::string::npos || end <= start) {
            return;
        }
        std::string items = str.substr(start + 1, end - start - 1);
        std::vector<std::string> parts = split(items, ',');
        for (std::string& item : parts) {
            item = trim(item);
            if (!item.empty()) {
                out.push_back(item);
            }
        }
    };
    
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        // Definicja grupy: "nazwa: {"
        if (!inGroup && line.find(':') != std::string::npos && line.find('{') != std::string::npos) {
            size_t pos = line.find(':');
            currentGroup = trim(line.substr(0, pos));
            currentConfig = GroupConfig();
            inGroup = true;
            std::cerr << "DEBUG: Parsing group: " << currentGroup << "\n";
            continue;
        }
        
        if (!inGroup) continue;
        
        // Koniec grupy
        if (line.find('}') != std::string::npos) {
            if (!currentGroup.empty()) {
                groupConfigs[currentGroup] = currentConfig;
                std::cerr << "DEBUG: Saved group: " << currentGroup << "\n";
            }
            inGroup = false;
            currentGroup.clear();
            continue;
        }
        
        // Klucz: "key: value"
        size_t pos = line.find(':');
        if (pos != std::string::npos) {
            std::string key = trim(line.substr(0, pos));
            std::string value = trim(line.substr(pos + 1));
            
            if (key == "mode") {
                if (value == "darklist" || value == "blacklist") {
                    currentConfig.mode = GroupConfig::Mode::DARKLIST;
                } else if (value == "whitelist") {
                    currentConfig.mode = GroupConfig::Mode::WHITELIST;
                }
            }
            else if (key == "list") {
                parseList(value, currentConfig.commands);
            }
            else if (key == "except") {
                parseList(value, currentConfig.except);
            }
            else if (key == "blocked_users" || key == "blockedUsers") {
                parseList(value, currentConfig.blockedUsers);
            }
            else if (key == "allowed_users" || key == "allowedUsers") {
                parseList(value, currentConfig.allowedUsers);
            }
        }
    }
    
    if (groupConfigs.empty()) {
        GroupConfig defaultConfig;
        defaultConfig.mode = GroupConfig::Mode::DARKLIST;
        groupConfigs["adm"] = defaultConfig;
        groupConfigs["wheel"] = defaultConfig;
        std::cerr << "WARNING: No groups loaded, added defaults\n";
    }
    
    return true;
}

bool Config::loadTimeouts(const std::string& path) {
    std::string content = readFile(path);
    if (content.empty()) {
        return true;
    }
    
    std::istringstream stream(content);
    std::string line;
    std::string currentCommand;
    Seconds currentTimeout;
    bool inCommand = false;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        if (line.find("default:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string value = trim(line.substr(pos + 1));
                defaultTimeout = parseTimeString(value);
            }
        } else if (line.find('{') != std::string::npos) {
            size_t pos = line.find('{');
            currentCommand = trim(line.substr(0, pos));
            inCommand = true;
        } else if (line.find('}') != std::string::npos) {
            if (inCommand && !currentCommand.empty()) {
                commandTimeouts[currentCommand] = currentTimeout;
            }
            inCommand = false;
            currentCommand.clear();
        } else if (inCommand) {
            std::string value = trim(line);
            if (!value.empty()) {
                currentTimeout = parseTimeString(value);
            }
        }
    }
    
    return true;
}

bool Config::loadShellPatterns(const std::string& path) {
    std::string content = readFile(path);
    if (content.empty()) {
        shellPatterns = {
            "[root@",
            "[user@",
            "$",
            "#",
            ">",
            "%",
            "bash-",
            "zsh-",
            "fish-",
            "sh-"
        };
        return true;
    }
    
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        shellPatterns.push_back(line);
    }
    
    return true;
}

bool Config::loadTimeConfigs(const std::string& path) {
    std::string content = readFile(path);
    if (content.empty()) {
        return true;
    }
    
    std::istringstream stream(content);
    std::string line;
    std::string currentGroup;
    TimeConfig currentConfig;
    bool inGroup = false;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        if (line.find('{') != std::string::npos && line.find(':') != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                currentGroup = trim(line.substr(0, pos));
                currentConfig = TimeConfig();
                inGroup = true;
            }
        } else if (line.find('}') != std::string::npos) {
            if (inGroup && !currentGroup.empty()) {
                timeConfigs[currentGroup] = currentConfig;
            }
            inGroup = false;
            currentGroup.clear();
        } else if (inGroup) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string key = trim(line.substr(0, pos));
                std::string value = trim(line.substr(pos + 1));
                
                if (key == "allowed") {
                    currentConfig.timeRange = value;
                } else if (key == "days") {
                    currentConfig.dayRange = value;
                }
            }
        }
    }
    
    return true;
}

bool Config::loadSessionConfigs(const std::string& path) {
    std::string content = readFile(path);
    if (content.empty()) {
        return true;
    }
    
    std::istringstream stream(content);
    std::string line;
    std::string currentGroup;
    SessionConfig currentConfig;
    bool inGroup = false;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        if (line.find('{') != std::string::npos && line.find(':') != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                currentGroup = trim(line.substr(0, pos));
                currentConfig = SessionConfig();
                inGroup = true;
            }
        } else if (line.find('}') != std::string::npos) {
            if (inGroup && !currentGroup.empty()) {
                sessionConfigs[currentGroup] = currentConfig;
            }
            inGroup = false;
            currentGroup.clear();
        } else if (inGroup) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string key = trim(line.substr(0, pos));
                std::string value = trim(line.substr(pos + 1));
                
                if (key == "session_time") {
                    currentConfig.sessionTime = parseTimeString(value);
                }
            }
        }
    }
    
    return true;
}

bool Config::loadRateLimitConfigs(const std::string& path) {
    std::string content = readFile(path);
    if (content.empty()) {
        RateLimitConfig defaultConfig;
        defaultConfig.maxAttempts = 3;
        defaultConfig.blockTime = Seconds(30);
        rateLimitConfigs["default"] = defaultConfig;
        return true;
    }
    
    std::istringstream stream(content);
    std::string line;
    std::string currentUser;
    RateLimitConfig currentConfig;
    bool inUser = false;
    bool hasDefault = false;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        if (line.find('{') != std::string::npos) {
            size_t pos = line.find('{');
            currentUser = trim(line.substr(0, pos));
            currentConfig = RateLimitConfig();
            inUser = true;
        } else if (line.find('}') != std::string::npos) {
            if (inUser && !currentUser.empty()) {
                rateLimitConfigs[currentUser] = currentConfig;
                if (currentUser == "default") {
                    hasDefault = true;
                }
            }
            inUser = false;
            currentUser.clear();
        } else if (inUser) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string key = trim(line.substr(0, pos));
                std::string value = trim(line.substr(pos + 1));
                
                if (key == "n" || key == "max_attempts") {
                    currentConfig.maxAttempts = std::stoi(value);
                } else if (key == "x" || key == "block_time") {
                    currentConfig.blockTime = parseTimeString(value);
                }
            }
        }
    }
    
    if (!hasDefault) {
        RateLimitConfig defaultConfig;
        defaultConfig.maxAttempts = 3;
        defaultConfig.blockTime = Seconds(30);
        rateLimitConfigs["default"] = defaultConfig;
    }
    
    return true;
}

bool Config::isCommandAllowed(const std::string& username, 
                             const std::string& command,
                             const std::string& targetUser) const {
    if (isRoot(username)) {
        return true;
    }
    
    std::vector<std::string> groups = getUserGroups(username);
    if (groups.empty()) {
        return false;
    }
    
    bool allowed = false;
    bool blocked = false;
    
    for (const std::string& group : groups) {
        auto it = groupConfigs.find(group);
        if (it == groupConfigs.end()) {
            continue;
        }
        
        const GroupConfig& config = it->second;
        
        if (!config.allowedUsers.empty()) {
            if (std::find(config.allowedUsers.begin(), config.allowedUsers.end(), 
                         targetUser) == config.allowedUsers.end()) {
                continue;
            }
        } else if (!config.blockedUsers.empty()) {
            if (std::find(config.blockedUsers.begin(), config.blockedUsers.end(), 
                         targetUser) != config.blockedUsers.end()) {
                continue;
            }
        }
        
        bool commandAllowed = false;
        bool commandBlocked = false;
        
        for (const std::string& pattern : config.except) {
            if (isCommandMatch(command, pattern)) {
                if (config.mode == GroupConfig::Mode::DARKLIST) {
                    commandAllowed = true;
                } else {
                    commandBlocked = true;
                }
            }
        }
        
        if (config.mode == GroupConfig::Mode::DARKLIST) {
            if (config.commands.empty()) {
                if (!commandBlocked) {
                    commandAllowed = true;
                }
            } else {
                bool inDarklist = false;
                for (const std::string& pattern : config.commands) {
                    if (isCommandMatch(command, pattern)) {
                        inDarklist = true;
                        break;
                    }
                }
                if (!inDarklist && !commandBlocked) {
                    commandAllowed = true;
                }
            }
        } else if (config.mode == GroupConfig::Mode::WHITELIST) {
            if (config.commands.empty()) {
                continue;
            }
            
            bool inWhitelist = false;
            for (const std::string& pattern : config.commands) {
                if (isCommandMatch(command, pattern)) {
                    inWhitelist = true;
                    break;
                }
            }
            if (inWhitelist && !commandBlocked) {
                commandAllowed = true;
            }
        }
        
        if (commandAllowed) {
            allowed = true;
        }
        if (commandBlocked) {
            blocked = true;
        }
    }
    
    return allowed && !blocked;
}

bool Config::isTargetUserAllowed(const std::string& username,
                                const std::string& targetUser) const {
    if (isRoot(username)) {
        return true;
    }
    
    std::vector<std::string> groups = getUserGroups(username);
    if (groups.empty()) {
        return false;
    }
    
    for (const std::string& group : groups) {
        auto it = groupConfigs.find(group);
        if (it == groupConfigs.end()) {
            continue;
        }
        
        const GroupConfig& config = it->second;
        
        if (!config.allowedUsers.empty()) {
            if (std::find(config.allowedUsers.begin(), config.allowedUsers.end(), 
                         targetUser) != config.allowedUsers.end()) {
                return true;
            }
        } else if (!config.blockedUsers.empty()) {
            if (std::find(config.blockedUsers.begin(), config.blockedUsers.end(), 
                         targetUser) == config.blockedUsers.end()) {
                return true;
            }
        } else {
            return true;
        }
    }
    
    return false;
}

bool Config::isAuthUserAllowed(const std::string& username,
                              const std::string& authUser) const {
    if (isRoot(username) || isRoot(authUser)) {
        return true;
    }
    
    return isTargetUserAllowed(username, authUser);
}

std::vector<std::string> Config::getAllowedTargetUsers(const std::string& username) const {
    std::vector<std::string> result;
    
    if (isRoot(username)) {
        setpwent();
        struct passwd* pw;
        while ((pw = getpwent()) != nullptr) {
            result.push_back(std::string(pw->pw_name));
        }
        endpwent();
        return result;
    }
    
    std::vector<std::string> groups = getUserGroups(username);
    if (groups.empty()) {
        result.push_back("root");
        return result;
    }
    
    std::map<std::string, bool> allowedMap;
    bool hasAnyRestriction = false;
    
    for (const std::string& group : groups) {
        auto it = groupConfigs.find(group);
        if (it == groupConfigs.end()) {
            continue;
        }
        
        const GroupConfig& config = it->second;
        
        if (!config.allowedUsers.empty()) {
            hasAnyRestriction = true;
            for (const std::string& user : config.allowedUsers) {
                allowedMap[user] = true;
            }
        } else if (!config.blockedUsers.empty()) {
            hasAnyRestriction = true;
            setpwent();
            struct passwd* pw;
            while ((pw = getpwent()) != nullptr) {
                std::string user = pw->pw_name;
                if (std::find(config.blockedUsers.begin(), config.blockedUsers.end(), 
                             user) == config.blockedUsers.end()) {
                    allowedMap[user] = true;
                }
            }
            endpwent();
        } else {
            setpwent();
            struct passwd* pw;
            while ((pw = getpwent()) != nullptr) {
                allowedMap[std::string(pw->pw_name)] = true;
            }
            endpwent();
        }
    }
    
    if (!hasAnyRestriction) {
        setpwent();
        struct passwd* pw;
        while ((pw = getpwent()) != nullptr) {
            result.push_back(std::string(pw->pw_name));
        }
        endpwent();
        return result;
    }
    
    for (const auto& pair : allowedMap) {
        result.push_back(pair.first);
    }
    
    return result;
}

std::vector<std::string> Config::getAllowedAuthUsers(const std::string& username) const {
    if (isRoot(username)) {
        setpwent();
        std::vector<std::string> result;
        struct passwd* pw;
        while ((pw = getpwent()) != nullptr) {
            result.push_back(std::string(pw->pw_name));
        }
        endpwent();
        return result;
    }
    
    return getAllowedTargetUsers(username);
}

bool Config::isTimeAllowed(const std::string& username) const {
    if (isRoot(username)) {
        return true;
    }
    
    std::vector<std::string> groups = getUserGroups(username);
    if (groups.empty()) {
        return true;
    }
    
    for (const std::string& group : groups) {
        auto it = timeConfigs.find(group);
        if (it == timeConfigs.end()) {
            continue;
        }
        
        const TimeConfig& config = it->second;
        if (checkTimeConfig(group, config)) {
            return true;
        }
    }
    
    return false;
}

bool Config::checkTimeConfig(const std::string& groupName, const TimeConfig& config) const {
    (void)groupName;
    std::string currentTime = getCurrentTime();
    std::string currentDay = getCurrentDay();
    
    if (!config.timeRange.empty()) {
        if (!isTimeInRange(currentTime, config.timeRange)) {
            return false;
        }
    }
    
    if (!config.dayRange.empty()) {
        if (!isDayInRange(currentDay, config.dayRange)) {
            return false;
        }
    }
    
    return true;
}

Seconds Config::getCommandTimeout(const std::string& command) const {
    auto it = commandTimeouts.find(command);
    if (it != commandTimeouts.end()) {
        return it->second;
    }
    
    for (const auto& pair : commandTimeouts) {
        if (isCommandMatch(command, pair.first)) {
            return pair.second;
        }
    }
    
    return defaultTimeout;
}

Seconds Config::getSessionTime(const std::string& username) const {
    std::vector<std::string> groups = getUserGroups(username);
    for (const std::string& group : groups) {
        auto it = sessionConfigs.find(group);
        if (it != sessionConfigs.end()) {
            return it->second.sessionTime;
        }
    }
    
    return Seconds(0);
}

RateLimitConfig Config::getRateLimitConfig(const std::string& username) const {
    auto it = rateLimitConfigs.find(username);
    if (it != rateLimitConfigs.end()) {
        return it->second;
    }
    
    auto defaultIt = rateLimitConfigs.find("default");
    if (defaultIt != rateLimitConfigs.end()) {
        return defaultIt->second;
    }
    
    RateLimitConfig defaultConfig;
    defaultConfig.maxAttempts = 3;
    defaultConfig.blockTime = Seconds(30);
    return defaultConfig;
}

const std::vector<std::string>& Config::getShellPatterns() const {
    return shellPatterns;
}

std::vector<std::string> Config::getUserGroups(const std::string& username) const {
    std::vector<std::string> groups;
    
    struct passwd* pw = getpwnam(username.c_str());
    if (pw == nullptr) {
        return groups;
    }
    
    int ngroups = 0;
    getgrouplist(username.c_str(), pw->pw_gid, nullptr, &ngroups);
    
    if (ngroups > 0) {
        std::vector<gid_t> gids(ngroups);
        getgrouplist(username.c_str(), pw->pw_gid, gids.data(), &ngroups);
        
        for (int i = 0; i < ngroups; ++i) {
            struct group* gr = getgrgid(gids[i]);
            if (gr != nullptr) {
                groups.push_back(std::string(gr->gr_name));
            }
        }
    }
    
    return groups;
}

} // namespace permup
