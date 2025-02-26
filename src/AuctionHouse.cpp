#include "AuctionHouse.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <functional>

// SearchCriteria 해시 함수 구현
size_t SearchCriteria::hash() const {
    std::size_t h = 0;
    
    // 문자열 해싱을 위한 간단한 해시 함수
    auto hash_combine = [&h](const std::string& str) {
        for (char c : str) {
            h ^= std::hash<char>{}(c) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
    };
    
    hash_combine(keyword);
    h ^= std::hash<uint32_t>{}(min_price) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(max_price) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(item_type) + 0x9e3779b9 + (h << 6) + (h >> 2);
    hash_combine(seller_name);
    hash_combine(sort_by);
    hash_combine(sort_order);
    
    return h;
}

bool SearchCriteria::isPopularSearch() const {
    // 키워드 검색이나 카테고리 검색은 인기 검색어로 간주
    return !keyword.empty() || item_type != 0;
}

AuctionHouse::AuctionHouse(std::shared_ptr<DatabaseManager> db)
    : db_manager_(db), next_auction_id_(1) {
    
    // 데이터베이스에서 기존 경매 아이템 로드
    auto items = db_manager_->loadAllAuctionItems();
    
    for (const auto& item : items) {
        auction_items_[item.id] = item;
        updateIndices(item);
        
        // 다음 경매 ID 업데이트
        if (item.id >= next_auction_id_) {
            next_auction_id_ = item.id + 1;
        }
    }
    
    spdlog::info("AuctionHouse initialized with {} items", items.size());
}

uint64_t AuctionHouse::registerItem(uint32_t seller_id, const Item& item, uint32_t price) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 새 경매 아이템 생성
    AuctionItem auction_item;
    auction_item.id = next_auction_id_++;
    auction_item.item = item;
    auction_item.seller_id = seller_id;
    auction_item.buyer_id = 0;
    auction_item.price = price;
    auction_item.status = AuctionStatus::ACTIVE;
    auction_item.registration_time = std::time(nullptr);
    
    // 기본 만료 시간: 7일 후
    auction_item.expiration_time = auction_item.registration_time + 7 * 24 * 60 * 60;
    auction_item.sold_time = 0;
    
    // 데이터베이스에 저장
    if (!db_manager_->saveAuctionItem(auction_item)) {
        spdlog::error("Failed to save auction item to database");
        return 0;
    }
    
    // 메모리에 저장
    auction_items_[auction_item.id] = auction_item;
    
    // 인덱스 업데이트
    updateIndices(auction_item);
    
    // 키워드 인덱싱
    indexItemKeywords(auction_item.id, item);
    
    spdlog::info("Item registered: Auction ID {}, Item {}, Price {}", 
                auction_item.id, item.name, price);
    
    return auction_item.id;
}

std::vector<AuctionItem> AuctionHouse::searchItems(const SearchCriteria& criteria) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 캐시 확인
    if (criteria.isPopularSearch()) {
        auto cached_results = db_manager_->getCachedSearch(criteria.hash());
        if (cached_results) {
            return *cached_results;
        }
    }
    
    std::set<uint64_t> result_set;
    
    // 가장 제한적인 조건부터 검색하여 결과 세트 구성
    if (!criteria.keyword.empty() && keyword_index_.find(criteria.keyword) != keyword_index_.end()) {
        // 키워드 검색
        result_set = keyword_index_[criteria.keyword];
    }
    else if (criteria.item_type != 0 && item_type_index_.find(criteria.item_type) != item_type_index_.end()) {
        // 아이템 타입 검색
        result_set = item_type_index_[criteria.item_type];
    }
    else if (!criteria.seller_name.empty()) {
        // 판매자 이름 검색
        auto it = seller_index_.find(criteria.seller_name);
        if (it != seller_index_.end()) {
            result_set = it->second;
        }
    }
    else if (criteria.max_price > 0) {
        // 가격 범위 검색
        for (auto it = price_index_.lower_bound(criteria.min_price);
             it != price_index_.upper_bound(criteria.max_price);
             ++it) {
            result_set.insert(it->second.begin(), it->second.end());
        }
    }
    else {
        // 모든 활성 아이템 반환
        for (const auto& [id, item] : auction_items_) {
            if (item.status == AuctionStatus::ACTIVE) {
                result_set.insert(id);
            }
        }
    }
    
    // 추가 필터링
    std::vector<AuctionItem> results;
    for (uint64_t id : result_set) {
        const auto& item = auction_items_[id];
        if (matchesCriteria(item, criteria)) {
            results.push_back(item);
        }
    }
    
    // 정렬
    sortResults(results, criteria.sort_by, criteria.sort_order);
    
    // 페이지네이션
    applyPagination(results, criteria.page, criteria.items_per_page);
    
    // 인기 검색어는 캐싱
    if (criteria.isPopularSearch()) {
        db_manager_->cacheSearchResults(criteria.hash(), results);
    }
    
    spdlog::info("Search performed: found {} items", results.size());
    
    return results;
}

