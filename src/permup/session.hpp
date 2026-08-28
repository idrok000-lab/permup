#ifndef PERMUP_SESSION_HPP
#define PERMUP_SESSION_HPP

#include <string>
#include <map>
#include <chrono>
#include "../common/utils.hpp"

namespace permup {

struct Session {
    std::string password;
    uint64_t expiresAt;
    
    Session() : expiresAt(0) {}
    Session(const std::string& pass, Seconds duration) {
        password = pass;
        expiresAt = getCurrentTimestamp() + duration.count();
    }
    
    bool isValid() const {
        return getCurrentTimestamp() < expiresAt;
    }
};

class SessionManager {
public:
    SessionManager();
    ~SessionManager();
    
    bool hasValidSession(const std::string& caller,
                        const std::string& targetUser,
                        const std::string& authUser) const;
    
    std::string getPassword(const std::string& caller,
                           const std::string& targetUser,
                           const std::string& authUser) const;
    
    void storeSession(const std::string& caller,
                     const std::string& targetUser,
                     const std::string& authUser,
                     const std::string& password,
                     Seconds duration);
    
    void invalidateSession(const std::string& caller,
                          const std::string& targetUser,
                          const std::string& authUser);
    
    void cleanupExpiredSessions();
    
private:
    struct SessionKey {
        std::string caller;
        std::string targetUser;
        std::string authUser;
        
        bool operator<(const SessionKey& other) const;
        bool operator==(const SessionKey& other) const;
    };
    
    std::map<SessionKey, Session> sessions_;
};

} // namespace permup

#endif // PERMUP_SESSION_HPP