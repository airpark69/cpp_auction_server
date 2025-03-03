#include "DatabaseManager.h"
#include <spdlog/spdlog.h>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/string/to_string.hpp>
#include <mongocxx/exception/exception.hpp>
#include <nlohmann/json.hpp>
#include "AuctionHouse.h"

using bsoncxx::builder::stream::document;
using bsoncxx::builder::stream::open_document;
using bsoncxx::builder::stream::open_array;
using bsoncxx::builder::stream::close_array;
using bsoncxx::builder::stream::close_document;
using bsoncxx::builder::stream::finalize;
using json = nlohmann::json;

// MongoDB 인스턴스 초기화 (정적 인스턴스)
static mongocxx::instance instance{};

DatabaseManager::DatabaseManager(const std::string& mongo_uri, const std::string& redis_uri)
    : mongo_client_(mongocxx::uri(mongo_uri)), redis_available_(false) {
    
    // 데이터베이스 선택
    db_ = mongo_client_["auction_db"];
    
    // Redis 연결 설정
    try {
        redis_ = std::make_shared<sw::redis::Redis>(redis_uri);
        // 연결 테스트
        redis_->ping();
        redis_available_ = true;
        spdlog::info("Connected to Redis at {}", redis_uri);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to connect to Redis: {}", e.what());
        spdlog::warn("Caching will be disabled");
    }
    
    spdlog::info("DatabaseManager initialized with MongoDB at {}", mongo_uri);
}

DatabaseManager::~DatabaseManager() {
    // Redis 연결 종료는 자동으로 처리됨
    spdlog::info("DatabaseManager destroyed");
}

std::shared_ptr<Transaction> DatabaseManager::beginTransaction() {
    return std::make_shared<Transaction>(shared_from_this());
}

mongocxx::collection DatabaseManager::getAuctionCollection() {
    return db_["auctions"];
}

mongocxx::collection DatabaseManager::getUserCollection() {
    return db_["users"];
}

bool DatabaseManager::saveAuctionItem(const AuctionItem& item) {
    try {
        auto collection = getAuctionCollection();
        auto doc = itemToBson(item);
        
        auto result = collection.insert_one(doc.view());
        return result && result->result().inserted_count() == 1;
    } catch (const mongocxx::exception& e) {
        spdlog::error("MongoDB error in saveAuctionItem: {}", e.what());
        return false;
    }
}

std::optional<AuctionItem> DatabaseManager::getAuctionItem(uint64_t id) {
    try {
        if (redis_available_) {
            std::string cache_key = getItemCacheKey(id);
            auto cached = redis_->get(cache_key);
            // 캐시 적중
            if (cached) {
                // 캐시에서 아이템 파싱
                json item_json = json::parse(*cached);
                AuctionItem item;
                // JSON에서 AuctionItem으로 변환
                item.id = item_json["id"];
                item.seller_id = item_json["seller_id"];
                item.buyer_id = item_json["buyer_id"];
                item.price = item_json["price"];
                item.status = static_cast<AuctionStatus>(item_json["status"]);
                item.registration_time = item_json["registration_time"];
                item.expiration_time = item_json["expiration_time"];
                item.sold_time = item_json["sold_time"];
                
                // 아이템 정보 파싱
                item.item.id = item_json["item"]["id"];
                item.item.name = item_json["item"]["name"];
                item.item.description = item_json["item"]["description"];
                item.item.type = item_json["item"]["type"];
                item.item.rarity = item_json["item"]["rarity"];
                item.item.level = item_json["item"]["level"];
                item.item.owner_id = item_json["item"]["owner_id"];
                
                return item;
            }
        }
        
        // 캐시 Miss의 경우
        // - DB에서 조회
        auto collection = getAuctionCollection();
        auto filter = document{} << "id" << static_cast<int64_t>(id) << finalize;
        
        auto result = collection.find_one(filter.view());
        if (result) {
            AuctionItem item = bsonToItem(*result);
            
            // 캐시에 저장
            if (redis_available_) {
                // AuctionItem을 JSON으로 변환 (실제 구현에서는 더 복잡할 수 있음)
                json item_json = {
                    {"id", item.id},
                    {"seller_id", item.seller_id},
                    {"buyer_id", item.buyer_id},
                    {"price", item.price},
                    {"status", static_cast<int>(item.status)},
                    {"registration_time", item.registration_time},
                    {"expiration_time", item.expiration_time},
                    {"sold_time", item.sold_time},
                    {"item", {
                        {"id", item.item.id},
                        {"name", item.item.name},
                        {"description", item.item.description},
                        {"type", item.item.type},
                        {"rarity", item.item.rarity},
                        {"level", item.item.level},
                        {"owner_id", item.item.owner_id}
                    }}
                };
                
                std::string cache_key = getItemCacheKey(id);
                redis_->setex(cache_key, 300, item_json.dump());  // 300초 (5분) TTL 설정
            }
            
            return item;
        }
        
        return std::nullopt;
    } catch (const std::exception& e) {
        spdlog::error("Error in getAuctionItem: {}", e.what());
        return std::nullopt;
    }
}