bool AuctionHouse::purchaseItem(uint64_t auction_id, uint32_t buyer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = auction_items_.find(auction_id);
    if (it == auction_items_.end() || it->second.status != AuctionStatus::ACTIVE) {
        spdlog::warn("Purchase failed: item {} not found or not active", auction_id);
        return false;
    }
    
    // 자신의 아이템은 구매할 수 없음
    if (it->second.seller_id == buyer_id) {
        spdlog::warn("Purchase failed: buyer {} cannot buy own item", buyer_id);
        return false;
    }
    
    // 트랜잭션 시작
    auto transaction = db_manager_->beginTransaction();
    
    try {
        // 구매자 잔액 확인 및 차감
        auto buyer = db_manager_->getUserInfo(buyer_id);
        if (!buyer || buyer->balance < it->second.price) {
            transaction->rollback();
            spdlog::warn("Purchase failed: insufficient balance for user {}", buyer_id);
            return false;
        }
        
        // 판매자에게 금액 지급
        if (!db_manager_->updateUserBalance(it->second.seller_id, it->second.price)) {
            transaction->rollback();
            spdlog::error("Purchase failed: could not update seller balance");
            return false;
        }
        
        // 구매자 잔액 차감
        if (!db_manager_->updateUserBalance(buyer_id, -static_cast<int32_t>(it->second.price))) {
            transaction->rollback();
            spdlog::error("Purchase failed: could not update buyer balance");
            return false;
        }
        
        // 아이템 소유권 이전
        it->second.status = AuctionStatus::SOLD;
        it->second.buyer_id = buyer_id;
        it->second.sold_time = std::time(nullptr);
        
        // 아이템 소유자 변경
        it->second.item.owner_id = buyer_id;
        
        if (!db_manager_->updateAuctionItem(it->second)) {
            transaction->rollback();
            spdlog::error("Purchase failed: could not update auction item");
            return false;
        }
        
        // 인덱스 업데이트
        removeFromIndices(it->second);
        
        transaction->commit();
        
        spdlog::info("Item {} purchased by user {} for {}", 
                   auction_id, buyer_id, it->second.price);
        
        return true;
    }
    catch (const std::exception& e) {
        transaction->rollback();
        spdlog::error("Transaction failed: {}", e.what());
        return false;
    }
}

bool AuctionHouse::cancelListing(uint64_t auction_id, uint32_t seller_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = auction_items_.find(auction_id);
    if (it == auction_items_.end() || it->second.status != AuctionStatus::ACTIVE) {
        spdlog::warn("Cancel failed: item {} not found or not active", auction_id);
        return false;
    }
    
    // 판매자 확인
    if (it->second.seller_id != seller_id) {
        spdlog::warn("Cancel failed: user {} is not the seller of item {}", seller_id, auction_id);
        return false;
    }
    
    // 상태 변경
    it->second.status = AuctionStatus::CANCELLED;
    
    // 데이터베이스 업데이트
    if (!db_manager_->updateAuctionItem(it->second)) {
        spdlog::error("Cancel failed: could not update auction item in database");
        return false;
    }
    
    // 인덱스에서 제거
    removeFromIndices(it->second);
    
    spdlog::info("Listing {} cancelled by seller {}", auction_id, seller_id);
    
    return true;
}

