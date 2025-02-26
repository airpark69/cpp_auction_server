#include "Server.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    try {
        // 기본 설정값
        int port = 9000;
        int thread_count = std::thread::hardware_concurrency();
        
        // 명령줄 인자 파싱
        for (int i = 1; i < argc; i += 2) {
            std::string arg = argv[i];
            if (i + 1 < argc) {
                if (arg == "--port") {
                    port = std::stoi(argv[i + 1]);
                } else if (arg == "--threads") {
                    thread_count = std::stoi(argv[i + 1]);
                }
            }
        }
        
        std::cout << "Starting Auction Server on port " << port 
                  << " with " << thread_count << " threads" << std::endl;
        
        // 서버 생성 및 시작
        AuctionServer server(port, thread_count);
        server.start();
        
        // 종료 신호 대기
        std::cout << "Server running. Press Enter to stop." << std::endl;
        std::cin.get();
        
        // 서버 종료
        server.stop();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