bool DatabaseManager::updateAuctionItem(const AuctionItem& item) {
    try {
        auto collection = getAuctionCollection();
        auto filter = document{} << "id" << static_cast<int64_t>(item.id) << finalize;
        auto update = document{} << "$set" << itemToBson(item).view() << finalize;
        
        auto result = collection.update_one(filter.view(), update.view());
        
        // 캐시 업데이트
        if (redis_available_) {
            std::string cache_key = getItemCacheKey(item.id);
            redis_->del(cache_key);
        }
        
        return result && result->modified_count() == 1;
    } catch (const mongocxx::exception& e) {
        spdlog::error("MongoDB error in updateAuctionItem: {}", e.what());
        return false;
    }
}

bool DatabaseManager::deleteAuctionItem(uint64_t id) {
    try {
        auto collection = getAuctionCollection();
        auto filter = document{} << "id" << static_cast<int64_t>(id) << finalize;
        
        auto result = collection.delete_one(filter.view());
        
        // 캐시에서 삭제
        if (redis_available_) {
            std::string cache_key = getItemCacheKey(id);
            redis_->del(cache_key);
        }
        
        return result && result->deleted_count() == 1;
    } catch (const mongocxx::exception& e) {
        spdlog::error("MongoDB error in deleteAuctionItem: {}", e.what());
        return false;
    }
}

std::vector<AuctionItem> DatabaseManager::loadAllAuctionItems() {
    std::vector<AuctionItem> items;
    
    try {
        auto collection = getAuctionCollection();
        auto cursor = collection.find({});
        
        for (auto&& doc : cursor) {
            items.push_back(bsonToItem(doc));
        }
        
        spdlog::info("Loaded {} auction items from database", items.size());
    } catch (const mongocxx::exception& e) {
        spdlog::error("MongoDB error in loadAllAuctionItems: {}", e.what());
    }
    
    return items;
}

bool DatabaseManager::updateUserBalance(uint32_t user_id, int32_t amount) {
    try {
        auto collection = getUserCollection();
        auto filter = document{} << "id" << static_cast<int32_t>(user_id) << finalize;
        
        // 사용자 정보 가져오기
        auto user_doc = collection.find_one(filter.view());
        if (!user_doc) {
            spdlog::error("User {} not found", user_id);
            return false;
        }
        
        // 현재 잔액 가져오기
        int32_t current_balance = 0;
        try {
            current_balance = user_doc->view()["balance"].get_int32();
        } catch (const std::exception& e) {
            spdlog::error("Error getting user balance: {}", e.what());
            return false;
        }
        
        // 새 잔액 계산
        int32_t new_balance = current_balance + amount;
        
        // 잔액이 음수가 되지 않도록 확인
        if (new_balance < 0) {
            spdlog::error("Insufficient balance for user {}: {} < {}", 
                         user_id, current_balance, -amount);
            return false;
        }
        
        // 잔액 업데이트
        auto update = document{} << "$set" << open_document <<
            "balance" << new_balance << close_document << finalize;
        
        auto result = collection.update_one(filter.view(), update.view());
        
        return result && result->modified_count() == 1;
    } catch (const mongocxx::exception& e) {
        spdlog::error("MongoDB error in updateUserBalance: {}", e.what());
        return false;
    }
}

std::optional<UserInfo> DatabaseManager::getUserInfo(uint32_t user_id) {
    try {
        auto collection = getUserCollection();
        auto filter = document{} << "id" << static_cast<int32_t>(user_id) << finalize;
        
        auto result = collection.find_one(filter.view());
        if (result) {
            UserInfo user;
            user.id = user_id;
            user.username = bsoncxx::string::to_string(result->view()["username"].get_string().value);
            user.balance = result->view()["balance"].get_int32();
            return user;
        }
        
        return std::nullopt;
    } catch (const mongocxx::exception& e) {
        spdlog::error("MongoDB error in getUserInfo: {}", e.what());
        return std::nullopt;
    }
}

