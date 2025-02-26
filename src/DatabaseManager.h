#pragma once

#include <memory>
#include <string>
#include <optional>
#include <vector>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <bsoncxx/json.hpp>
#include <sw/redis++/redis++.h>
#include "Item.h"

class Transaction;
struct AuctionItem;
struct SearchCriteria;

class DatabaseManager : public std::enable_shared_from_this<DatabaseManager> {
private:
    // MongoDB 인스턴스 및 클라이언트
    mongocxx::instance instance_;
    mongocxx::client mongo_client_;
    mongocxx::database db_;
    
    // Redis 클라이언트
    std::shared_ptr<sw::redis::Redis> redis_;
    bool redis_available_;
    
public:
    DatabaseManager(const std::string& mongo_uri, const std::string& redis_uri = "tcp://localhost:6379");
    ~DatabaseManager();
    
    // 트랜잭션 관리
    std::shared_ptr<Transaction> beginTransaction();
    
    // 경매 아이템 CRUD 작업
    bool saveAuctionItem(const AuctionItem& item);
    std::optional<AuctionItem> getAuctionItem(uint64_t id);
    bool updateAuctionItem(const AuctionItem& item);
    bool deleteAuctionItem(uint64_t id);
    std::vector<AuctionItem> loadAllAuctionItems();
    
    // 사용자 관련 작업
    bool updateUserBalance(uint32_t user_id, int32_t amount);
    std::optional<UserInfo> getUserInfo(uint32_t user_id);
    
    // 캐싱 관련 메서드
    void cacheSearchResults(size_t criteria_hash, const std::vector<AuctionItem>& results);
    std::optional<std::vector<AuctionItem>> getCachedSearch(size_t criteria_hash);
    std::vector<AuctionItem> getPopularItems(uint32_t count);
    std::vector<AuctionItem> getItemsByCategory(uint32_t category, uint32_t count);
    
private:
    // MongoDB 컬렉션 접근 헬퍼 메서드
    mongocxx::collection getAuctionCollection();
    mongocxx::collection getUserCollection();
    
    // 아이템 변환 유틸리티
    bsoncxx::document::value itemToBson(const AuctionItem& item);
    AuctionItem bsonToItem(const bsoncxx::document::view& doc);
    
    // Redis 키 생성 유틸리티
    std::string getSearchCacheKey(size_t criteria_hash);
    std::string getItemCacheKey(uint64_t item_id);
};

// 트랜잭션 클래스
class Transaction {
private:
    std::shared_ptr<DatabaseManager> db_manager_;
    bool committed_;
    
public:
    Transaction(std::shared_ptr<DatabaseManager> db_manager);
    ~Transaction();
    
    void commit();
    void rollback();
};
