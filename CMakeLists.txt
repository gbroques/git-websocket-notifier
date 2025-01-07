cmake_minimum_required(VERSION 3.15)
project(git-websocket-notifier CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(efsw REQUIRED)
find_package(simple-websocket-server REQUIRED)
find_package(Boost REQUIRED)
find_package(libgit2 REQUIRED)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} efsw::efsw simple-websocket-server::simple-websocket-server boost::boost libgit2::libgit2)
