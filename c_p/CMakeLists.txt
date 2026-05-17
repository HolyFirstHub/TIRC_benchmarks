cmake_minimum_required(VERSION 3.13)
project(HEMatrixTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SEAL 4.1 REQUIRED)

add_executable(test_matrix
    main.cpp
    HEMatrixMultiplier.cpp
)

target_link_libraries(test_matrix SEAL::seal)

if(MSVC)
    target_compile_options(test_matrix PRIVATE /O2)
else()
    target_compile_options(test_matrix PRIVATE -O3)
endif()
