#include "ClientSession.h"
#include <iostream>
#include <spdlog/spdlog.h>
#include "Protocol.h"

ClientSession::ClientSession(tcp::socket socket, std::shared_ptr<AuctionHouse> auction_house)
    : socket_(std::move(socket)),
      user_id_(0),
      authenticated_(false),
      auction_house_(auction_house),
      is_writing_(false) {
        initialize_handlers();
}

ClientSession::~ClientSession() {
    stop();
}

void ClientSession::start() {
    // 클라이언트로부터 메시지 헤더 읽기 시작
    read_header();
}

void ClientSession::stop() {
    // 소켓이 열려있으면 닫기
    if (socket_.is_open()) {
        boost::system::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
}

void ClientSession::read_header() {
    auto self = shared_from_this();
    
    // 4바이트 메시지 길이 헤더 읽기 -- 메시지 길이
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(&current_message_size_, sizeof(uint32_t)),
        [this, self](const boost::system::error_code& error, size_t bytes_transferred) {
            handle_read_header(error, bytes_transferred);
        });
}

void ClientSession::handle_read_header(const boost::system::error_code& error, size_t bytes_transferred) {
    if (!error && bytes_transferred == sizeof(uint32_t)) {
        // 네트워크 바이트 순서에서 호스트 바이트 순서로 변환
        uint32_t message_size = ntohl(current_message_size_);
        
        // 메시지 크기가 최대 허용 크기를 초과하는지 확인
        if (message_size > Protocol::MAX_MESSAGE_SIZE) {
            spdlog::error("Message size too large: {}", message_size);
            stop();
            return;
        }
        
        // 메시지 본문 읽기
        read_body(message_size);
    } else {
        if (error != boost::asio::error::eof) {
            spdlog::error("Header read error: {}", error.message());
        }
        stop();
    }
}

void ClientSession::read_body(uint32_t body_size) {
    auto self = shared_from_this();
    
    // 메시지 본문 읽기
    boost::asio::async_read(
        socket_,
        read_buffer_,
        boost::asio::transfer_exactly(body_size),
        [this, self](const boost::system::error_code& error, size_t bytes_transferred) {
            handle_read_body(error, bytes_transferred);
        });
}

void ClientSession::handle_read_body(const boost::system::error_code& error, size_t bytes_transferred) {
    if (!error) {
        // 버퍼에서 메시지 추출
        std::string message(
            boost::asio::buffer_cast<const char*>(read_buffer_.data()),
            bytes_transferred);
        read_buffer_.consume(bytes_transferred);
        
        // 메시지 처리
        process_message(message);
        
        // 다음 메시지 헤더 읽기
        read_header();
    } else {
        if (error != boost::asio::error::eof) {
            spdlog::error("Body read error: {}", error.message());
        }
        stop();
    }
}

void ClientSession::initialize_handlers() {
    message_handlers_[Protocol::MessageType::LOGIN] = [this](const json& data) { handle_login(data); };
    message_handlers_[Protocol::MessageType::REGISTER_ITEM] = [this](const json& data) { 
        if (check_authentication()) handle_register_item(data); 
    };
    message_handlers_[Protocol::MessageType::SEARCH_ITEMS] = [this](const json& data) { handle_search_items(data); };
    message_handlers_[Protocol::MessageType::PURCHASE_ITEM] = [this](const json& data) { 
        if (check_authentication()) handle_purchase_item(data); 
    };
    message_handlers_[Protocol::MessageType::CANCEL_LISTING] = [this](const json& data) { 
        if (check_authentication()) handle_cancel_listing(data); 
    };
}

bool ClientSession::check_authentication() {
    if (!authenticated_) {
        send_error("Authentication required");
        return false;
    }
    return true;
}


void ClientSession::process_message(const json& data) {
    Protocol::MessageType msg_type = data["type"];
    auto handler = message_handlers_.find(msg_type);
    if (handler != message_handlers_.end()) {
        handler->second(data);
    } else {
        send_error("Unknown message type: " + std::to_string(static_cast<int>(msg_type)));
    }
}

