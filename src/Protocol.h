#pragma once

#include <cstdint>

// 네트워크 프로토콜 관련 상수 및 구조체
namespace Protocol {
    // 최대 메시지 크기 (10MB)
    constexpr uint32_t MAX_MESSAGE_SIZE = 10 * 1024 * 1024;
    
    // 현재 처리 중인 메시지 크기 (네트워크 바이트 순서)
    inline uint32_t current_message_size_ = 0;
    
    // 메시지 타입 열거형
    enum class MessageType : uint8_t {
        LOGIN = 1,
        LOGIN_RESPONSE = 2,
        REGISTER_ITEM = 3,
        REGISTER_ITEM_RESPONSE = 4,
        SEARCH_ITEMS = 5,
        SEARCH_ITEMS_RESPONSE = 6,
        PURCHASE_ITEM = 7,
        PURCHASE_ITEM_RESPONSE = 8,
        CANCEL_LISTING = 9,
        CANCEL_LISTING_RESPONSE = 10,
        ERROR = 255
    };
    
    // 응답 상태 열거형
    enum class ResponseStatus : uint8_t {
        SUCCESS = 0,
        ERROR = 1
    };
    
    // 네트워크 바이트 순서 변환 함수 (호스트 -> 네트워크)
    inline uint16_t htons(uint16_t value) {
        #if defined(__LITTLE_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __LITTLE_ENDIAN)
            return ((value & 0xFF) << 8) | ((value & 0xFF00) >> 8);
        #else
            return value;
        #endif
    }
    
    // 네트워크 바이트 순서 변환 함수 (네트워크 -> 호스트)
    inline uint16_t ntohs(uint16_t value) {
        return htons(value); // 동일한 연산
    }
    
    // 네트워크 바이트 순서 변환 함수 (호스트 -> 네트워크)
    inline uint32_t htonl(uint32_t value) {
        #if defined(__LITTLE_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __LITTLE_ENDIAN)
            return ((value & 0xFF) << 24) |
                   ((value & 0xFF00) << 8) |
                   ((value & 0xFF0000) >> 8) |
                   ((value & 0xFF000000) >> 24);
        #else
            return value;
        #endif
    }
    
    // 네트워크 바이트 순서 변환 함수 (네트워크 -> 호스트)
    inline uint32_t ntohl(uint32_t value) {
        return htonl(value); // 동일한 연산
    }
    
    // 네트워크 바이트 순서 변환 함수 (호스트 -> 네트워크)
    inline uint64_t htonll(uint64_t value) {
        #if defined(__LITTLE_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __LITTLE_ENDIAN)
            return ((value & 0xFF) << 56) |
                   ((value & 0xFF00) << 40) |
                   ((value & 0xFF0000) << 24) |
                   ((value & 0xFF000000) << 8) |
                   ((value & 0xFF00000000) >> 8) |
                   ((value & 0xFF0000000000) >> 24) |
                   ((value & 0xFF000000000000) >> 40) |
                   ((value & 0xFF00000000000000) >> 56);
        #else
            return value;
        #endif
    }
    
    // 네트워크 바이트 순서 변환 함수 (네트워크 -> 호스트)
    inline uint64_t ntohll(uint64_t value) {
        return htonll(value); // 동일한 연산
    }
}

