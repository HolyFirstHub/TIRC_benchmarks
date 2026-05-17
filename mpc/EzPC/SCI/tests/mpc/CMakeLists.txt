cmake_minimum_required(VERSION 3.10)
project(TestOps)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LINK_LIBS SCI-Math SCI-FloatingPoint)

macro(add_test_exec _name)
    add_executable(test-${_name} test-${_name}.cpp)
    target_link_libraries(test-${_name} ${LINK_LIBS})
endmacro()

add_test_exec(gelu)
add_test_exec(softmax)
add_test_exec(layernorm)
add_test_exec(silu)
add_test_exec(rms)


if(MSVC)
    foreach(target_name gelu softmax layernorm silu rms )
        target_compile_options(test-${target_name} PRIVATE /O2)
    endforeach()
else()
    foreach(target_name gelu softmax layernorm silu rms )
        target_compile_options(test-${target_name} PRIVATE -O3)
    endforeach()
endif()