void ClientSession::send(const std::string& message) {
    auto self = shared_from_this();
    
    // 스레드 안전을 위한 락 사용 --> 이 부분이 비동기 작업인 handle_write()의 뮤텍스와 연결되어서 일관성을 보장함
    std::lock_guard<std::mutex> lock(write_mutex_);
    
    // 메시지를 큐에 추가
    write_queue_.push(message);
    
    // 현재 쓰기 작업이 진행 중이 아니면 새 쓰기 작업 시작
    if (!is_writing_) {
        write();
    }
}

void ClientSession::write() {
    auto self = shared_from_this();
    is_writing_ = true;
    
    // 큐에서 다음 메시지 가져오기
    std::string& message = write_queue_.front();
    
    // 메시지 길이 계산 (네트워크 바이트 순서로 변환)
    uint32_t length = htonl(static_cast<uint32_t>(message.size()));
    
    // 헤더와 메시지를 함께 전송하기 위한 버퍼 준비
    std::vector<boost::asio::const_buffer> buffers;
    buffers.push_back(boost::asio::buffer(&length, sizeof(length)));
    buffers.push_back(boost::asio::buffer(message));
    
    // 비동기 쓰기 작업 시작
    boost::asio::async_write(
        socket_,
        buffers,
        [this, self](const boost::system::error_code& error, size_t /*bytes_transferred*/) {
            handle_write(error);
        });
}

void ClientSession::handle_write(const boost::system::error_code& error) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    
    // 쓰기 작업이 완료된 메시지 제거
    write_queue_.pop();
    
    if (!error) {
        // 큐에 더 메시지가 있으면 계속 쓰기
        if (!write_queue_.empty()) {
            write();
        } else {
            is_writing_ = false;
        }
    } else {
        spdlog::error("Write error: {}", error.message());
        is_writing_ = false;
        stop();
    }
}

void ClientSession::handle_login(const json& data) {
    // 필수 필드 확인
    if (!data.contains("username") || !data.contains("password")) {
        send_error("Missing username or password");
        return;
    }
    
    std::string username = data["username"];
    std::string password = data["password"];
    
    // 실제 구현에서는 데이터베이스에서 사용자 인증 수행
    // 여기서는 간단한 예시로 구현
    if (username == "user123" && password == "password123") {
        user_id_ = 1001;  // 실제로는 DB에서 가져온 사용자 ID
        authenticated_ = true;
        
        // 세션 토큰 생성 (실제로는 더 안전한 방법 사용)
        std::string session_token = "session_" + std::to_string(user_id_) + "_" + 
                                   std::to_string(std::time(nullptr));
        
        // 성공 응답 전송
        json response = {
            {"status", Protocol::ResponseStatus::SUCCESS},
            {"user_id", user_id_},
            {"session_token", session_token}
        };
        
        send_success(response);
        spdlog::info("User {} logged in", username);
    } else {
        send_error("Invalid username or password");
    }
}

void ClientSession::handle_register_item(const json& data) {
    // 필수 필드 확인
    if (!data.contains("item_id") || !data.contains("item_name") || !data.contains("price")) {
        send_error("Missing item information");
        return;
    }
    
    uint32_t item_id = data["item_id"];
    std::string item_name = data["item_name"];
    uint32_t price = data["price"];
    
    // 아이템 객체 생성
    Item item;
    item.id = item_id;
    item.name = item_name;
    item.owner_id = user_id_;
    
    // 경매장에 아이템 등록
    uint64_t auction_id = auction_house_->registerItem(user_id_, item, price);
    
    if (auction_id > 0) {
        // 성공 응답 전송
        json response = {
            {"status", "success"},
            {"auction_id", auction_id}
        };
        
        send_success(response);
        spdlog::info("User {} registered item {} for {}", user_id_, item_name, price);
    } else {
        send_error("Failed to register item");
    }
}

