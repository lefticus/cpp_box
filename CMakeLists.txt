cmake_minimum_required(VERSION 3.2)

include(cmake/Catch.cmake)

option(BUILD_SHARED_LIBS "Enable compilation of shared libraries" FALSE)

# Link this 'library' to use the standard warnings
add_library(project_warnings INTERFACE)

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)

target_compile_features(project_options INTERFACE cxx_std_17)
target_include_directories(project_options INTERFACE include)

if(CMAKE_COMPILER_IS_GNUCC)
  option(ENABLE_COVERAGE "Enable coverage reporting for gcc/clang" FALSE)

  if(ENABLE_COVERAGE)
    target_compile_options(--coverage -O0)
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
                         -Wmisleading-indentation # warn if identation implies blocks where blocks
                                                  # do not exist
                         -Wduplicated-cond # warn if if / else chain has duplicated conditions
                         -Wduplicated-branches # warn if if / else branches have duplicated code
                         -Wlogical-op # warn about logical operations being used where bitwise were
                                      # probably wanted
                         -Wnull-dereference # warn if a null dereference is detected
                         -Wuseless-cast # warn if you perform a cast to the same type
                         -Wdouble-promotion # warn if float is implicit promoted to double
                         -Wformat=2 # warn on security issues around functions that format output
                                    # (ie printf)
                         )
endif()

#add_executable(arm main.cpp)
#target_link_libraries(arm PRIVATE project_options project_warnings)

enable_testing()

add_executable(constexpr_tests test/constexpr_tests.cpp)
target_link_libraries(constexpr_tests PRIVATE project_options project_warnings)
catch_discover_tests(constexpr_tests)


# imgui example
# find_package(SFML COMPONENTS graphics window system)
# find_package(OpenGL)

# imgui + sfml built as a lib, intentionally not using full warning flags
# add_library(imgui imgui/lib/imgui.cpp imgui/lib/imgui_draw.cpp imgui/lib/imgui-SFML.cpp)
# target_link_libraries(imgui INTERFACE ${SFML_LIBRARIES} ${OPENGL_gl_LIBRARY})

# imgui test executable, with full warnings enabled
# add_executable(test_imgui imgui/test.cpp)
# target_link_libraries(test_imgui PRIVATE project_warnings imgui)
# target_include_directories(test_imgui PRIVATE ${SFML_INCLUDE_DIR})


