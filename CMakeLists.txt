cmake_minimum_required(VERSION 3.10)
project(TheCppBox VERSION 0.0.1 LANGUAGES CXX)

find_program(CCACHE ccache)
if(CCACHE)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
endif()

#
# Set compiler flags before running conan
#

# Link this 'library' to use the standard warnings
add_library(project_warnings INTERFACE)

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)

target_compile_features(project_options INTERFACE cxx_std_17)
target_include_directories(project_options INTERFACE include external)

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
# Load Conan
#

if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
  message(
    STATUS
      "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
  file(
    DOWNLOAD
      "https://raw.githubusercontent.com/conan-io/cmake-conan/v0.12/conan.cmake"
      "${CMAKE_BINARY_DIR}/conan.cmake")
endif()

include(${CMAKE_BINARY_DIR}/conan.cmake)
conan_check(REQUIRED)
conan_cmake_run(CONANFILE
                conanfile.txt
                BASIC_SETUP
                CONAN_COMMAND
                ${CONAN_CMD}
                CMAKE_TARGETS
                BUILD
                missing)

set(CMAKE_MODULE_PATH /usr/share/SFML/cmake/Modules ${CMAKE_BINARY_DIR})

if(CONAN_CATCH2_ROOT_DEBUG)
  include(${CONAN_CATCH2_ROOT_DEBUG}/lib/cmake/Catch2/Catch.cmake)
else()
  include(${CONAN_CATCH2_ROOT}/lib/cmake/Catch2/Catch.cmake)
endif()

find_package(fmt)
find_package(rang)
find_package(libpng)
find_package(clara)
find_package(catch2)
find_package(spdlog)

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
                      PRIVATE project_options project_warnings catch2::catch2)
catch_discover_tests(constexpr_tests TEST_PREFIX "constexpr.")

add_executable(relaxed_constexpr_tests test/constexpr_tests.cpp)
target_link_libraries(relaxed_constexpr_tests
                      PRIVATE project_options project_warnings catch2::catch2)
target_compile_definitions(relaxed_constexpr_tests PRIVATE RELAXED_CONSTEXPR=1)
catch_discover_tests(relaxed_constexpr_tests TEST_PREFIX "relaxed_constexpr.")

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

  # imgui dependencies

  set(OpenGL_GL_PREFERENCE GLVND)
  find_package(OpenGL)

  add_subdirectory(external)

  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)

  # imgui test executable, with full warnings enabled
  add_executable(cpp_box src/cpp_box.cpp)
  target_link_libraries(cpp_box
                        PRIVATE project_options
                                project_warnings
                                utility
                                compiler
                                imgui
                                Threads::Threads
                                fmt::fmt
                                libpng::libpng
                                clara::clara)
  target_include_directories(cpp_box SYSTEM PRIVATE external)
endif()
