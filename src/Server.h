#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <thread>
#include <vector>
#include "ClientSession.h"
#include "AuctionHouse.h"
#include "DatabaseManager.h"

using boost::asio::ip::tcp;

class AuctionServer {
private:
    boost::asio::io_context io_context_;
    tcp::acceptor acceptor_;
    std::vector<std::shared_ptr<ClientSession>> sessions_;
    std::shared_ptr<DatabaseManager> db_manager_;
    std::shared_ptr<AuctionHouse> auction_house_;
    
    // 스레드 풀 관리
    std::vector<std::thread> worker_threads_;
    bool running_;
    
public:
    AuctionServer(int port, int thread_count, const std::string& db_connection_string = "tcp://localhost:27017");
    ~AuctionServer();
    
    void start();
    void stop();
    
private:
    void accept_connection();
    void handle_new_connection(std::shared_ptr<ClientSession> session, const boost::system::error_code& error);
    void run_worker();
};
