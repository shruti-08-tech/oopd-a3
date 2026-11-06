
cmake_minimum_required(VERSION 3.16)
project(PandemicSimulation VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(pandemic_sim
    src/main.cpp
    src/Config.cpp
    src/Simulation.cpp
    src/ThreadPool.cpp
)

target_include_directories(pandemic_sim PRIVATE include)

find_package(Threads REQUIRED)
target_link_libraries(pandemic_sim PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(pandemic_sim PRIVATE /W4)
else()
    target_compile_options(pandemic_sim PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

include(CTest)
find_package(Python3 COMPONENTS Interpreter QUIET)

if(BUILD_TESTING AND Python3_Interpreter_FOUND)
    add_test(
        NAME part_a_correctness
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/test_part_a.py"
                "$<TARGET_FILE:pandemic_sim>"
    )

    add_test(
        NAME part_b_equivalence
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/test_part_b.py"
                "$<TARGET_FILE:pandemic_sim>"
    )
endif()
