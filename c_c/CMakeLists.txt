cmake_minimum_required(VERSION 3.13)
project(HEMatrixTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SEAL_DIR "/home/guan/Desktop/SEAL-4.1.2/build/cmake")
find_package(SEAL 4.1.2 REQUIRED)

set(Eigen3_DIR "/usr/share/eigen3/cmake")
find_package(Eigen3 REQUIRED)

add_executable(test_qk
    qk_main.cpp
    HEMatrixMultiplier.cpp
)

add_executable(test_sv
    sv_main.cpp
    HEMatrixMultiplier.cpp
)

target_link_libraries(test_qk SEAL::seal Eigen3::Eigen)
target_link_libraries(test_sv SEAL::seal Eigen3::Eigen)

if(MSVC)
    target_compile_options(test_qk PRIVATE /O2)
    target_compile_options(test_sv PRIVATE /O2)
else()
    target_compile_options(test_qk PRIVATE -O3)
    target_compile_options(test_sv PRIVATE -O3)
endif()
