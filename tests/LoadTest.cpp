#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using boost::asio::ip::tcp;
using json = nlohmann::json;
using namespace std::chrono;

// 부하 테스트 클라이언트 클래스
class TestClient {
private:
    boost::asio::io_context& io_context_;
    tcp::socket socket_;
    std::string host_;
    int port_;
    uint32_t user_id_;
    std::string session_token_;
    bool connected_;
    std::mutex mutex_;

public:
    TestClient(boost::asio::io_context& io_context, const std::string& host, int port)
        : io_context_(io_context), socket_(io_context), host_(host), port_(port),
          user_id_(0), connected_(false) {
    }

    bool connect() {
        try {
            tcp::resolver resolver(io_context_);
            auto endpoints = resolver.resolve(host_, std::to_string(port_));
            
            boost::asio::connect(socket_, endpoints);
            connected_ = true;
            return true;
        } catch (const std::exception& e) {
            spdlog::error("Connection error: {}", e.what());
            return false;
        }
    }

    bool login(const std::string& username, const std::string& password) {
        if (!connected_) return false;

        json request = {
            {"type", "login"},
            {"username", username},
            {"password", password}
        };

        std::string response;
        if (!send_request(request.dump(), response)) {
            return false;
        }

        try {
            json response_json = json::parse(response);
            if (response_json["status"] == "success") {
                user_id_ = response_json["user_id"];
                session_token_ = response_json["session_token"];
                return true;
            }
        } catch (const std::exception& e) {
            spdlog::error("Login parse error: {}", e.what());
        }
        return false;
    }

    bool register_item(uint32_t item_id, const std::string& item_name, uint32_t price) {
        if (!connected_ || user_id_ == 0) return false;

        json request = {
            {"type", "register_item"},
            {"session_token", session_token_},
            {"item_id", item_id},
            {"item_name", item_name},
            {"price", price}
        };

        std::string response;
        if (!send_request(request.dump(), response)) {
            return false;
        }

        try {
            json response_json = json::parse(response);
            return response_json["status"] == "success";
        } catch (const std::exception& e) {
            spdlog::error("Register item parse error: {}", e.what());
            return false;
        }
    }

    bool search_items(const std::string& keyword, std::vector<uint64_t>& auction_ids) {
        if (!connected_) return false;

        json request = {
            {"type", "search_items"},
            {"keyword", keyword}
        };

        std::string response;
        if (!send_request(request.dump(), response)) {
            return false;
        }

        try {
            json response_json = json::parse(response);
            if (response_json["status"] == "success") {
                auction_ids.clear();
                for (const auto& item : response_json["items"]) {
                    auction_ids.push_back(item["auction_id"]);
                }
                return true;
            }
        } catch (const std::exception& e) {
            spdlog::error("Search items parse error: {}", e.what());
        }
        return false;
    }

    bool purchase_item(uint64_t auction_id) {
        if (!connected_ || user_id_ == 0) return false;

        json request = {
            {"type", "purchase_item"},
            {"session_token", session_token_},
            {"auction_id", auction_id}
        };

        std::string response;
        if (!send_request(request.dump(), response)) {
            return false;
        }

        try {
            json response_json = json::parse(response);
            return response_json["status"] == "success";
        } catch (const std::exception& e) {
            spdlog::error("Purchase item parse error: {}", e.what());
            return false;
        }
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connected_) {
            boost::system::error_code ec;
            socket_.close(ec);
            connected_ = false;
        }
    }

private:
    bool send_request(const std::string& request, std::string& response) {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            // 메시지 길이 전송 (네트워크 바이트 순서)
            uint32_t length = htonl(static_cast<uint32_t>(request.size()));
            boost::asio::write(socket_, boost::asio::buffer(&length, sizeof(length)));
            
            // 메시지 본문 전송
            boost::asio::write(socket_, boost::asio::buffer(request));
            
            // 응답 길이 읽기
            uint32_t response_length = 0;
            boost::asio::read(socket_, boost::asio::buffer(&response_length, sizeof(response_length)));
            response_length = ntohl(response_length);
            
            // 응답 본문 읽기
            std::vector<char> buffer(response_length);
            boost::asio::read(socket_, boost::asio::buffer(buffer));
            
            response = std::string(buffer.begin(), buffer.end());
            return true;
        } catch (const std::exception& e) {
            spdlog::error("Request error: {}", e.what());
            connected_ = false;
            return false;
        }
    }
};

// 부하 테스트 통계 클래스
class LoadTestStats {
private:
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> successful_requests_{0};
    std::atomic<uint64_t> failed_requests_{0};
    std::atomic<uint64_t> total_response_time_{0}; // 마이크로초 단위
    std::mutex mutex_;
    high_resolution_clock::time_point start_time_;

public:
    LoadTestStats() : start_time_(high_resolution_clock::now()) {}

    void record_request(bool success, uint64_t response_time_us) {
        total_requests_++;
        if (success) {
            successful_requests_++;
        } else {
            failed_requests_++;
        }
        total_response_time_ += response_time_us;
    }

