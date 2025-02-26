#include "Server.h"
#include <iostream>

AuctionServer::AuctionServer(int port, int thread_count)
    : acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)),
      running_(false) {
    
    // 워커 스레드 수 설정
    worker_threads_.resize(thread_count);
    
    std::cout << "Auction server initialized on port " << port << std::endl;
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
    
    std::cout << "Auction server started with " << worker_threads_.size() 
              << " worker threads" << std::endl;
}

void AuctionServer::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // io_context 중지
    io_context_.stop();
    
    // 모든 워커 스레드 조인
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    std::cout << "Auction server stopped" << std::endl;
}

void AuctionServer::accept_connection() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::cout << "New client connected: " 
                          << socket.remote_endpoint().address().to_string() << std::endl;
                
                handle_new_connection(std::move(socket));
            }
            
            // 다음 연결 수락 준비
            accept_connection();
        });
}

void AuctionServer::handle_new_connection(tcp::socket socket) {
    // 여기서 새 클라이언트 세션 처리
    // 실제 구현에서는 ClientSession 객체 생성
}

void AuctionServer::run_worker() {
    try {
        // io_context 실행 (이벤트 루프)
        io_context_.run();
    } catch (const std::exception& e) {
        std::cerr << "Worker thread exception: " << e.what() << std::endl;
    }
}
