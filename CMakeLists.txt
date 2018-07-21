cmake_minimum_required(VERSION 3.2)

set(CMAKE_MODULE_PATH /usr/share/SFML/cmake/Modules)
include(cmake/Catch.cmake)

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
    target_compile_options(project_options --coverage -O0)
  endif()

  option(ENABLE_ASAN "Enable address sanitizer" FALSE)

  if(ENABLE_ASAN)
    target_compile_options(project_options INTERFACE -fsanitize=address)
    target_link_libraries(project_options INTERFACE -fsanitize=address)
  endif()
endif()

if(MSVC)
  target_compile_options(project_warnings INTERFACE /W4)
else()
  target_compile_options(project_warnings
                         INTERFACE
                         -Wall
                         -Wextra # reasonable and standard
                         -Wshadow # warn the user if a variable declaration shadows one from a
                                  # parent context
                         -Wnon-virtual-dtor # warn the user if a class with virtual functions has a
                                            # non-virtual destructor. This helps catch hard to
                                            # track down memory errors
                         -Wold-style-cast # warn for c-style casts
                         -Wcast-align # warn for potential performance problem casts
                         -Wunused # warn on anything being unused
                         -Woverloaded-virtual # warn if you overload (not override) a virtual
                                              # function
                         -Wpedantic # warn if non-standard C++ is used
                         -Wconversion # warn on type conversions that may lose data
                         -Wsign-conversion # warn on sign conversions
                         -Wnull-dereference # warn if a null dereference is detected
                         -Wdouble-promotion # warn if float is implicit promoted to double
                         -Wformat=2 # warn on security issues around functions that format output
                                    # (ie printf)
                         )

  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    target_compile_options(project_warnings
                           INTERFACE
                           -Wmisleading-indentation # warn if identation implies blocks where blocks
                                                    # do not exist
                           -Wduplicated-cond # warn if if / else chain has duplicated conditions
                           -Wduplicated-branches # warn if if / else branches have duplicated code
                           -Wlogical-op # warn about logical operations being used where bitwise were
                                        # probably wanted
                           -Wuseless-cast # warn if you perform a cast to the same type
                           )
  endif()
endif()

#add_executable(arm main.cpp)
#target_link_libraries(arm PRIVATE project_options project_warnings)

enable_testing()

add_executable(constexpr_tests test/constexpr_tests.cpp)
target_link_libraries(constexpr_tests PRIVATE project_options project_warnings)
catch_discover_tests(constexpr_tests)

add_executable(arm_emu src/main.cpp)
target_link_libraries(arm_emu PRIVATE project_options project_warnings)


# imgui dependencies
find_package(SFML COMPONENTS graphics window system)
find_package(OpenGL)


# imgui + sfml built as a lib, intentionally not using full warning flags
add_library(imgui src/imgui/lib/imgui.cpp src/imgui/lib/imgui_draw.cpp src/imgui/lib/imgui-SFML.cpp)
target_link_libraries(imgui INTERFACE sfml-graphics sfml-window sfml-system ${OPENGL_gl_LIBRARY})

# imgui test executable, with full warnings enabled
add_executable(imgui_client src/imgui_client.cpp)
target_link_libraries(imgui_client PRIVATE project_options imgui)
target_include_directories(imgui_client PRIVATE ${SFML_INCLUDE_DIR})


