cmake_minimum_required(VERSION 3.16)
project(poc_hbdt_sumpath LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(SEAL_HINT_DIR "" CACHE PATH "Path to the directory that contains SEALConfig.cmake")
set(SEAL_ROOT "" CACHE PATH "Microsoft SEAL installation prefix")

if(DEFINED ENV{SEAL_DIR} AND NOT SEAL_DIR)
    set(SEAL_DIR "$ENV{SEAL_DIR}" CACHE PATH "SEAL config directory from environment" FORCE)
endif()

if(DEFINED ENV{SEAL_ROOT} AND NOT SEAL_ROOT)
    set(SEAL_ROOT "$ENV{SEAL_ROOT}" CACHE PATH "SEAL installation prefix from environment" FORCE)
endif()

if(SEAL_HINT_DIR)
    list(APPEND CMAKE_PREFIX_PATH "${SEAL_HINT_DIR}")
endif()

if(SEAL_ROOT)
    list(APPEND CMAKE_PREFIX_PATH
        "${SEAL_ROOT}"
        "${SEAL_ROOT}/lib/cmake"
        "${SEAL_ROOT}/lib/cmake/SEAL"
        "${SEAL_ROOT}/share/cmake/SEAL"
        "${SEAL_ROOT}/lib64/cmake"
        "${SEAL_ROOT}/lib64/cmake/SEAL")
endif()

find_package(SEAL CONFIG)

set(SOURCES_COMMON
    TreeNode.cpp
    HardTree.cpp
    SoftTree.cpp
    SoftStepApprox.cpp
    PolyEval.cpp
    OneHotEncoder.cpp
    DataLayout.cpp
    Metrics.cpp
    TreeExporter.cpp
    ClearInference.cpp
)

set(SOURCES_HE
    ${SOURCES_COMMON}
    HEInference.cpp
)

add_library(project_warnings INTERFACE)
if(MSVC)
    target_compile_options(project_warnings INTERFACE /W4)
else()
    target_compile_options(project_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_library(project_options INTERFACE)
target_include_directories(project_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(project_options_he INTERFACE)
target_include_directories(project_options_he INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

set(HAS_SEAL_TARGET FALSE)
if(TARGET SEAL::seal)
    target_link_libraries(project_options_he INTERFACE SEAL::seal)
    set(HAS_SEAL_TARGET TRUE)
elseif(TARGET SEAL::seal_shared)
    target_link_libraries(project_options_he INTERFACE SEAL::seal_shared)
    set(HAS_SEAL_TARGET TRUE)
elseif(TARGET seal)
    target_link_libraries(project_options_he INTERFACE seal)
    set(HAS_SEAL_TARGET TRUE)
endif()

add_executable(poc_clear main_clear.cpp ${SOURCES_COMMON})
target_link_libraries(poc_clear PRIVATE project_options project_warnings)

add_executable(run_tests test_main.cpp ${SOURCES_COMMON})
target_link_libraries(run_tests PRIVATE project_options project_warnings)

if(HAS_SEAL_TARGET)
    add_executable(poc_he main_he.cpp ${SOURCES_HE})
    target_link_libraries(poc_he PRIVATE project_options project_options_he project_warnings)

    add_executable(run_tests_akavia test_akavia_style.cpp ${SOURCES_HE})
    target_link_libraries(run_tests_akavia PRIVATE project_options project_options_he project_warnings)
endif()

message(STATUS "Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "SEAL_DIR     : ${SEAL_DIR}")
message(STATUS "SEAL_ROOT    : ${SEAL_ROOT}")
if(HAS_SEAL_TARGET)
    message(STATUS "SEAL support : enabled")
    message(STATUS "Targets      : poc_clear, poc_he, run_tests, run_tests_akavia")
else()
    message(WARNING "SEAL introuvable: seuls les targets clear seront generes (poc_clear, run_tests).")
endif()
