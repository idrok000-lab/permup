#ifndef PERMUP_UTILS_HPP
#define PERMUP_UTILS_HPP

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <sys/types.h>

namespace permup {

using Seconds = std::chrono::seconds;
using Milliseconds = std::chrono::milliseconds;

std::string trim(const std::string& str);
std::vector<std::string> split(const std::string& str, char delimiter);
std::string normalizeCommand(const std::string& cmd);
std::string canonicalizePath(const std::string& path);
bool isSubstringMatch(const std::string& command, const std::string& pattern);
bool isCommandMatch(const std::string& command, const std::string& pattern);

bool fileExists(const std::string& path);
std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& content);
bool mkdirRecursive(const std::string& path, mode_t mode);

std::string getUsername(uid_t uid);
uid_t getUserId(const std::string& username);
std::vector<std::string> getUserGroups(const std::string& username);
bool isRoot(const std::string& username);

Seconds parseTimeString(const std::string& str);
bool isTimeInRange(const std::string& timeStr, const std::string& range);
bool isDayInRange(const std::string& dayStr, const std::string& range);
std::string getCurrentTime();
std::string getCurrentDay();

std::string generateRandomString(size_t length);
uint64_t getCurrentTimestamp();

} // namespace permup

#endif // PERMUP_UTILS_HPP