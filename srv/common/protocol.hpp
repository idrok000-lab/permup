#ifndef PERMUP_PROTOCOL_HPP
#define PERMUP_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

namespace permup {

enum class MessageType : uint32_t {
    LIST_USERS_REQUEST = 0x0001,
    EXECUTE_REQUEST = 0x0002,
    
    LIST_USERS_RESPONSE = 0x8001,
    EXECUTE_RESPONSE = 0x8002,
    ERROR_RESPONSE = 0x8003,
    
    PTY_FD = 0x9001,
};

struct MessageHeader {
    uint32_t magic;
    MessageType type;
    uint32_t length;
    uint32_t flags;
    
    MessageHeader() : magic(0x504D5550), type(MessageType::ERROR_RESPONSE), 
                     length(0), flags(0) {}
};

struct ListUsersRequest {
};

struct ListUsersResponse {
    uint32_t targetUserCount;
    uint32_t authUserCount;
};

struct ExecuteRequest {
    uint32_t targetUserLen;
    uint32_t authUserLen;
    uint32_t commandLen;
    uint32_t passwordLen;
};

struct ExecuteResponse {
    uint32_t pid;
    uint32_t status;
};

struct ErrorResponse {
    uint32_t errorCode;
    uint32_t errorMsgLen;
};

enum class ErrorCode : uint32_t {
    SUCCESS = 0,
    INVALID_USER = 1,
    INVALID_AUTH = 2,
    INVALID_COMMAND = 3,
    AUTH_FAILED = 4,
    PERMISSION_DENIED = 5,
    TIME_RESTRICTED = 6,
    SHELL_BLOCKED = 7,
    INTERNAL_ERROR = 8,
    TIMEOUT = 9,
    CONNECTION_ERROR = 10,
};

class Protocol {
public:
    static constexpr uint32_t MAGIC = 0x504D5550;
    static constexpr size_t MAX_MESSAGE_SIZE = 64 * 1024;
    static constexpr size_t MAX_PASSWORD_LEN = 256;
    
    static std::vector<uint8_t> serializeMessage(const MessageHeader& header,
                                                 const std::vector<uint8_t>& payload);
    
    static bool deserializeMessage(const std::vector<uint8_t>& data,
                                  MessageHeader& header,
                                  std::vector<uint8_t>& payload);
    
    static std::vector<uint8_t> buildListUsersRequest();
    static std::vector<uint8_t> buildExecuteRequest(const std::string& targetUser,
                                                   const std::string& authUser,
                                                   const std::string& command,
                                                   const std::string& password);
    
    static bool parseListUsersResponse(const std::vector<uint8_t>& payload,
                                      std::vector<std::string>& targetUsers,
                                      std::vector<std::string>& authUsers);
    
    static std::vector<uint8_t> buildListUsersResponse(const std::vector<std::string>& targetUsers,
                                                      const std::vector<std::string>& authUsers);
    
    static std::vector<uint8_t> buildErrorResponse(ErrorCode code,
                                                   const std::string& message);
    
    static bool parseErrorResponse(const std::vector<uint8_t>& payload,
                                  ErrorCode& code,
                                  std::string& message);
    
    static std::vector<uint8_t> buildExecuteResponse(uint32_t pid, uint32_t status);
};

} // namespace permup

#endif // PERMUP_PROTOCOL_HPP