void ClientSession::handle_search_items(const json& data) {
    // 검색 조건 구성
    SearchCriteria criteria;
    
    if (data.contains("keyword") && data["keyword"].is_string()) {
        criteria.keyword = data["keyword"];
    }
    
    if (data.contains("min_price") && data["min_price"].is_number()) {
        criteria.min_price = data["min_price"];
    }
    
    if (data.contains("max_price") && data["max_price"].is_number()) {
        criteria.max_price = data["max_price"];
    }
    
    if (data.contains("item_type") && data["item_type"].is_number()) {
        criteria.item_type = data["item_type"];
    }
    
    if (data.contains("seller_name") && data["seller_name"].is_string()) {
        criteria.seller_name = data["seller_name"];
    }
    
    // 페이지네이션 설정
    if (data.contains("page") && data["page"].is_number()) {
        criteria.page = data["page"];
    }
    
    if (data.contains("items_per_page") && data["items_per_page"].is_number()) {
        criteria.items_per_page = data["items_per_page"];
    }
    
    // 정렬 설정
    if (data.contains("sort_by") && data["sort_by"].is_string()) {
        criteria.sort_by = data["sort_by"];
    }
    
    if (data.contains("sort_order") && data["sort_order"].is_string()) {
        criteria.sort_order = data["sort_order"];
    }
    
    // 경매장에서 아이템 검색
    std::vector<AuctionItem> items = auction_house_->searchItems(criteria);
    
    // 결과를 JSON 배열로 변환
    json items_json = json::array();
    for (const auto& item : items) {
        items_json.push_back({
            {"auction_id", item.id},
            {"item_id", item.item.id},
            {"item_name", item.item.name},
            {"seller_id", item.seller_id},
            {"price", item.price},
            {"registration_time", item.registration_time}
        });
    }
    
    // 응답 전송
    json response = {
        {"status", "success"},
        {"items", items_json},
        {"total_count", items.size()},
        {"page", criteria.page},
        {"items_per_page", criteria.items_per_page}
    };
    
    send_success(response);
    spdlog::info("Search performed with keyword '{}', found {} items", 
                criteria.keyword, items.size());
}

void ClientSession::handle_purchase_item(const json& data) {
    // 필수 필드 확인
    if (!data.contains("auction_id")) {
        send_error("Missing auction_id");
        return;
    }
    
    uint64_t auction_id = data["auction_id"];
    
    // 경매장에서 아이템 구매
    bool success = auction_house_->purchaseItem(auction_id, user_id_);
    
    if (success) {
        // 성공 응답 전송
        json response = {
            {"status", "success"},
            {"message", "Item purchased successfully"}
        };
        
        send_success(response);
        spdlog::info("User {} purchased item {}", user_id_, auction_id);
    } else {
        send_error("Failed to purchase item");
    }
}

void ClientSession::handle_cancel_listing(const json& data) {
    // 필수 필드 확인
    if (!data.contains("auction_id")) {
        send_error("Missing auction_id");
        return;
    }
    
    uint64_t auction_id = data["auction_id"];
    
    // 경매장에서 등록 취소
    bool success = auction_house_->cancelListing(auction_id, user_id_);
    
    if (success) {
        // 성공 응답 전송
        json response = {
            {"status", "success"},
            {"message", "Listing cancelled successfully"}
        };
        
        send_success(response);
        spdlog::info("User {} cancelled listing {}", user_id_, auction_id);
    } else {
        send_error("Failed to cancel listing");
    }
}

void ClientSession::send_error(const std::string& message) {
    json response = {
        {"status", Protocol::ResponseStatus::FAILURE},
        {"message", message}
    };
    
    send(response.dump());
    spdlog::warn("Error sent to client: {}", message);
}

void ClientSession::send_success(const json& data) {
    json response = data;
    if (!response.contains("status")) {
        response["status"] = Protocol::ResponseStatus::SUCCESS;
    }
    
    send(response.dump());
}


