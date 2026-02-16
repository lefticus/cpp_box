cmake_minimum_required(VERSION 3.10)
project(TheCppBox VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_program(CCACHE ccache)
if(CCACHE)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
endif()

#
# Set compiler flags
#

# Link this 'library' to use the standard warnings
add_library(project_warnings INTERFACE)

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)

target_compile_features(project_options INTERFACE cxx_std_17)
target_include_directories(project_options INTERFACE include)

if(MSVC)
  target_compile_options(project_warnings INTERFACE /W4 /WX "/permissive-")
else()
  option(ONLY_COVERAGE "Build only tests necessary for coverage" FALSE)
  option(LIBCPP "Build with libc++" FALSE)
  option(ENABLE_COVERAGE "Enable coverage reporting for gcc/clang" FALSE)
  option(ENABLE_FUZZERS "Enable fuzz testing tools" FALSE)

  if(ONLY_COVERAGE OR ENABLE_COVERAGE)
    target_compile_options(project_options INTERFACE --coverage -O0 -g)
    target_link_libraries(project_options INTERFACE --coverage)
  endif()

  option(ENABLE_ASAN "Enable address sanitizer" FALSE)

  if(ENABLE_ASAN)
    target_compile_options(project_options INTERFACE -fsanitize=address)
    target_link_libraries(project_options INTERFACE -fsanitize=address)
  endif()

  target_compile_options(project_warnings
                         INTERFACE -Wall
                                   -Werror # treat all warnings as errors
                                   -Wextra # reasonable and standard
                                   -Wshadow # warn the user if a variable
                                            # declaration shadows one from a
                                            # parent context
                                   -Wnon-virtual-dtor # warn the user if a class
                                                      # with virtual functions
                                                      # has a non-virtual
                                                      # destructor. This helps
                                                      # catch hard to track down
                                                      # memory errors
                                   -Wold-style-cast # warn for c-style casts
                                   -Wcast-align # warn for potential performance
                                                # problem casts
                                   -Wunused # warn on anything being unused
                                   -Woverloaded-virtual # warn if you overload
                                                        # (not override) a
                                                        # virtual function
                                   -Wpedantic # warn if non-standard C++ is used
                                   -Wconversion # warn on type conversions that
                                                # may lose data
                                   -Wsign-conversion # warn on sign conversions
                                   -Wnull-dereference # warn if a null
                                                      # dereference is detected
                                   -Wdouble-promotion # warn if float is
                                                      # implicit promoted to
                                                      # double
                                   -Wformat=2 # warn on security issues around
                                              # functions that format output (ie
                                              # printf)
                         )

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    target_compile_options(project_warnings
                           INTERFACE -Wmisleading-indentation # warn if
                                                              # identation
                                                              # implies blocks
                                                              # where blocks do
                                                              # not exist
                                     -Wduplicated-cond # warn if if / else chain
                                                       # has duplicated
                                                       # conditions
                                     -Wduplicated-branches # warn if if / else
                                                           # branches have
                                                           # duplicated code
                                     -Wlogical-op # warn about logical
                                                  # operations being used where
                                                  # bitwise were probably wanted
                                     -Wuseless-cast # warn if you perform a cast
                                                    # to the same type
                           )
    target_link_libraries(project_options INTERFACE stdc++fs)
  else()
    if(LIBCPP)
      target_compile_options(project_options INTERFACE -stdlib=libc++)
    else()
      target_link_libraries(project_options INTERFACE stdc++fs)
    endif()

  endif()
endif()

#
# CPM - CMake Package Manager
#

set(CPM_DOWNLOAD_VERSION 0.40.2)
file(DOWNLOAD
  "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
  "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
include(${CMAKE_BINARY_DIR}/cmake/CPM.cmake)

CPMAddPackage("gh:fmtlib/fmt#10.2.1")

CPMAddPackage(
  NAME spdlog
  VERSION 1.14.1
  GITHUB_REPOSITORY gabime/spdlog
  OPTIONS "SPDLOG_FMT_EXTERNAL ON"
)

CPMAddPackage(
  NAME rang
  GITHUB_REPOSITORY agauniyal/rang
  GIT_TAG v3.1.0
  DOWNLOAD_ONLY YES
)
if(rang_ADDED)
  add_library(rang::rang INTERFACE IMPORTED)
  target_include_directories(rang::rang INTERFACE ${rang_SOURCE_DIR}/include)
endif()
CPMAddPackage(
  NAME Clara
  GITHUB_REPOSITORY catchorg/Clara
  GIT_TAG v1.1.5
  DOWNLOAD_ONLY YES
)
if(Clara_ADDED)
  add_library(clara::clara INTERFACE IMPORTED)
  target_include_directories(clara::clara INTERFACE ${Clara_SOURCE_DIR}/include)
endif()

CPMAddPackage(
  NAME Catch2
  VERSION 3.7.1
  GITHUB_REPOSITORY catchorg/Catch2
)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)

