# Tests for the Optimizer::extract_patch "island" bug family (see
# IslandCoverage_test.cpp's header comment and CLAUDE.md at the project
# root for the full background).
#
# These tests only need Catch2 and the standard library - IslandCoverageCheck.hpp
# is a self-contained VTK-parsing/geometry utility with no dependency on
# QuadriFlow's own sources (which the top-level CMakeLists.txt builds as a
# single add_executable with no separate library target to link a test
# against), so no QuadriFlow-internal linking is needed here.

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.0
)
FetchContent_MakeAvailable(Catch2)

include(CTest)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(IslandCoverage_test IslandCoverage_test.cpp)
target_link_libraries(IslandCoverage_test PRIVATE Catch2::Catch2WithMain)
target_include_directories(IslandCoverage_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(IslandCoverage_test PRIVATE
    FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

catch_discover_tests(IslandCoverage_test)
