cmake_minimum_required(VERSION 3.20)
project(mini_dbms LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 메모리 실수(배열 넘침, 해제한 메모리 사용)를 잡는 검사. 속도를 잴 때는 -DDBMS_SANITIZE=OFF로 끈다.
option(DBMS_SANITIZE "AddressSanitizer 켜기" ON)
# 스레드끼리 락 없이 같은 데이터를 건드리는지 잡는 검사. AddressSanitizer와 같이 못 쓴다.
# 켤 때: -DDBMS_SANITIZE=OFF -DDBMS_TSAN=ON. WSL2에서는 setarch $(uname -m) -R ./실행파일 로 돌린다.
option(DBMS_TSAN "ThreadSanitizer 켜기" OFF)

add_compile_options(-Wall -Wextra -g)
if(DBMS_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
elseif(DBMS_SANITIZE)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

find_package(Threads REQUIRED)
# httplib는 .so로 컴파일된 판이라, 그걸 만들 때 쓴 설정을 pkg-config로 똑같이 받아야 한다 (팀 환경 메모)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HTTPLIB REQUIRED IMPORTED_TARGET cpp-httplib)

# DB 엔진: SQL 처리기 + B+ 트리 + 스레드 풀. 명령창, 서버, 테스트가 같이 쓴다.
add_library(dbms_core
    src/bplus_tree.cpp
    src/tokenizer.cpp
    src/parser.cpp
    src/schema.cpp
    src/storage.cpp
    src/database.cpp
    src/executor.cpp
    src/sql_runner.cpp
    src/thread_pool.cpp
)
target_include_directories(dbms_core PUBLIC src)
target_link_libraries(dbms_core PUBLIC Threads::Threads)

# HTTP API: 엔진을 httplib 서버에 연결하는 부분
add_library(dbms_http src/http_api.cpp)
target_link_libraries(dbms_http PUBLIC dbms_core PkgConfig::HTTPLIB)

add_executable(sqlproc src/main.cpp)
target_link_libraries(sqlproc PRIVATE dbms_core)

add_executable(dbms_server src/server_main.cpp)
target_link_libraries(dbms_server PRIVATE dbms_http)

# 테스트
enable_testing()
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(unit_tests
    tests/bplus_tree_test.cpp
    tests/tokenizer_test.cpp
    tests/parser_test.cpp
    tests/storage_test.cpp
    tests/schema_test.cpp
    tests/executor_test.cpp
    tests/thread_pool_test.cpp
    tests/concurrency_test.cpp
    tests/http_api_test.cpp
)
target_link_libraries(unit_tests PRIVATE dbms_http GTest::gtest_main)
# 테스트 목록은 ctest를 돌릴 때 읽는다. 빌드 직후에 읽으면 TSan 빌드가 WSL2에서 죽어 빌드가 실패한다.
gtest_discover_tests(unit_tests DISCOVERY_MODE PRE_TEST)

# 기능 테스트: sqlproc에 SQL 파일을 넣고 출력이 기대값과 같은지 비교한다.
add_test(NAME functional_basic
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/functional/run_functional_test.sh
            $<TARGET_FILE:sqlproc> ${CMAKE_SOURCE_DIR}/tests/functional
)