void DatabaseManager::cacheSearchResults(size_t criteria_hash, const std::vector<AuctionItem>& results) {
    if (!redis_available_) {
        return;
    }
    
    try {
        // 검색 결과를 JSON 배열로 변환
        json results_json = json::array();
        
        for (const auto& item : results) {
            json item_json = {
                {"id", item.id},
                {"seller_id", item.seller_id},
                {"price", item.price},
                {"registration_time", item.registration_time},
                {"item", {
                    {"id", item.item.id},
                    {"name", item.item.name},
                    {"type", item.item.type},
                    {"rarity", item.item.rarity},
                    {"level", item.item.level}
                }}
            };
            
            results_json.push_back(item_json);
        }
        
        // Redis에 캐싱 (5분 만료)
        std::string cache_key = getSearchCacheKey(criteria_hash);
        redis_->setex(cache_key, 300, results_json.dump());  // 300초 (5분) TTL 설정
        
        spdlog::debug("Cached search results for hash {}: {} items", criteria_hash, results.size());
    } catch (const std::exception& e) {
        spdlog::error("Error caching search results: {}", e.what());
    }
}

std::optional<std::vector<AuctionItem>> DatabaseManager::getCachedSearch(size_t criteria_hash) {
    if (!redis_available_) {
        return std::nullopt;
    }
    
    try {
        std::string cache_key = getSearchCacheKey(criteria_hash);
        auto cached = redis_->get(cache_key);
        
        if (!cached) {
            return std::nullopt;
        }
        
        // JSON 파싱
        json results_json = json::parse(*cached);
        
        // JSON 배열을 AuctionItem 벡터로 변환
        std::vector<AuctionItem> results;
        
        for (const auto& item_json : results_json) {
            AuctionItem item;
            item.id = item_json["id"];
            item.seller_id = item_json["seller_id"];
            item.price = item_json["price"];
            item.registration_time = item_json["registration_time"];
            item.status = AuctionStatus::ACTIVE;
            
            item.item.id = item_json["item"]["id"];
            item.item.name = item_json["item"]["name"];
            item.item.type = item_json["item"]["type"];
            item.item.rarity = item_json["item"]["rarity"];
            item.item.level = item_json["item"]["level"];
            
            results.push_back(item);
        }
        
        spdlog::debug("Retrieved cached search results for hash {}: {} items", 
                     criteria_hash, results.size());
        
        return results;
    } catch (const std::exception& e) {
        spdlog::error("Error retrieving cached search results: {}", e.what());
        return std::nullopt;
    }
}

std::vector<AuctionItem> DatabaseManager::getPopularItems(uint32_t count) {
    std::vector<AuctionItem> items;
    
    try {
        auto collection = getAuctionCollection();
        
        // 활성 상태의 아이템 중 조회수가 높은 순으로 정렬
        auto filter = document{} << "status" << static_cast<int>(AuctionStatus::ACTIVE) << finalize;
        mongocxx::options::find options;
        options.sort(document{} << "views" << -1 << finalize);
        options.limit(count);
        
        auto cursor = collection.find(filter.view(), options);
        
        for (auto&& doc : cursor) {
            items.push_back(bsonToItem(doc));
        }
        
        spdlog::info("Retrieved {} popular items", items.size());
    } catch (const mongocxx::exception& e) {
        spdlog::error("MongoDB error in getPopularItems: {}", e.what());
    }
    
    return items;
}

std::vector<AuctionItem> DatabaseManager::getItemsByCategory(uint32_t category, uint32_t count) {
    std::vector<AuctionItem> items;
    
    try {
        auto collection = getAuctionCollection();
        
        // 특정 카테고리의 활성 아이템 조회
        auto filter = document{} << "status" << static_cast<int>(AuctionStatus::ACTIVE)
                                << "item.type" << static_cast<int32_t>(category) << finalize;
        
        mongocxx::options::find options;
        options.limit(count);
        
        auto cursor = collection.find(filter.view(), options);
        
        for (auto&& doc : cursor) {
            items.push_back(bsonToItem(doc));
        }
        
        spdlog::info("Retrieved {} items for category {}", items.size(), category);
    } catch (const mongocxx::exception& e) {
        spdlog::error("MongoDB error in getItemsByCategory: {}", e.what());
    }
    
    return items;
}

