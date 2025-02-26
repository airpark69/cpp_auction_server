#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include "Server.h"
#include "AuctionHouse.h"
#include "DatabaseManager.h"
#include "Item.h"

using json = nlohmann::json;

// 테스트용 모의 데이터베이스 매니저 클래스
class MockDatabaseManager : public DatabaseManager {
public:
    MockDatabaseManager() : DatabaseManager("mongodb://localhost:27017", "tcp://localhost:6379") {}

    // 아이템 저장 모의 구현
    bool saveAuctionItem(const AuctionItem& item) override {
        saved_items_[item.id] = item;
        return true;
    }

    // 아이템 조회 모의 구현
    std::optional<AuctionItem> getAuctionItem(uint64_t id) override {
        auto it = saved_items_.find(id);
        if (it != saved_items_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // 모든 아이템 로드 모의 구현
    std::vector<AuctionItem> loadAllAuctionItems() override {
        std::vector<AuctionItem> items;
        for (const auto& [id, item] : saved_items_) {
            items.push_back(item);
        }
        return items;
    }

    // 사용자 정보 조회 모의 구현
    std::optional<UserInfo> getUserInfo(uint32_t user_id) override {
        UserInfo user;
        user.id = user_id;
        user.username = "test_user_" + std::to_string(user_id);
        user.balance = 10000;
        return user;
    }

private:
    std::unordered_map<uint64_t, AuctionItem> saved_items_;
};

// 서버 기본 기능 테스트
TEST(ServerTest, BasicFunctionality) {
    // 모의 데이터베이스 매니저 생성
    auto db_manager = std::make_shared<MockDatabaseManager>();
    
    // 경매장 시스템 생성
    auto auction_house = std::make_shared<AuctionHouse>(db_manager);
    
    // 테스트 아이템 생성
    Item test_item;
    test_item.id = 1001;
    test_item.name = "Test Sword";
    test_item.description = "A test sword for unit testing";
    test_item.type = 1; // 무기
    test_item.rarity = 2; // 희귀
    test_item.level = 10;
    test_item.owner_id = 1;
    
    // 아이템 등록
    uint64_t auction_id = auction_house->registerItem(1, test_item, 500);
    ASSERT_GT(auction_id, 0) << "Failed to register item";
    
    // 검색 조건 생성
    SearchCriteria criteria;
    criteria.keyword = "Sword";
    
    // 아이템 검색
    auto items = auction_house->searchItems(criteria);
    ASSERT_EQ(items.size(), 1) << "Failed to find registered item";
    ASSERT_EQ(items[0].id, auction_id) << "Found wrong item";
    ASSERT_EQ(items[0].item.name, "Test Sword") << "Item name mismatch";
    ASSERT_EQ(items[0].price, 500) << "Item price mismatch";
    
    // 아이템 구매
    bool purchase_result = auction_house->purchaseItem(auction_id, 2);
    ASSERT_TRUE(purchase_result) << "Failed to purchase item";
    
    // 구매 후 아이템 상태 확인
    auto item = db_manager->getAuctionItem(auction_id);
    ASSERT_TRUE(item.has_value()) << "Failed to retrieve item after purchase";
    ASSERT_EQ(item->status, AuctionStatus::SOLD) << "Item status not updated to SOLD";
    ASSERT_EQ(item->buyer_id, 2) << "Buyer ID not updated correctly";
}

// 경매장 검색 기능 테스트
TEST(AuctionHouseTest, SearchFunctionality) {
    // 모의 데이터베이스 매니저 생성
    auto db_manager = std::make_shared<MockDatabaseManager>();
    
    // 경매장 시스템 생성
    auto auction_house = std::make_shared<AuctionHouse>(db_manager);
    
    // 여러 테스트 아이템 등록
    for (int i = 1; i <= 10; i++) {
        Item item;
        item.id = 1000 + i;
        
        if (i <= 5) {
            item.name = "Sword " + std::to_string(i);
            item.type = 1; // 무기
        } else {
            item.name = "Shield " + std::to_string(i - 5);
            item.type = 2; // 방어구
        }
        
        item.description = "Test item " + std::to_string(i);
        item.rarity = (i % 3) + 1; // 1-3 희귀도
        item.level = i * 5;
        item.owner_id = 1;
        
        auction_house->registerItem(1, item, i * 100);
    }
    
    // 키워드 검색 테스트
    {
        SearchCriteria criteria;
        criteria.keyword = "Sword";
        
        auto items = auction_house->searchItems(criteria);
        ASSERT_EQ(items.size(), 5) << "Wrong number of sword items found";
        
        for (const auto& item : items) {
            ASSERT_TRUE(item.item.name.find("Sword") != std::string::npos) 
                << "Found item without 'Sword' in name: " << item.item.name;
        }
    }
    
    // 가격 범위 검색 테스트
    {
        SearchCriteria criteria;
        criteria.min_price = 300;
        criteria.max_price = 700;
        
        auto items = auction_house->searchItems(criteria);
        ASSERT_EQ(items.size(), 5) << "Wrong number of items in price range";
        
        for (const auto& item : items) {
            ASSERT_GE(item.price, 300) << "Found item with price below minimum";
            ASSERT_LE(item.price, 700) << "Found item with price above maximum";
        }
    }
    
    // 아이템 타입 검색 테스트
    {
        SearchCriteria criteria;
        criteria.item_type = 2; // 방어구
        
        auto items = auction_house->searchItems(criteria);
        ASSERT_EQ(items.size(), 5) << "Wrong number of shield items found";
        
        for (const auto& item : items) {
            ASSERT_EQ(item.item.type, 2) << "Found item with wrong type";
            ASSERT_TRUE(item.item.name.find("Shield") != std::string::npos) 
                << "Found non-shield item: " << item.item.name;
        }
    }
    
    // 정렬 테스트
    {
        SearchCriteria criteria;
        criteria.sort_by = "price";
        criteria.sort_order = "desc";
        
        auto items = auction_house->searchItems(criteria);
        ASSERT_EQ(items.size(), 10) << "Wrong number of items returned";
        
        // 가격 내림차순 확인
        for (size_t i = 1; i < items.size(); i++) {
            ASSERT_GE(items[i-1].price, items[i].price) 
                << "Items not sorted correctly by price in descending order";
        }
    }
    
    // 페이지네이션 테스트
    {
        SearchCriteria criteria;
        criteria.page = 2;
        criteria.items_per_page = 3;
        
        auto items = auction_house->searchItems(criteria);
        ASSERT_EQ(items.size(), 3) << "Wrong number of items returned for pagination";
    }
}

// 동시성 테스트
TEST(AuctionHouseTest, ConcurrencyTest) {
    // 모의 데이터베이스 매니저 생성
    auto db_manager = std::make_shared<MockDatabaseManager>();
    
    // 경매장 시스템 생성
    auto auction_house = std::make_shared<AuctionHouse>(db_manager);
    
    // 테스트 아이템 등록
    Item test_item;
    test_item.id = 2001;
    test_item.name = "Concurrent Test Item";
    test_item.description = "An item for testing concurrent access";
    test_item.type = 3;
    test_item.rarity = 4;
    test_item.level = 50;
    test_item.owner_id = 1;
    
    uint64_t auction_id = auction_house->registerItem(1, test_item, 1000);
    ASSERT_GT(auction_id, 0) << "Failed to register item for concurrency test";
    
    // 여러 스레드에서 동시에 아이템 구매 시도
    constexpr int thread_count = 5;
    std::vector<std::thread> threads;
    std::vector<bool> results(thread_count, false);
    
    for (int i = 0; i < thread_count; i++) {
        threads.emplace_back([&auction_house, auction_id, i, &results]() {
            results[i] = auction_house->purchaseItem(auction_id, 100 + i);
        });
    }
    
    // 모든 스레드 종료 대기
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 결과 확인 - 하나의 스레드만 성공해야 함
    int success_count = 0;
    for (bool result : results) {
        if (result) success_count++;
    }
    
    ASSERT_EQ(success_count, 1) << "Concurrent purchase test failed: " << success_count << " successes";
    
    // 아이템 상태 확인
    auto item = db_manager->getAuctionItem(auction_id);
    ASSERT_TRUE(item.has_value()) << "Failed to retrieve item after concurrent purchases";
    ASSERT_EQ(item->status, AuctionStatus::SOLD) << "Item status not updated to SOLD";
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
