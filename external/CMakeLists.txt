# imgui + sfml built as a lib, intentionally not using full warning flags
add_library(imgui imgui/lib/imgui.cpp imgui/lib/imgui_draw.cpp imgui/lib/imgui-SFML.cpp)
target_link_libraries(imgui PUBLIC CONAN_PKG::sfml OpenGL::GL)
set_property(TARGET imgui PROPERTY CXX_CLANG_TIDY "")
set_property(TARGET imgui PROPERTY CXX_CPPCHECK "")

# Ignoring msvc warning. imgui is really hard to work with warning wise
if (MSVC)
  target_compile_options(imgui PRIVATE /wd4312)
endif()