# 테스트를 위한 CMake 설정
cmake_minimum_required(VERSION 3.14)

# FetchContent를 사용하여 GoogleTest 다운로드 및 빌드
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.16.0  # 최신 안정 버전으로 업데이트
)
# Windows: 부모 프로젝트의 컴파일러/링커 설정 덮어쓰기 방지
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 테스트 소스 파일 목록
set(TEST_SOURCES
    ServerTests.cpp
    LoadTest.cpp
)

# 서버 테스트 실행 파일 생성
add_executable(server_tests ServerTests.cpp)
target_link_libraries(server_tests
    PRIVATE
    gtest_main
    Boost::system
    Boost::thread
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    $<IF:$<TARGET_EXISTS:mongo::mongocxx_static>,mongo::mongocxx_static,mongo::mongocxx_shared>
    $<IF:$<TARGET_EXISTS:mongo::bsoncxx_static>,mongo::bsoncxx_static,mongo::bsoncxx_shared>
)

# Windows에서는 pthread가 필요 없음
if(NOT WIN32)
    target_link_libraries(server_tests PRIVATE pthread)
endif()

# 서버 테스트에 필요한 헤더 파일 경로 추가
target_include_directories(server_tests
    PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
)

# 부하 테스트 실행 파일 생성
add_executable(load_test LoadTest.cpp)
target_link_libraries(load_test
    PRIVATE
    Boost::system
    Boost::thread
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Windows에서는 pthread가 필요 없음
if(NOT WIN32)
    target_link_libraries(load_test PRIVATE pthread)
endif()

# 부하 테스트에 필요한 헤더 파일 경로 추가
target_include_directories(load_test
    PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
)

# GoogleTest 테스트 검색 활성화
include(GoogleTest)
gtest_discover_tests(server_tests)

# 부하 테스트 추가
add_test(NAME LoadTest COMMAND load_test --server localhost --port 9000 --clients 10 --duration 5)
