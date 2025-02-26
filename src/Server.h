#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;

class AuctionServer {
private:
    boost::asio::io_context io_context_;
    tcp::acceptor acceptor_;
    std::vector<std::thread> worker_threads_;
    bool running_;
    
public:
    AuctionServer(int port, int thread_count);
    ~AuctionServer();
    
    void start();
    void stop();
    
private:
    void accept_connection();
    void handle_new_connection(tcp::socket socket);
    void run_worker();
};