    void print_stats() {
        auto now = high_resolution_clock::now();
        auto duration = duration_cast<seconds>(now - start_time_).count();
        
        if (duration == 0) duration = 1; // 0으로 나누기 방지
        
        uint64_t total = total_requests_.load();
        uint64_t success = successful_requests_.load();
        uint64_t failed = failed_requests_.load();
        uint64_t total_time = total_response_time_.load();
        
        double avg_response_time = total > 0 ? static_cast<double>(total_time) / total / 1000.0 : 0; // 밀리초로 변환
        double requests_per_second = static_cast<double>(total) / duration;
        
        std::cout << "\n===== Load Test Results =====\n";
        std::cout << "Duration: " << duration << " seconds\n";
        std::cout << "Total Requests: " << total << "\n";
        std::cout << "Successful Requests: " << success << " (" << (total > 0 ? (success * 100.0 / total) : 0) << "%)\n";
        std::cout << "Failed Requests: " << failed << " (" << (total > 0 ? (failed * 100.0 / total) : 0) << "%)\n";
        std::cout << "Average Response Time: " << avg_response_time << " ms\n";
        std::cout << "Requests Per Second: " << requests_per_second << "\n";
        std::cout << "==============================\n";
    }
};

// 부하 테스트 워커 스레드 함수
void worker_thread(int thread_id, const std::string& host, int port, int operations, LoadTestStats& stats) {
    // 난수 생성기 초기화
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> op_dist(0, 2); // 0: register, 1: search, 2: purchase
    std::uniform_int_distribution<> price_dist(100, 10000);
    
    // IO 컨텍스트 및 클라이언트 생성
    boost::asio::io_context io_context;
    TestClient client(io_context, host, port);
    
    // 서버에 연결
    if (!client.connect()) {
        spdlog::error("Thread {}: Failed to connect to server", thread_id);
        return;
    }
    
    // 로그인
    std::string username = "user" + std::to_string(thread_id);
    std::string password = "password" + std::to_string(thread_id);
    
    auto login_start = high_resolution_clock::now();
    bool login_success = client.login(username, password);
    auto login_end = high_resolution_clock::now();
    
    stats.record_request(login_success, duration_cast<microseconds>(login_end - login_start).count());
    
    if (!login_success) {
        spdlog::error("Thread {}: Login failed", thread_id);
        client.close();
        return;
    }
    
    // 아이템 ID 기준값 (스레드별로 다른 범위 사용)
    uint32_t base_item_id = thread_id * 10000;
    
    // 검색 결과를 저장할 벡터
    std::vector<uint64_t> auction_ids;
    
    // 지정된 횟수만큼 작업 수행
    for (int i = 0; i < operations; i++) {
        int operation = op_dist(gen);
        bool success = false;
        auto start_time = high_resolution_clock::now();
        
        switch (operation) {
            case 0: { // 아이템 등록
                uint32_t item_id = base_item_id + i;
                std::string item_name = "Item " + std::to_string(item_id);
                uint32_t price = price_dist(gen);
                
                success = client.register_item(item_id, item_name, price);
                break;
            }
            case 1: { // 아이템 검색
                success = client.search_items("Item", auction_ids);
                break;
            }
            case 2: { // 아이템 구매
                // 검색 결과가 없으면 먼저 검색 수행
                if (auction_ids.empty()) {
                    client.search_items("Item", auction_ids);
                }
                
                // 구매할 아이템이 있으면 구매 시도
                if (!auction_ids.empty()) {
                    // 랜덤하게 아이템 선택
                    std::uniform_int_distribution<> item_dist(0, auction_ids.size() - 1);
                    uint64_t auction_id = auction_ids[item_dist(gen)];
                    
                    success = client.purchase_item(auction_id);
                    
                    // 구매 성공 시 목록에서 제거
                    if (success) {
                        auction_ids.erase(std::remove(auction_ids.begin(), auction_ids.end(), auction_id), auction_ids.end());
                    }
                } else {
                    // 구매할 아이템이 없으면 아이템 등록으로 대체
                    uint32_t item_id = base_item_id + i + 1000;
                    std::string item_name = "Item " + std::to_string(item_id);
                    uint32_t price = price_dist(gen);
                    
                    success = client.register_item(item_id, item_name, price);
                }
                break;
            }
        }
        
        auto end_time = high_resolution_clock::now();
        stats.record_request(success, duration_cast<microseconds>(end_time - start_time).count());
        
        // 요청 간 짧은 대기 시간 추가
        std::this_thread::sleep_for(milliseconds(10));
    }
    
    // 연결 종료
    client.close();
}

int main(int argc, char* argv[]) {
    // 기본 설정값
    std::string host = "localhost";
    int port = 9000;
    int client_count = 10;
    int operations_per_client = 100;
    int duration_seconds = 60;
    
    // 명령줄 인자 파싱
    for (int i = 1; i < argc; i += 2) {
        std::string arg = argv[i];
        if (i + 1 < argc) {
            if (arg == "--server") {
                host = argv[i + 1];
            } else if (arg == "--port") {
                port = std::stoi(argv[i + 1]);
            } else if (arg == "--clients") {
                client_count = std::stoi(argv[i + 1]);
            } else if (arg == "--operations") {
                operations_per_client = std::stoi(argv[i + 1]);
            } else if (arg == "--duration") {
                duration_seconds = std::stoi(argv[i + 1]);
            }
        }
    }
    
    // 로깅 설정
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Starting load test with {} clients, {} operations per client", client_count, operations_per_client);
    spdlog::info("Target server: {}:{}", host, port);
    
    // 통계 객체 생성
    LoadTestStats stats;
    
    // 워커 스레드 생성
    std::vector<std::thread> threads;
    for (int i = 0; i < client_count; i++) {
        threads.emplace_back(worker_thread, i, host, port, operations_per_client, std::ref(stats));
    }
    
    // 지정된 시간 동안 실행
    std::this_thread::sleep_for(seconds(duration_seconds));
    
    // 모든 스레드 종료 대기
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    // 최종 통계 출력
    stats.print_stats();
    
    return 0;
}