bsoncxx::document::value DatabaseManager::itemToBson(const AuctionItem& item) {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;
    using bsoncxx::builder::basic::make_array;
    
    // 아이템 속성 배열 생성
    bsoncxx::builder::basic::array attributes_array;
    if (!item.item.attributes.empty()) {
        for (const auto& attr : item.item.attributes) {
            attributes_array.append(
                make_document(
                    kvp("name", attr.name),
                    kvp("value", attr.value)
                )
            );
        }
    }
    
    // 아이템 서브 문서 생성
    auto item_doc = bsoncxx::builder::basic::document{};
    item_doc.append(kvp("id", static_cast<int32_t>(item.item.id)));
    item_doc.append(kvp("name", item.item.name));
    item_doc.append(kvp("description", item.item.description));
    item_doc.append(kvp("type", static_cast<int32_t>(item.item.type)));
    item_doc.append(kvp("rarity", static_cast<int32_t>(item.item.rarity)));
    item_doc.append(kvp("level", static_cast<int32_t>(item.item.level)));
    item_doc.append(kvp("owner_id", static_cast<int32_t>(item.item.owner_id)));
    // 속성 배열이 비어있지 않으면 아이템 문서에 추가
    if (!item.item.attributes.empty()) {
        item_doc.append(kvp("attributes", attributes_array));
    }
    
    // 메인 문서 생성
    auto doc = make_document(
        kvp("id", static_cast<int64_t>(item.id)),
        kvp("seller_id", static_cast<int32_t>(item.seller_id)),
        kvp("buyer_id", static_cast<int32_t>(item.buyer_id)),
        kvp("price", static_cast<int32_t>(item.price)),
        kvp("views", static_cast<int32_t>(item.views)),
        kvp("status", static_cast<int>(item.status)),
        kvp("registration_time", static_cast<int64_t>(item.registration_time)),
        kvp("expiration_time", static_cast<int64_t>(item.expiration_time)),
        kvp("sold_time", static_cast<int64_t>(item.sold_time)),
        kvp("item", item_doc)
    );
    
    return doc;
}


AuctionItem DatabaseManager::bsonToItem(const bsoncxx::document::view& doc) {
    AuctionItem item;
    
    item.id = doc["id"].get_int64();
    item.seller_id = doc["seller_id"].get_int32();
    item.buyer_id = doc["buyer_id"].get_int32();
    item.price = doc["price"].get_int32();
    item.views = doc["views"].get_int32();
    item.status = static_cast<AuctionStatus>(doc["status"].get_int32().value);
    item.registration_time = doc["registration_time"].get_int64();
    item.expiration_time = doc["expiration_time"].get_int64();
    item.sold_time = doc["sold_time"].get_int64();
    
    auto item_doc = doc["item"].get_document().view();
    item.item.id = item_doc["id"].get_int32();
    item.item.name = bsoncxx::string::to_string(item_doc["name"].get_string().value);
    item.item.description = bsoncxx::string::to_string(item_doc["description"].get_string().value);
    item.item.type = item_doc["type"].get_int32();
    item.item.rarity = item_doc["rarity"].get_int32();
    item.item.level = item_doc["level"].get_int32();
    item.item.owner_id = item_doc["owner_id"].get_int32();
    
    // 아이템 속성 배열 파싱
    if (doc["attributes"]) {
        auto attributes_array = doc["attributes"].get_array().value;
        
        for (auto&& attr_doc : attributes_array) {
            if (attr_doc.type() == bsoncxx::type::k_document) {
                auto attr_view = attr_doc.get_document().view();
                
                ItemAttribute attr;
                attr.name = bsoncxx::string::to_string(attr_view["name"].get_string().value);
                attr.value = bsoncxx::string::to_string(attr_view["value"].get_string().value);
                item.item.attributes.push_back(attr);
            }
        }
    }
    
    return item;
}

std::string DatabaseManager::getSearchCacheKey(size_t criteria_hash) {
    return "search:" + std::to_string(criteria_hash);
}

std::string DatabaseManager::getItemCacheKey(uint64_t item_id) {
    return "item:" + std::to_string(item_id);
}

// Transaction 클래스 구현
Transaction::Transaction(std::shared_ptr<DatabaseManager> db_manager)
    : db_manager_(db_manager), committed_(false) {
    // 실제 MongoDB 트랜잭션 시작
    // MongoDB 4.0 이상에서는 세션과 트랜잭션 API를 사용할 수 있음
    // 이 예제에서는 간단히 구현
}

Transaction::~Transaction() {
    if (!committed_) {
        rollback();
    }
}

void Transaction::commit() {
    // 트랜잭션 커밋
    committed_ = true;
    spdlog::debug("Transaction committed");
}

void Transaction::rollback() {
    // 트랜잭션 롤백
    committed_ = true; // 소멸자에서 다시 롤백하지 않도록 설정
    spdlog::debug("Transaction rolled back");
}
