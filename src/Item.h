#pragma once

#include <string>
#include <vector>
#include <cstdint>

// 아이템 속성 구조체
struct ItemAttribute {
    std::string name;
    std::string value;
};

// 아이템 구조체
struct Item {
    uint32_t id;                     // 아이템 고유 ID
    std::string name;                // 아이템 이름
    std::string description;         // 아이템 설명
    uint32_t type;                   // 아이템 타입 (무기, 방어구 등)
    uint32_t rarity;                 // 아이템 희귀도 (일반, 희귀, 전설 등)
    uint32_t level;                  // 아이템 레벨
    uint32_t owner_id;               // 소유자 ID
    std::vector<ItemAttribute> attributes; // 아이템 속성 목록
};

// 사용자 정보 구조체
struct UserInfo {
    uint32_t id;                     // 사용자 ID
    std::string username;            // 사용자 이름
    uint32_t balance;                // 보유 금액
};
