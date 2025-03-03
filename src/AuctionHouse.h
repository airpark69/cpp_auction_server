#pragma once

#include <mutex>
#include <unordered_map>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include "Item.h"
#include "DatabaseManager.h"

// 경매 아이템 상태 열거형
enum class AuctionStatus {
    ACTIVE,
    SOLD,
    EXPIRED,
    CANCELLED
};

// 경매 아이템 구조체
struct AuctionItem {
    uint64_t id;                 // 경매 ID
    Item item;                   // 아이템 정보
    uint32_t views;              // 아이템 조회수
    uint32_t seller_id;          // 판매자 ID
    uint32_t buyer_id;           // 구매자 ID (판매 전: 0)
    uint32_t price;              // 판매 가격
    AuctionStatus status;        // 경매 상태
    time_t registration_time;    // 등록 시간
    time_t expiration_time;      // 만료 시간
    time_t sold_time;            // 판매 시간
};

// 검색 조건 구조체
struct SearchCriteria {
    std::string keyword;         // 검색 키워드
    uint32_t min_price = 0;      // 최소 가격
    uint32_t max_price = 0;      // 최대 가격
    uint32_t item_type = 0;      // 아이템 타입 (0: 모든 타입)
    std::string seller_name;     // 판매자 이름
    
    // 페이지네이션
    uint32_t page = 1;           // 페이지 번호
    uint32_t items_per_page = 20; // 페이지당 아이템 수
    
    // 정렬
    std::string sort_by = "price"; // 정렬 기준 (price, time)
    std::string sort_order = "asc"; // 정렬 순서 (asc, desc)
    
    // 캐싱을 위한 해시 함수
    size_t hash() const;
    
    // 인기 검색어인지 확인
    bool isPopularSearch() const;
};

class AuctionHouse {
private:
    std::mutex mutex_;
    std::unordered_map<uint64_t, AuctionItem> auction_items_;
    std::shared_ptr<DatabaseManager> db_manager_;
    uint64_t next_auction_id_;
    
    // 인덱싱을 위한 자료구조
    std::map<uint32_t, std::set<uint64_t>> item_type_index_;
    std::map<std::string, std::set<uint64_t>> seller_index_;
    std::map<uint32_t, std::set<uint64_t>> price_index_;
    std::map<std::string, std::set<uint64_t>> keyword_index_;
    
public:
    AuctionHouse(std::shared_ptr<DatabaseManager> db);
    
    // 아이템 등록/검색/구매 API
    uint64_t registerItem(uint32_t seller_id, const Item& item, uint32_t price);
    std::vector<AuctionItem> searchItems(const SearchCriteria& criteria);
    bool purchaseItem(uint64_t auction_id, uint32_t buyer_id);
    bool cancelListing(uint64_t auction_id, uint32_t seller_id);
    
    // 주기적인 만료 아이템 처리
    void processExpiredItems();
    
private:
    // 인덱스 업데이트 유틸리티 함수
    void updateIndices(const AuctionItem& item);
    void removeFromIndices(const AuctionItem& item);
    
    // 검색 유틸리티 함수
    bool matchesCriteria(const AuctionItem& item, const SearchCriteria& criteria);
    void sortResults(std::vector<AuctionItem>& results, const std::string& sort_by, const std::string& sort_order);
    void applyPagination(std::vector<AuctionItem>& results, uint32_t page, uint32_t items_per_page);
    
    // 키워드 인덱싱 유틸리티
    void indexItemKeywords(uint64_t auction_id, const Item& item);
};
