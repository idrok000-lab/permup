#include "session.hpp"
#include <functional>

namespace permup {

SessionManager::SessionManager() {}

SessionManager::~SessionManager() {}

bool SessionManager::SessionKey::operator<(const SessionKey& other) const {
    if (caller != other.caller) return caller < other.caller;
    if (targetUser != other.targetUser) return targetUser < other.targetUser;
    return authUser < other.authUser;
}

bool SessionManager::SessionKey::operator==(const SessionKey& other) const {
    return caller == other.caller && targetUser == other.targetUser && authUser == other.authUser;
}

bool SessionManager::hasValidSession(const std::string& caller,
                                    const std::string& targetUser,
                                    const std::string& authUser) const {
    SessionKey key{caller, targetUser, authUser};
    auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        return false;
    }
    return it->second.isValid();
}

std::string SessionManager::getPassword(const std::string& caller,
                                       const std::string& targetUser,
                                       const std::string& authUser) const {
    SessionKey key{caller, targetUser, authUser};
    auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        return "";
    }
    if (!it->second.isValid()) {
        return "";
    }
    return it->second.password;
}

void SessionManager::storeSession(const std::string& caller,
                                 const std::string& targetUser,
                                 const std::string& authUser,
                                 const std::string& password,
                                 Seconds duration) {
    SessionKey key{caller, targetUser, authUser};
    sessions_[key] = Session(password, duration);
    cleanupExpiredSessions();
}

void SessionManager::invalidateSession(const std::string& caller,
                                      const std::string& targetUser,
                                      const std::string& authUser) {
    SessionKey key{caller, targetUser, authUser};
    sessions_.erase(key);
}

void SessionManager::cleanupExpiredSessions() {
    auto it = sessions_.begin();
    while (it != sessions_.end()) {
        if (!it->second.isValid()) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace permup