void AuctionHouse::processExpiredItems() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    time_t now = std::time(nullptr);
    std::vector<uint64_t> expired_ids;
    
    // 만료된 아이템 찾기
    for (auto& [id, item] : auction_items_) {
        if (item.status == AuctionStatus::ACTIVE && item.expiration_time < now) {
            item.status = AuctionStatus::EXPIRED;
            
            // 데이터베이스 업데이트
            if (!db_manager_->updateAuctionItem(item)) {
                spdlog::error("Failed to update expired item {} in database", id);
                continue;
            }
            
            expired_ids.push_back(id);
            spdlog::info("Item {} expired", id);
        }
    }
    
    // 인덱스에서 만료된 아이템 제거
    for (uint64_t id : expired_ids) {
        removeFromIndices(auction_items_[id]);
    }
    
    if (!expired_ids.empty()) {
        spdlog::info("Processed {} expired items", expired_ids.size());
    }
}

void AuctionHouse::updateIndices(const AuctionItem& item) {
    // 활성 아이템만 인덱싱
    if (item.status != AuctionStatus::ACTIVE) {
        return;
    }
    
    // 아이템 타입 인덱스 업데이트
    item_type_index_[item.item.type].insert(item.id);
    
    // 판매자 인덱스 업데이트
    auto seller_info = db_manager_->getUserInfo(item.seller_id);
    if (seller_info) {
        seller_index_[seller_info->username].insert(item.id);
    }
    
    // 가격 인덱스 업데이트
    price_index_[item.price].insert(item.id);
}

void AuctionHouse::removeFromIndices(const AuctionItem& item) {
    // 아이템 타입 인덱스에서 제거
    auto type_it = item_type_index_.find(item.item.type);
    if (type_it != item_type_index_.end()) {
        type_it->second.erase(item.id);
        if (type_it->second.empty()) {
            item_type_index_.erase(type_it);
        }
    }
    
    // 판매자 인덱스에서 제거
    auto seller_info = db_manager_->getUserInfo(item.seller_id);
    if (seller_info) {
        auto seller_it = seller_index_.find(seller_info->username);
        if (seller_it != seller_index_.end()) {
            seller_it->second.erase(item.id);
            if (seller_it->second.empty()) {
                seller_index_.erase(seller_it);
            }
        }
    }
    
    // 가격 인덱스에서 제거
    auto price_it = price_index_.find(item.price);
    if (price_it != price_index_.end()) {
        price_it->second.erase(item.id);
        if (price_it->second.empty()) {
            price_index_.erase(price_it);
        }
    }
    
    // 키워드 인덱스에서 제거
    for (auto& [keyword, items] : keyword_index_) {
        items.erase(item.id);
    }
    
    // 빈 키워드 인덱스 정리
    auto it = keyword_index_.begin();
    while (it != keyword_index_.end()) {
        if (it->second.empty()) {
            it = keyword_index_.erase(it);
        } else {
            ++it;
        }
    }
}

