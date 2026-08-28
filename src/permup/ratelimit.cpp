#include "ratelimit.hpp"
#include <iostream>

namespace permup {

RateLimiter::RateLimiter() {}

RateLimiter::~RateLimiter() {}

bool RateLimiter::checkLimit(const std::string& username, const RateLimitConfig& config) {
    cleanupExpired();
    
    auto it = states_.find(username);
    if (it == states_.end()) {
        return true;
    }
    
    if (it->second.blockedUntil > 0 && getCurrentTimestamp() < it->second.blockedUntil) {
        uint64_t remaining = it->second.blockedUntil - getCurrentTimestamp();
        std::cout << "Too many failed attempts. Try again in " 
                  << remaining << "s.\n";
        return false;
    }
    
    return true;
}

void RateLimiter::recordFailedAttempt(const std::string& username) {
    auto it = states_.find(username);
    if (it == states_.end()) {
        RateLimitState state;
        state.attempts = 1;
        states_[username] = state;
        return;
    }
    
    it->second.attempts++;
}

void RateLimiter::resetAttempts(const std::string& username) {
    auto it = states_.find(username);
    if (it != states_.end()) {
        it->second.attempts = 0;
        it->second.blockedUntil = 0;
    }
}

void RateLimiter::cleanupExpired() {
    auto it = states_.begin();
    while (it != states_.end()) {
        if (it->second.blockedUntil > 0 && getCurrentTimestamp() >= it->second.blockedUntil) {
            it->second.blockedUntil = 0;
            it->second.attempts = 0;
        }
        ++it;
    }
}

} // namespace permup