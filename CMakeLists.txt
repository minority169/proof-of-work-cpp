cmake_minimum_required(VERSION 3.5)

project(proof-of-work-cpp)

set(CMAKE_CXX_STANDARD 11)

find_package(PkgConfig)

pkg_check_modules(PC_ZeroMQ QUIET zmq)

find_path(ZeroMQ_INCLUDE_DIR
        NAMES zmq.hpp
        PATHS ${PC_ZeroMQ_INCLUDE_DIRS}
        )

find_library(ZeroMQ_LIBRARY
        NAMES zmq
        PATHS ${PC_ZeroMQ_LIBRARY_DIRS}
        )

find_library(SSL ssl)

find_library(CRYPTO crypto)

set(SOURCE_FILES src/node.cpp)

add_executable(run ${SOURCE_FILES})

target_include_directories(run PUBLIC ${ZeroMQ_INCLUDE_DIR})

target_link_libraries(run PUBLIC ${Boost_LIBRARIES} ${ZeroMQ_LIBRARY} ${SSL} ${CRYPTO})
