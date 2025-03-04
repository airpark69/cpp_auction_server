#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <queue>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>
#include "AuctionHouse.h"

using boost::asio::ip::tcp;
using json = nlohmann::json;

class ClientSession : public std::enable_shared_from_this<ClientSession> {
private:
    tcp::socket socket_;
    boost::asio::streambuf read_buffer_;
    uint32_t current_message_size_;
    std::queue<std::string> write_queue_;
    std::mutex write_mutex_;
    uint32_t user_id_;
    bool authenticated_;
    std::shared_ptr<AuctionHouse> auction_house_;
    bool is_writing_;
    
public:
    ClientSession(tcp::socket socket, std::shared_ptr<AuctionHouse> auction_house);
    ~ClientSession();
    
    void start();
    void stop();
    tcp::socket& socket() { return socket_; }
    void send(const std::string& message);
    bool is_authenticated() const { return authenticated_; }
    uint32_t get_user_id() const { return user_id_; }
    
private:
    void read_header();
    void handle_read_header(const boost::system::error_code& error, size_t bytes_transferred);
    void read_body(uint32_t body_size);
    void handle_read_body(const boost::system::error_code& error, size_t bytes_transferred);
    void process_message(const std::string& message);
    void write();
    void handle_write(const boost::system::error_code& error);
    
    // 메시지 처리 핸들러
    void handle_login(const json& data);
    void handle_register_item(const json& data);
    void handle_search_items(const json& data);
    void handle_purchase_item(const json& data);
    void handle_cancel_listing(const json& data);
    
    // 유틸리티 함수
    void send_error(const std::string& message);
    void send_success(const json& data = json::object());
};
