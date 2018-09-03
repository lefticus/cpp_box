cmake_minimum_required(VERSION 3.2)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

set(CMAKE_MODULE_PATH /usr/share/SFML/cmake/Modules)
include(${CONAN_LIB_DIRS_CATCH2}/cmake/Catch2/Catch.cmake)

option(BUILD_SHARED_LIBS "Enable compilation of shared libraries" FALSE)

# Link this 'library' to use the standard warnings
add_library(project_warnings INTERFACE)

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)

target_compile_features(project_options INTERFACE cxx_std_17)
target_include_directories(project_options INTERFACE include external)

if(CMAKE_COMPILER_IS_GNUCC)
  option(ENABLE_COVERAGE "Enable coverage reporting for gcc/clang" FALSE)

  if(ENABLE_COVERAGE)
    target_compile_options(project_options INTERFACE --coverage -O0)
    target_link_libraries(project_options INTERFACE --coverage)
  endif()

  option(ENABLE_ASAN "Enable address sanitizer" FALSE)

  if(ENABLE_ASAN)
    target_compile_options(project_options INTERFACE -fsanitize=address)
    target_link_libraries(project_options INTERFACE -fsanitize=address)
  endif()

  target_link_libraries(project_options INTERFACE stdc++fs)
endif()

if(MSVC)
  target_compile_options(project_warnings INTERFACE /W4)
else()
  target_compile_options(project_warnings
                         INTERFACE -Wall
                                   -Wextra # reasonable and standard
                                   -Wshadow # warn the user if a variable declaration shadows one
                                            # from a parent context
                                   -Wnon-virtual-dtor # warn the user if a class with virtual
                                                      # functions has a non-virtual destructor. This
                                                      # helps catch hard to track down memory errors
                                   -Wold-style-cast # warn for c-style casts
                                   -Wcast-align # warn for potential performance problem casts
                                   -Wunused # warn on anything being unused
                                   -Woverloaded-virtual # warn if you overload (not override) a
                                                        # virtual function
                                   -Wpedantic # warn if non-standard C++ is used
                                   -Wconversion # warn on type conversions that may lose data
                                   -Wsign-conversion # warn on sign conversions
                                   -Wnull-dereference # warn if a null dereference is detected
                                   -Wdouble-promotion # warn if float is implicit promoted to double
                                   -Wformat=2 # warn on security issues around functions that format
                                              # output (ie printf)
                         )

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    target_compile_options(project_warnings
                           INTERFACE -Wmisleading-indentation # warn if identation implies blocks
                                                              # where blocks do not exist
                                     -Wduplicated-cond # warn if if / else chain has duplicated
                                                       # conditions
                                     -Wduplicated-branches # warn if if / else branches have
                                                           # duplicated code
                                     -Wlogical-op # warn about logical operations being used where
                                                  # bitwise were probably wanted
                                     -Wuseless-cast # warn if you perform a cast to the same type
                           )
  endif()
endif()

enable_testing()

add_executable(constexpr_tests test/constexpr_tests.cpp)
target_link_libraries(constexpr_tests PRIVATE project_options project_warnings CONAN_PKG::catch2)
#catch_discover_tests(constexpr_tests TEST_PREFIX "constexpr.")

add_executable(relaxed_constexpr_tests test/constexpr_tests.cpp)
target_link_libraries(relaxed_constexpr_tests
                      PRIVATE project_options project_warnings CONAN_PKG::catch2)
target_compile_definitions(relaxed_constexpr_tests PRIVATE RELAXED_CONSTEXPR=1)
catch_discover_tests(relaxed_constexpr_tests TEST_PREFIX "relaxed_constexpr.")

add_executable(arm_emu src/main.cpp)
target_link_libraries(arm_emu PRIVATE project_options project_warnings CONAN_PKG::rang)

add_executable(elf_reader src/elf_reader.cpp)
target_link_libraries(elf_reader PRIVATE project_options project_warnings)

#imgui dependencies

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL)
set(SFML_STATIC_LIBRARIES TRUE)

find_package(SFML 2.5 REQUIRED system graphics window)
find_package(fmt REQUIRED)

# imgui + sfml built as a lib, intentionally not using full warning flags
add_library(imgui
            external/imgui/lib/imgui.cpp
            external/imgui/lib/imgui_draw.cpp
            external/imgui/lib/imgui-SFML.cpp)
target_link_libraries(imgui
                      PUBLIC sfml-system
                             sfml-graphics
                             sfml-window
                             OpenGL::GL)

add_library(utility src/utility.cpp)
target_link_libraries(utility PRIVATE project_options project_warnings PUBLIC CONAN_PKG::spdlog)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# imgui test executable, with full warnings enabled
add_executable(imgui_client src/imgui_client.cpp)
target_link_libraries(imgui_client
                      PRIVATE project_options
                              project_warnings
                              utility
                              imgui
                              Threads::Threads
                              fmt::fmt
                              CONAN_PKG::rang
                              CONAN_PKG::libpng)
target_include_directories(imgui_client SYSTEM PRIVATE external)
