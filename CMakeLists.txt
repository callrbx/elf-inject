cmake_minimum_required(VERSION 3.20)

project(dlinject VERSION 0.2.0 DESCRIPTION "Small Linux shared-library injector" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(dlinject_lib src/elf.cpp src/inject.cpp src/maps.cpp src/trace.cpp src/version.cpp)
add_library(dlinject::lib ALIAS dlinject_lib)
target_include_directories(dlinject_lib PUBLIC ${PROJECT_SOURCE_DIR}/include PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dlinject_lib PRIVATE ${CMAKE_DL_LIBS})
target_compile_options(dlinject_lib PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(dlinject src/main.cpp)
target_link_libraries(dlinject PRIVATE dlinject::lib)
option(DLINJECT_PORTABLE "Link the C++ and compiler runtimes into the injector" ON)
if(DLINJECT_PORTABLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++.a
        OUTPUT_VARIABLE DLINJECT_STATIC_STDCXX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(IS_ABSOLUTE "${DLINJECT_STATIC_STDCXX}" AND EXISTS "${DLINJECT_STATIC_STDCXX}")
        target_link_options(dlinject PRIVATE -static-libstdc++ -static-libgcc)
    else()
        message(STATUS "Static libstdc++ unavailable; using the shared runtime")
    endif()
endif()

add_executable(dlinject-target demo/target.cpp)
target_link_libraries(dlinject-target PRIVATE Threads::Threads)
target_compile_options(dlinject-target PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(dlinject-example SHARED demo/library.cpp)
set_target_properties(dlinject-example PROPERTIES PREFIX "" OUTPUT_NAME "dlinject-example")
target_compile_options(dlinject-example PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
if(BUILD_TESTING)
    add_executable(dlinject_tests tests/tests.cpp)
    target_link_libraries(dlinject_tests PRIVATE dlinject::lib)
    target_compile_definitions(dlinject_tests PRIVATE
        DLINJECT_BIN="$<TARGET_FILE:dlinject>"
        TARGET_BIN="$<TARGET_FILE:dlinject-target>"
        EXAMPLE_LIB="$<TARGET_FILE:dlinject-example>"
    )
    add_test(NAME dlinject_tests COMMAND dlinject_tests)
    set_tests_properties(dlinject_tests PROPERTIES TIMEOUT 20)
endif()

install(TARGETS dlinject RUNTIME DESTINATION bin)
