cmake_minimum_required(VERSION 3.14)
project(auction_server VERSION 1.0.0 LANGUAGES CXX)

# C++17 표준 사용
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 빌드 타입 설정
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# MongoDB 드라이버 경로 설정 (필요한 경우 경로 수정)
list(APPEND CMAKE_PREFIX_PATH "C:/mongo-cxx-driver")

# 외부 라이브러리 찾기
if(CMAKE_VERSION VERSION_LESS 3.27)
  find_package(Boost REQUIRED COMPONENTS system thread)
else()
  find_package(Boost CONFIG REQUIRED COMPONENTS system thread)
endif()
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(mongocxx REQUIRED)
find_package(bsoncxx REQUIRED)

# MongoDB 드라이버 정보 출력 (디버깅용)
message(STATUS "MongoDB C++ Driver found: ${mongocxx_FOUND}")
message(STATUS "MongoDB C++ Driver version: ${mongocxx_VERSION}")
message(STATUS "MongoDB C++ Driver include dirs: ${mongocxx_INCLUDE_DIRS}")

# 헤더 파일 경로 추가
include_directories(${PROJECT_SOURCE_DIR}/include)

# 소스 파일 목록
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.h")

# 실행 파일 생성
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 라이브러리 링크 - 정적 또는 동적 라이브러리 자동 선택
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Boost::system
    Boost::thread
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    $<IF:$<TARGET_EXISTS:mongo::mongocxx_static>,mongo::mongocxx_static,mongo::mongocxx_shared>
    $<IF:$<TARGET_EXISTS:mongo::bsoncxx_static>,mongo::bsoncxx_static,mongo::bsoncxx_shared>
)

# Windows에서는 pthread가 필요 없음
if(NOT WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# 테스트 디렉토리가 존재하는 경우에만 추가
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

# 설치 설정
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