bool AuctionHouse::matchesCriteria(const AuctionItem& item, const SearchCriteria& criteria) {
    // 활성 아이템만 검색 결과에 포함
    if (item.status != AuctionStatus::ACTIVE) {
        return false;
    }
    
    // 가격 범위 확인
    if (criteria.min_price > 0 && item.price < criteria.min_price) {
        return false;
    }
    
    if (criteria.max_price > 0 && item.price > criteria.max_price) {
        return false;
    }
    
    // 아이템 타입 확인
    if (criteria.item_type > 0 && item.item.type != criteria.item_type) {
        return false;
    }
    
    // 판매자 이름 확인
    if (!criteria.seller_name.empty()) {
        auto seller_info = db_manager_->getUserInfo(item.seller_id);
        if (!seller_info || seller_info->username != criteria.seller_name) {
            return false;
        }
    }
    
    // 키워드 검색
    if (!criteria.keyword.empty()) {
        // 아이템 이름에 키워드가 포함되어 있는지 확인
        std::string item_name_lower = item.item.name;
        std::string keyword_lower = criteria.keyword;
        
        // 대소문자 구분 없이 검색을 위해 소문자로 변환
        std::transform(item_name_lower.begin(), item_name_lower.end(), 
                      item_name_lower.begin(), ::tolower);
        std::transform(keyword_lower.begin(), keyword_lower.end(), 
                      keyword_lower.begin(), ::tolower);
        
        if (item_name_lower.find(keyword_lower) == std::string::npos) {
            return false;
        }
    }
    
    return true;
}

void AuctionHouse::sortResults(std::vector<AuctionItem>& results, 
                              const std::string& sort_by, 
                              const std::string& sort_order) {
    bool ascending = (sort_order != "desc");
    
    if (sort_by == "price") {
        if (ascending) {
            std::sort(results.begin(), results.end(), 
                     [](const AuctionItem& a, const AuctionItem& b) {
                         return a.price < b.price;
                     });
        } else {
            std::sort(results.begin(), results.end(), 
                     [](const AuctionItem& a, const AuctionItem& b) {
                         return a.price > b.price;
                     });
        }
    } else if (sort_by == "time") {
        if (ascending) {
            std::sort(results.begin(), results.end(), 
                     [](const AuctionItem& a, const AuctionItem& b) {
                         return a.registration_time < b.registration_time;
                     });
        } else {
            std::sort(results.begin(), results.end(), 
                     [](const AuctionItem& a, const AuctionItem& b) {
                         return a.registration_time > b.registration_time;
                     });
        }
    } else if (sort_by == "name") {
        if (ascending) {
            std::sort(results.begin(), results.end(), 
                     [](const AuctionItem& a, const AuctionItem& b) {
                         return a.item.name < b.item.name;
                     });
        } else {
            std::sort(results.begin(), results.end(), 
                     [](const AuctionItem& a, const AuctionItem& b) {
                         return a.item.name > b.item.name;
                     });
        }
    }
    // 기본적으로 정렬하지 않음 (이미 인덱스에 의해 정렬된 경우)
}

void AuctionHouse::applyPagination(std::vector<AuctionItem>& results, 
                                  uint32_t page, 
                                  uint32_t items_per_page) {
    if (results.empty() || items_per_page == 0) {
        return;
    }
    
    // 페이지 번호는 1부터 시작
    if (page < 1) {
        page = 1;
    }
    
    // 시작 인덱스 계산
    size_t start_index = (page - 1) * items_per_page;
    
    // 결과 세트가 요청된 페이지보다 작은 경우
    if (start_index >= results.size()) {
        results.clear();
        return;
    }
    
    // 종료 인덱스 계산
    size_t end_index = std::min(start_index + items_per_page, results.size());
    
    // 결과 벡터 슬라이싱
    results = std::vector<AuctionItem>(
        results.begin() + start_index,
        results.begin() + end_index
    );
}

void AuctionHouse::indexItemKeywords(uint64_t auction_id, const Item& item) {
    // 아이템 이름을 단어로 분리
    std::string name = item.name;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    
    std::istringstream iss(name);
    std::string word;
    
    while (iss >> word) {
        // 최소 길이 필터링 (너무 짧은 단어는 인덱싱하지 않음)
        if (word.length() >= 3) {
            keyword_index_[word].insert(auction_id);
        }
    }
    
    // 아이템 이름 전체도 키워드로 인덱싱
    keyword_index_[name].insert(auction_id);
}

