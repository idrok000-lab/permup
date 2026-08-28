#include "protocol.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <algorithm>

namespace permup {

std::vector<uint8_t> Protocol::serializeMessage(const MessageHeader& header,
                                               const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> data(sizeof(MessageHeader) + payload.size());
    
    MessageHeader* hdr = reinterpret_cast<MessageHeader*>(data.data());
    hdr->magic = htonl(header.magic);
    hdr->type = static_cast<MessageType>(htonl(static_cast<uint32_t>(header.type)));
    hdr->length = htonl(header.length);
    hdr->flags = htonl(header.flags);
    
    if (!payload.empty()) {
        std::memcpy(data.data() + sizeof(MessageHeader), payload.data(), payload.size());
    }
    
    return data;
}

bool Protocol::deserializeMessage(const std::vector<uint8_t>& data,
                                 MessageHeader& header,
                                 std::vector<uint8_t>& payload) {
    if (data.size() < sizeof(MessageHeader)) {
        return false;
    }
    
    const MessageHeader* hdr = reinterpret_cast<const MessageHeader*>(data.data());
    header.magic = ntohl(hdr->magic);
    header.type = static_cast<MessageType>(ntohl(static_cast<uint32_t>(hdr->type)));
    header.length = ntohl(hdr->length);
    header.flags = ntohl(hdr->flags);
    
    if (header.magic != MAGIC) {
        return false;
    }
    
    if (header.length > MAX_MESSAGE_SIZE) {
        return false;
    }
    
    payload.clear();
    if (header.length > 0) {
        size_t payloadStart = sizeof(MessageHeader);
        if (data.size() < payloadStart + header.length) {
            return false;
        }
        payload.assign(data.begin() + payloadStart, 
                      data.begin() + payloadStart + header.length);
    }
    
    return true;
}

std::vector<uint8_t> Protocol::buildListUsersRequest() {
    MessageHeader header;
    header.type = MessageType::LIST_USERS_REQUEST;
    header.length = 0;
    header.flags = 0;
    return serializeMessage(header, {});
}

std::vector<uint8_t> Protocol::buildExecuteRequest(const std::string& targetUser,
                                                  const std::string& authUser,
                                                  const std::string& command,
                                                  const std::string& password) {
    if (password.length() > MAX_PASSWORD_LEN) {
        return {};
    }
    
    ExecuteRequest req;
    req.targetUserLen = htonl(targetUser.length());
    req.authUserLen = htonl(authUser.length());
    req.commandLen = htonl(command.length());
    req.passwordLen = htonl(password.length());
    
    size_t payloadSize = sizeof(ExecuteRequest) + 
                         targetUser.length() + 
                         authUser.length() + 
                         command.length() + 
                         password.length();
    
    std::vector<uint8_t> payload(payloadSize);
    size_t offset = 0;
    
    std::memcpy(payload.data() + offset, &req, sizeof(ExecuteRequest));
    offset += sizeof(ExecuteRequest);
    
    std::memcpy(payload.data() + offset, targetUser.c_str(), targetUser.length());
    offset += targetUser.length();
    
    std::memcpy(payload.data() + offset, authUser.c_str(), authUser.length());
    offset += authUser.length();
    
    std::memcpy(payload.data() + offset, command.c_str(), command.length());
    offset += command.length();
    
    std::memcpy(payload.data() + offset, password.c_str(), password.length());
    
    MessageHeader header;
    header.type = MessageType::EXECUTE_REQUEST;
    header.length = payload.size();
    header.flags = 0;
    
    return serializeMessage(header, payload);
}

bool Protocol::parseListUsersResponse(const std::vector<uint8_t>& payload,
                                     std::vector<std::string>& targetUsers,
                                     std::vector<std::string>& authUsers) {
    if (payload.size() < sizeof(ListUsersResponse)) {
        return false;
    }
    
    const ListUsersResponse* resp = reinterpret_cast<const ListUsersResponse*>(payload.data());
    uint32_t targetCount = ntohl(resp->targetUserCount);
    uint32_t authCount = ntohl(resp->authUserCount);
    
    size_t offset = sizeof(ListUsersResponse);
    
    targetUsers.clear();
    for (uint32_t i = 0; i < targetCount; ++i) {
        if (offset >= payload.size()) return false;
        uint32_t len = 0;
        if (offset + 4 > payload.size()) return false;
        std::memcpy(&len, payload.data() + offset, 4);
        len = ntohl(len);
        offset += 4;
        
        if (offset + len > payload.size()) return false;
        std::string user(reinterpret_cast<const char*>(payload.data() + offset), len);
        targetUsers.push_back(user);
        offset += len;
    }
    
    authUsers.clear();
    for (uint32_t i = 0; i < authCount; ++i) {
        if (offset >= payload.size()) return false;
        uint32_t len = 0;
        if (offset + 4 > payload.size()) return false;
        std::memcpy(&len, payload.data() + offset, 4);
        len = ntohl(len);
        offset += 4;
        
        if (offset + len > payload.size()) return false;
        std::string user(reinterpret_cast<const char*>(payload.data() + offset), len);
        authUsers.push_back(user);
        offset += len;
    }
    
    return true;
}

std::vector<uint8_t> Protocol::buildListUsersResponse(const std::vector<std::string>& targetUsers,
                                                     const std::vector<std::string>& authUsers) {
    size_t payloadSize = sizeof(ListUsersResponse);
    
    for (const std::string& user : targetUsers) {
        payloadSize += 4 + user.length();
    }
    
    for (const std::string& user : authUsers) {
        payloadSize += 4 + user.length();
    }
    
    std::vector<uint8_t> payload(payloadSize);
    ListUsersResponse* resp = reinterpret_cast<ListUsersResponse*>(payload.data());
    resp->targetUserCount = htonl(targetUsers.size());
    resp->authUserCount = htonl(authUsers.size());
    
    size_t offset = sizeof(ListUsersResponse);
    
    for (const std::string& user : targetUsers) {
        uint32_t len = htonl(user.length());
        std::memcpy(payload.data() + offset, &len, 4);
        offset += 4;
        std::memcpy(payload.data() + offset, user.c_str(), user.length());
        offset += user.length();
    }
    
    for (const std::string& user : authUsers) {
        uint32_t len = htonl(user.length());
        std::memcpy(payload.data() + offset, &len, 4);
        offset += 4;
        std::memcpy(payload.data() + offset, user.c_str(), user.length());
        offset += user.length();
    }
    
    MessageHeader header;
    header.type = MessageType::LIST_USERS_RESPONSE;
    header.length = payload.size();
    header.flags = 0;
    
    return serializeMessage(header, payload);
}

std::vector<uint8_t> Protocol::buildErrorResponse(ErrorCode code,
                                                 const std::string& message) {
    ErrorResponse resp;
    resp.errorCode = htonl(static_cast<uint32_t>(code));
    resp.errorMsgLen = htonl(message.length());
    
    size_t payloadSize = sizeof(ErrorResponse) + message.length();
    std::vector<uint8_t> payload(payloadSize);
    
    std::memcpy(payload.data(), &resp, sizeof(ErrorResponse));
    std::memcpy(payload.data() + sizeof(ErrorResponse), 
               message.c_str(), message.length());
    
    MessageHeader header;
    header.type = MessageType::ERROR_RESPONSE;
    header.length = payload.size();
    header.flags = 0;
    
    return serializeMessage(header, payload);
}

bool Protocol::parseErrorResponse(const std::vector<uint8_t>& payload,
                                 ErrorCode& code,
                                 std::string& message) {
    if (payload.size() < sizeof(ErrorResponse)) {
        return false;
    }
    
    const ErrorResponse* resp = reinterpret_cast<const ErrorResponse*>(payload.data());
    code = static_cast<ErrorCode>(ntohl(resp->errorCode));
    uint32_t msgLen = ntohl(resp->errorMsgLen);
    
    if (payload.size() < sizeof(ErrorResponse) + msgLen) {
        return false;
    }
    
    message = std::string(reinterpret_cast<const char*>(payload.data() + sizeof(ErrorResponse)), 
                         msgLen);
    
    return true;
}

std::vector<uint8_t> Protocol::buildExecuteResponse(uint32_t pid, uint32_t status) {
    ExecuteResponse resp;
    resp.pid = htonl(pid);
    resp.status = htonl(status);
    
    std::vector<uint8_t> payload(sizeof(ExecuteResponse));
    std::memcpy(payload.data(), &resp, sizeof(ExecuteResponse));
    
    MessageHeader header;
    header.type = MessageType::EXECUTE_RESPONSE;
    header.length = payload.size();
    header.flags = 0;
    
    return serializeMessage(header, payload);
}

} // namespace permup