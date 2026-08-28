#ifndef PERMUP_RATELIMIT_HPP
#define PERMUP_RATELIMIT_HPP

#include <string>
#include <map>
#include "../common/utils.hpp"
#include "../common/config.hpp"

namespace permup {

struct RateLimitState {
    int attempts;
    uint64_t blockedUntil;
    
    RateLimitState() : attempts(0), blockedUntil(0) {}
};

class RateLimiter {
public:
    RateLimiter();
    ~RateLimiter();
    
    bool checkLimit(const std::string& username, const RateLimitConfig& config);
    void recordFailedAttempt(const std::string& username);
    void resetAttempts(const std::string& username);
    void cleanupExpired();
    
private:
    std::map<std::string, RateLimitState> states_;
};

} // namespace permup

#endif // PERMUP_RATELIMIT_HPP