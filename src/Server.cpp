#include "Server.h"
#include <iostream>
#include <boost/bind.hpp>
#include <spdlog/spdlog.h>

AuctionServer::AuctionServer(int port, int thread_count, const std::string& db_connection_string)
    : acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)),
      running_(false) {
    
    // 데이터베이스 매니저 초기화
    db_manager_ = std::make_shared<DatabaseManager>(db_connection_string);
    
    // 경매장 시스템 초기화
    auction_house_ = std::make_shared<AuctionHouse>(db_manager_);
    
    // 워커 스레드 수 설정
    worker_threads_.resize(thread_count);
    
    spdlog::info("Auction server initialized on port {}", port);
}

AuctionServer::~AuctionServer() {
    stop();
}

void AuctionServer::start() {
    if (running_) {
        return;
    }
    
    running_ = true;
    
    // 첫 연결 수락 시작
    accept_connection();
    
    // 워커 스레드 시작
    for (std::size_t i = 0; i < worker_threads_.size(); ++i) {
        worker_threads_[i] = std::thread(&AuctionServer::run_worker, this);
    }
    
    spdlog::info("Auction server started with {} worker threads", worker_threads_.size());
}

void AuctionServer::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // 모든 세션 종료
    for (auto& session : sessions_) {
        session->stop();
    }
    sessions_.clear();
    
    // io_context 중지
    io_context_.stop();
    
    // 모든 워커 스레드 조인
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    spdlog::info("Auction server stopped");
}

void AuctionServer::accept_connection() {
    auto new_session = std::make_shared<ClientSession>(
        tcp::socket(io_context_), auction_house_);
    
    acceptor_.async_accept(
        new_session->socket(),
        boost::bind(&AuctionServer::handle_new_connection, this, new_session,
                   boost::asio::placeholders::error));
}

void AuctionServer::handle_new_connection(std::shared_ptr<ClientSession> session, 
                                         const boost::system::error_code& error) {
    if (!error) {
        spdlog::info("New client connected: {}", 
                    session->socket().remote_endpoint().address().to_string());
        
        sessions_.push_back(session);
        session->start();
    } else {
        spdlog::error("Connection error: {}", error.message());
    }
    
    // 다음 연결 수락 준비
    accept_connection();
}

void AuctionServer::run_worker() {
    try {
        // io_context 실행 (이벤트 루프)
        io_context_.run();
    } catch (const std::exception& e) {
        spdlog::error("Worker thread exception: {}", e.what());
    }
}
