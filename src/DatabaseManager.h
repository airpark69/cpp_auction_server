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

    // MongoDB 클라이언트 접근자
    mongocxx::client& getMongoClient();

    // 트랜잭션 관리
    std::shared_ptr<Transaction> beginTransaction();
    
    // 경매 아이템 CRUD 작업
    bool saveAuctionItem(const AuctionItem& item, mongocxx::client_session* session = nullptr);
    std::optional<AuctionItem> getAuctionItem(uint64_t id);
    bool updateAuctionItem(const AuctionItem& item, mongocxx::client_session* session = nullptr);
    bool deleteAuctionItem(uint64_t id, mongocxx::client_session* session = nullptr);
    std::vector<AuctionItem> loadAllAuctionItems();
    
    // 사용자 관련 작업
    bool DatabaseManager::updateUserBalance(uint32_t user_id, int32_t amount, mongocxx::client_session* session = nullptr);
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
        mongocxx::client_session session_;
        bool committed_;
        
    public:
        Transaction(std::shared_ptr<DatabaseManager> db_manager)
            : db_manager_(db_manager), 
              session_(db_manager->getMongoClient().start_session()),
              committed_(false) {
            // 트랜잭션 시작
            mongocxx::options::transaction options;
            session_.start_transaction(options);
            spdlog::debug("Transaction started with MongoDB session");
        }
        
        ~Transaction() {
            if (!committed_) {
                try {
                    rollback();
                } catch (const std::exception& e) {
                    spdlog::error("Error during transaction rollback in destructor: {}", e.what());
                }
            }
        }
        
        mongocxx::client_session& session() {
            return session_;
        }
        
        void commit() {
            if (!committed_) {
                session_.commit_transaction();
                committed_ = true;
                spdlog::debug("Transaction committed");
            }
        }
        
        void rollback() {
            if (!committed_) {
                session_.abort_transaction();
                committed_ = true; // 소멸자에서 다시 롤백하지 않도록 설정
                spdlog::debug("Transaction rolled back");
            }
        }
};
