#include "utils.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <random>
#include <chrono>
#include <ctime>
#include <cstring>

namespace permup {

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        std::string trimmed = trim(token);
        if (!trimmed.empty()) {
            tokens.push_back(trimmed);
        }
    }
    return tokens;
}

std::string normalizeCommand(const std::string& cmd) {
    std::vector<std::string> parts = split(cmd, ' ');
    if (parts.empty()) return "";
    
    std::string program = parts[0];
    std::vector<std::string> flags;
    
    for (size_t i = 1; i < parts.size(); ++i) {
        const std::string& part = parts[i];
        if (part[0] == '-') {
            if (part.size() > 2 && part[1] != '-') {
                for (size_t j = 1; j < part.size(); ++j) {
                    flags.push_back(std::string("-") + part[j]);
                }
            } else {
                flags.push_back(part);
            }
        } else {
            flags.push_back(canonicalizePath(part));
        }
    }
    
    std::sort(flags.begin(), flags.end());
    
    std::string normalized = program;
    for (const std::string& flag : flags) {
        normalized += " " + flag;
    }
    
    return trim(normalized);
}

std::string canonicalizePath(const std::string& path) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) {
        return std::string(resolved);
    }
    return path;
}

bool isSubstringMatch(const std::string& command, const std::string& pattern) {
    std::string cmd = " " + command + " ";
    std::string pat = " " + pattern + " ";
    return cmd.find(pat) != std::string::npos;
}

bool isCommandMatch(const std::string& command, const std::string& pattern) {
    std::string normalizedCmd = normalizeCommand(command);
    std::string normalizedPattern = normalizeCommand(pattern);
    return isSubstringMatch(normalizedCmd, normalizedPattern);
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    return true;
}

bool mkdirRecursive(const std::string& path, mode_t mode) {
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                mkdir(current.c_str(), mode);
            }
            current += c;
        } else {
            current += c;
        }
    }
    mkdir(current.c_str(), mode);
    return true;
}

std::string getUsername(uid_t uid) {
    struct passwd* pw = getpwuid(uid);
    if (pw == nullptr) {
        return "";
    }
    return std::string(pw->pw_name);
}

uid_t getUserId(const std::string& username) {
    struct passwd* pw = getpwnam(username.c_str());
    if (pw == nullptr) {
        return static_cast<uid_t>(-1);
    }
    return pw->pw_uid;
}

std::vector<std::string> getUserGroups(const std::string& username) {
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

bool isRoot(const std::string& username) {
    return username == "root";
}

Seconds parseTimeString(const std::string& str) {
    std::string s = trim(str);
    if (s.empty()) return Seconds(0);
    
    size_t pos = 0;
    while (pos < s.length() && std::isdigit(s[pos])) {
        ++pos;
    }
    
    if (pos == 0) return Seconds(0);
    
    int value = std::stoi(s.substr(0, pos));
    std::string unit = s.substr(pos);
    unit = trim(unit);
    
    if (unit == "s" || unit == "sec" || unit == "second" || unit == "seconds") {
        return Seconds(value);
    } else if (unit == "m" || unit == "min" || unit == "minute" || unit == "minutes") {
        return Seconds(value * 60);
    } else if (unit == "h" || unit == "hour" || unit == "hours") {
        return Seconds(value * 3600);
    } else if (unit == "d" || unit == "day" || unit == "days") {
        return Seconds(value * 86400);
    }
    
    return Seconds(value);
}

bool isTimeInRange(const std::string& timeStr, const std::string& range) {
    if (timeStr.length() != 5 || timeStr[2] != ':') {
        return false;
    }
    
    int hour = std::stoi(timeStr.substr(0, 2));
    int minute = std::stoi(timeStr.substr(3, 2));
    int currentMinutes = hour * 60 + minute;
    
    size_t dashPos = range.find('-');
    if (dashPos == std::string::npos) {
        return false;
    }
    
    std::string startTime = trim(range.substr(0, dashPos));
    std::string endTime = trim(range.substr(dashPos + 1));
    
    if (startTime.length() != 5 || startTime[2] != ':' ||
        endTime.length() != 5 || endTime[2] != ':') {
        return false;
    }
    
    int startHour = std::stoi(startTime.substr(0, 2));
    int startMinute = std::stoi(startTime.substr(3, 2));
    int startMinutes = startHour * 60 + startMinute;
    
    int endHour = std::stoi(endTime.substr(0, 2));
    int endMinute = std::stoi(endTime.substr(3, 2));
    int endMinutes = endHour * 60 + endMinute;
    
    if (startMinutes <= endMinutes) {
        return currentMinutes >= startMinutes && currentMinutes <= endMinutes;
    } else {
        return currentMinutes >= startMinutes || currentMinutes <= endMinutes;
    }
}

bool isDayInRange(const std::string& dayStr, const std::string& range) {
    std::vector<std::string> days = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    
    std::vector<std::string> rangeDays = split(range, ',');
    
    for (const std::string& rangeDay : rangeDays) {
        size_t dashPos = rangeDay.find('-');
        if (dashPos != std::string::npos) {
            std::string start = trim(rangeDay.substr(0, dashPos));
            std::string end = trim(rangeDay.substr(dashPos + 1));
            
            auto startIt = std::find(days.begin(), days.end(), start);
            auto endIt = std::find(days.begin(), days.end(), end);
            
            if (startIt != days.end() && endIt != days.end()) {
                int startIdx = std::distance(days.begin(), startIt);
                int endIdx = std::distance(days.begin(), endIt);
                int currentIdx = std::distance(days.begin(), std::find(days.begin(), days.end(), dayStr));
                
                if (startIdx <= endIdx) {
                    if (currentIdx >= startIdx && currentIdx <= endIdx) {
                        return true;
                    }
                } else {
                    if (currentIdx >= startIdx || currentIdx <= endIdx) {
                        return true;
                    }
                }
            }
        } else {
            if (rangeDay == dayStr) {
                return true;
            }
        }
    }
    
    return false;
}

std::string getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&now_time);
    
    std::stringstream ss;
    ss << std::setw(2) << std::setfill('0') << tm->tm_hour << ":"
       << std::setw(2) << std::setfill('0') << tm->tm_min;
    return ss.str();
}

std::string getCurrentDay() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&now_time);
    
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return std::string(days[tm->tm_wday]);
}

std::string generateRandomString(size_t length) {
    static const char charset[] = 
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(gen)];
    }
    return result;
}

uint64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace permup