CPMAddPackage(
  NAME SFML
  VERSION 2.6.2
  GITHUB_REPOSITORY SFML/SFML
  GIT_TAG 2.6.2
  OPTIONS
    "SFML_BUILD_AUDIO OFF"
    "SFML_BUILD_NETWORK OFF"
)

CPMAddPackage(
  NAME imgui
  GITHUB_REPOSITORY ocornut/imgui
  GIT_TAG v1.91.5
  DOWNLOAD_ONLY YES
)

set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SFML_FIND_SFML OFF)
CPMAddPackage(
  NAME imgui-sfml
  GITHUB_REPOSITORY SFML/imgui-sfml
  GIT_TAG v2.6.1
)
# Mark imgui/imgui-sfml headers as SYSTEM to suppress warnings from -Werror
get_target_property(_imgui_sfml_includes ImGui-SFML INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(ImGui-SFML PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_imgui_sfml_includes}")

find_package(PNG REQUIRED)

#
# Options
#

option(BUILD_SHARED_LIBS "Enable compilation of shared libraries" FALSE)
option(ENABLE_CLANG_TIDY "Enable testing with clang-tidy" FALSE)
option(ENABLE_CPPCHECK "Enable testing with cppcheck" FALSE)

if(ENABLE_CPPCHECK)
  find_program(CPPCHECK cppcheck)
  if(CPPCHECK)
    set(CMAKE_CXX_CPPCHECK
        ${CPPCHECK}
        --suppress=syntaxError
        --enable=all
        --inconclusive)
  else()
    message(SEND_ERROR "cppcheck requested but executable not found")
  endif()
endif()

if(ENABLE_CLANG_TIDY)
  find_program(CLANGTIDY clang-tidy)
  if(CLANGTIDY)
    set(CMAKE_CXX_CLANG_TIDY ${CLANGTIDY})
  else()
    message(SEND_ERROR "clang-tidy requested but executable not found")
  endif()
endif()

#
# Project Setup
#

enable_testing()

add_executable(constexpr_tests test/constexpr_tests.cpp)
target_link_libraries(constexpr_tests
                      PRIVATE project_options project_warnings Catch2::Catch2WithMain)
catch_discover_tests(constexpr_tests TEST_PREFIX "constexpr." EXTRA_ARGS -s --reporter=xml --out=constexpr.xml)

add_executable(relaxed_constexpr_tests test/constexpr_tests.cpp)
target_link_libraries(relaxed_constexpr_tests
                      PRIVATE project_options project_warnings Catch2::Catch2WithMain)
target_compile_definitions(relaxed_constexpr_tests PRIVATE RELAXED_CONSTEXPR=1)
catch_discover_tests(relaxed_constexpr_tests TEST_PREFIX "relaxed_constexpr."  EXTRA_ARGS -s --reporter=xml --out=relaxed_constexpr.xml)

if(NOT ONLY_COVERAGE)
  add_library(utility lib/utility.cpp)
  target_link_libraries(utility
                        PRIVATE project_options project_warnings fmt::fmt
                        PUBLIC spdlog::spdlog rang::rang)

  add_library(compiler lib/compiler.cpp)
  target_link_libraries(compiler
                        PUBLIC spdlog::spdlog utility
                        PRIVATE project_options project_warnings fmt::fmt)

  add_executable(arm_emu src/arm_emu.cpp)
  target_link_libraries(arm_emu
                        PRIVATE project_options
                                project_warnings
                                rang::rang
                                compiler
                                utility)

  add_executable(obj_compiler src/obj_compiler.cpp)
  target_link_libraries(obj_compiler
                        PRIVATE project_options
                                project_warnings
                                rang::rang
                                clara::clara
                                compiler
                                utility)

  add_executable(elf_reader src/elf_reader.cpp)
  target_link_libraries(elf_reader
                        PRIVATE project_options project_warnings compiler)

  if(ENABLE_FUZZERS)
    add_executable(elf_reader_fuzzer test/elf_reader_fuzzer.cpp)
    target_link_libraries(elf_reader_fuzzer
                          PRIVATE project_options
                                  project_warnings
                                  compiler
                                  -fsanitize=address,fuzzer)
    target_compile_options(elf_reader_fuzzer
                           INTERFACE -fsanitize=address,fuzzer)
  endif()

  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)

  # imgui test executable, with full warnings enabled
  add_executable(cpp_box src/cpp_box.cpp)
  target_link_libraries(cpp_box
                        PRIVATE project_options
                                project_warnings
                                utility
                                compiler
                                ImGui-SFML::ImGui-SFML
                                Threads::Threads
                                fmt::fmt
                                PNG::PNG
                                clara::clara)
endif()
