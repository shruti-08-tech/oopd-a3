
cmake_minimum_required(VERSION 3.16)
project(PandemicSimulation VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(pandemic_core STATIC
    src/Config.cpp
    src/Simulation.cpp
    src/ThreadPool.cpp
    src/SnapshotWriter.cpp
)

target_include_directories(pandemic_core PUBLIC include)
target_link_libraries(pandemic_core PUBLIC Threads::Threads)

add_executable(pandemic_sim src/main.cpp)
target_link_libraries(pandemic_sim PRIVATE pandemic_core)

add_executable(pandemic_sweep src/sweep_main.cpp)
target_link_libraries(pandemic_sweep PRIVATE pandemic_core)

if(MSVC)
    target_compile_options(pandemic_core PRIVATE /W4)
    target_compile_options(pandemic_sweep PRIVATE /W4)
else()
    target_compile_options(pandemic_core PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_compile_options(pandemic_sweep PRIVATE
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

    add_test(
        NAME part_c_async_and_sweep
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/test_part_c.py"
                "$<TARGET_FILE:pandemic_sim>"
                "$<TARGET_FILE:pandemic_sweep>"
    )
endif()
