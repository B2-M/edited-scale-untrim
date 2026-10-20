cmake_minimum_required(VERSION 3.1)
project(QuadriFlow LANGUAGES CXX)

option(BUILD_PERFORMANCE_TEST "More subdivisition for performance test" OFF)
option(BUILD_LOG "Enable verbose log" OFF)
option(BUILD_GUROBI "Enable GUROBI for comparison ONLY" OFF)
option(BUILD_OPENMP "Enable support for OpenMP" OFF)
option(BUILD_TBB "Enable support for TBB" OFF)
option(BUILD_FREE_LICENSE "Only use libraries with permissive licenses" OFF)
option(BUILD_ISLAND_TESTS "Build Optimizer::extract_patch island-coverage regression tests (fetches Catch2)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
set(Boost_USE_STATIC_LIBS ON)
find_package(Eigen REQUIRED)
find_package(Boost REQUIRED)

if(APPLE)
    # Detect homebrew opencascade prefix (Apple Silicon: /opt/homebrew, Intel: /usr/local)
    execute_process(COMMAND brew --prefix opencascade OUTPUT_VARIABLE OCC_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(OpenCASCADE_INCLUDE_DIR "${OCC_PREFIX}/include/opencascade")
    file(GLOB OpenCASCADE_LIBRARIES "${OCC_PREFIX}/lib/libTK*.dylib")
    include_directories(${OpenCASCADE_INCLUDE_DIR})
elseif(UNIX)
    # Linux fallback (if find_package fails)
    set(OpenCASCADE_PATH "/usr")  # system prefix
    set(OpenCASCADE_INCLUDE_DIR "${OpenCASCADE_PATH}/include/opencascade")
    file(GLOB OpenCASCADE_LIBRARIES
        "${OpenCASCADE_PATH}/lib/x86_64-linux-gnu/libTK*.so"
        "${OpenCASCADE_PATH}/lib/libTK*.so")

    if(NOT EXISTS "${OpenCASCADE_INCLUDE_DIR}")
        message(FATAL_ERROR "OpenCASCADE headers not found at ${OpenCASCADE_INCLUDE_DIR}")
    endif()

    include_directories(${CMAKE_PROJECT_NAME} ${OpenCASCADE_INCLUDE_DIR})

    # find_package(OpenCASCADE COMPONENTS
    #     FoundationClasses
    #     ModelingData
    #     ModelingAlgorithms
    #     DataExchange
    #     Visualization
    #     REQUIRED)
    # if(OpenCASCADE_FOUND)
    #     message(STATUS "OpenCascade found at ${OpenCASCADE_DIR}")

    #     include_directories(/usr/include/opencascade)
    #     link_directories(/usr/lib/aarch64-linux-gnu)
    # endif()
endif()

if (BUILD_GUROBI)
    find_package(GUROBI REQUIRED)
endif(BUILD_GUROBI)

if (BUILD_TBB)
    find_package(TBB REQUIRED)
endif(BUILD_TBB)

set(LEMON_3RD_PATH 3rd/lemon-1.3.1)

find_path(
    LEMON_SOURCE_ROOT_DIR
    CMakeLists.txt
    PATHS ${CMAKE_SOURCE_DIR}/${LEMON_3RD_PATH} NO_DEFAULT_PATH
    DOC "Location of LEMON source as a CMAKE subproject"
)

if (EXISTS ${LEMON_SOURCE_ROOT_DIR})
    add_subdirectory(${LEMON_SOURCE_ROOT_DIR})
    set(LEMON_INCLUDE_DIRS ${LEMON_SOURCE_ROOT_DIR} ${PROJECT_BINARY_DIR}/${LEMON_3RD_PATH})
    set(LEMON_LIBRARIES lemon)
    unset(LEMON_ROOT_DIR CACHE)
    unset(LEMON_DIR CACHE)
    unset(LEMON_INCLUDE_DIR CACHE)
    unset(LEMON_LIBRARY CACHE)
else()
    find_package(LEMON QUIET NO_MODULE)
    find_package(LEMON REQUIRED)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")  # enable assert
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g")  # enable assert
set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS}")
set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG}")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fopenmp -Wno-int-in-bool-context -Wno-sign-compare")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS}")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address")
endif()

if(MSVC)
    # Enable M_PI and disable fopen() etc. warnings
    ADD_DEFINITIONS(-D_USE_MATH_DEFINES -D_CRT_SECURE_NO_WARNINGS) 
    # Disable the following warnings : C4267 and 4244 (conversions) C4018 (sign compare) and C4800 (bool to int)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /wd4267 /wd4244 /wd4018 /wd4800") 
endif()

include_directories(include)
include_directories(3rd/pcg32)
include_directories(3rd/pss)
include_directories(mergefit/include)
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${EIGEN_INCLUDE_DIRS})
include_directories(${GLM_INCLUDE_DIRS})
include_directories(${GLUT_INCLUDE_DIRS})
include_directories(${LEMON_INCLUDE_DIRS})
include_directories(${TBB_INCLUDE_DIRS})
include_directories(${GUROBI_INCLUDE_DIRS})

if (BUILD_PERFORMANCE_TEST)
    add_definitions(-DPERFORMANCE_TEST)
endif(BUILD_PERFORMANCE_TEST)

if (BUILD_OPENMP)
    add_definitions(-DWITH_OMP)
endif(BUILD_OPENMP)

if (BUILD_LOG)
    add_definitions(-DLOG_OUTPUT)
endif(BUILD_LOG)

if (BUILD_GUROBI)
    add_definitions(-DWITH_GUROBI)
endif(BUILD_GUROBI)

if (BUILD_TBB)
    add_definitions(-DWITH_TBB)
endif(BUILD_TBB)

if (BUILD_FREE_LICENSE)
    add_definitions(-DEIGEN_MPL2_ONLY)
endif(BUILD_FREE_LICENSE)

file(GLOB QuadriFlow_headers "include/*.h" "include/*.hpp"
    "mergefit/include/*.h")

file(GLOB QuadriFlow_sources "source/*.cpp"
    "mergefit/source/*.cpp")

# add_library(${CMAKE_PROJECT_NAME} SHARED ${QuadriFlow_sources})

add_executable(${CMAKE_PROJECT_NAME} ${QuadriFlow_sources})

if(UNIX AND NOT APPLE)
    target_link_libraries(${CMAKE_PROJECT_NAME}
        ${TBB_LIBRARIES}
        ${LEMON_LIBRARIES}
        ${GUROBI_LIBRARIES}
        ${OpenCASCADE_LIBRARIES}
        # OpenCASCADE::FoundationClasses
        # OpenCASCADE::ModelingData
        # OpenCASCADE::ModelingAlgorithms
        # OpenCASCADE::DataExchange
        # OpenCASCADE::Visualization
    )
else()
    target_link_libraries(${CMAKE_PROJECT_NAME}
        ${TBB_LIBRARIES}
        ${LEMON_LIBRARIES}
        ${GUROBI_LIBRARIES}
        ${OpenCASCADE_LIBRARIES}
    )
endif()

if(BUILD_ISLAND_TESTS)
    enable_testing()
    add_subdirectory(test)
endif(BUILD_ISLAND_TESTS)

# Manually set the installation path here
# set(CMAKE_INSTALL_PREFIX
#     "/Users/xiaodongwei/Documents/projects/quadriflow/build/release")

# install(TARGETS ${CMAKE_PROJECT_NAME} DESTINATION lib)
# install(FILES ${QuadriFlow_headers} DESTINATION include)
