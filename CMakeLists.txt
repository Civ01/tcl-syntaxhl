cmake_minimum_required(VERSION 3.20)
project(tcl_syntaxhl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT for MSVC: no VC++ redistributable needed on the user's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WIN32)
  set(_plugin_default ON)
else()
  set(_plugin_default OFF)
endif()
option(TCL_BUILD_PLUGIN "Build the Total Commander plugin (.wlx64, Windows only)" ${_plugin_default})
option(TCL_BUILD_TESTS "Build unit tests" ON)

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------- core (portable)
add_library(shl_core STATIC
  src/core/Token.cpp
  src/core/TextUtil.cpp
  src/core/TextDecoder.cpp
  src/core/IniDocument.cpp
  src/core/Config.cpp
  src/core/Theme.cpp
  src/core/LazyModel.cpp
  src/core/LexerFactory.cpp
  src/lexers/JsonLexer.cpp
  src/lexers/CssLexer.cpp
  src/lexers/PythonLexer.cpp
  src/lexers/JsLexer.cpp
  src/lexers/IniLexer.cpp
  src/regex/RegexLexer.cpp
)
target_include_directories(shl_core PUBLIC src)

# ---------------------------------------------------------------- plugin (Windows)
if(TCL_BUILD_PLUGIN)
  if(NOT WIN32)
    message(FATAL_ERROR "TCL_BUILD_PLUGIN requires a Windows target (MSVC or MinGW cross toolchain)")
  endif()
  add_library(tcl-syntaxhl SHARED
    src/plugin/exports.cpp
    src/plugin/WinUtil.cpp
    src/view/ListerWindow.cpp
    src/view/InfoBar.cpp
    src/view/Highlighter.cpp
    src/ui/GotoLineDialog.cpp
    src/ui/ConfigDialog.cpp
    src/ui/resources.rc
    src/plugin/tcl-syntaxhl.def
  )
  target_link_libraries(tcl-syntaxhl PRIVATE shl_core user32 gdi32 comctl32 uxtheme ole32)
  target_compile_definitions(tcl-syntaxhl PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  set_target_properties(tcl-syntaxhl PROPERTIES PREFIX "" SUFFIX ".wlx64")
  if(MINGW)
    target_link_options(tcl-syntaxhl PRIVATE -static -static-libgcc -static-libstdc++)
  endif()

  # Release layout: build/package/tcl-syntaxhl/* and build/tcl-syntaxhl-<ver>.zip
  set(_pkg_dir ${CMAKE_BINARY_DIR}/package/tcl-syntaxhl)
  set(_zip ${CMAKE_BINARY_DIR}/tcl-syntaxhl-${PROJECT_VERSION}-x64.zip)
  add_custom_target(package_zip
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/package
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_pkg_dir}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tcl-syntaxhl> ${_pkg_dir}/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/dist ${_pkg_dir}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/README.md ${_pkg_dir}/
    COMMAND ${CMAKE_COMMAND} -E chdir ${_pkg_dir} ${CMAKE_COMMAND} -E tar cf ${_zip} --format=zip .
    DEPENDS tcl-syntaxhl
    COMMENT "Packaging ${_zip}")
endif()

# ---------------------------------------------------------------- tests
if(TCL_BUILD_TESTS)
  enable_testing()
  add_executable(shl_tests
    tests/test_main.cpp
    tests/test_core.cpp
    tests/test_json.cpp
    tests/test_css.cpp
    tests/test_python.cpp
    tests/test_js.cpp
    tests/test_ini.cpp
    tests/test_regex.cpp
    tests/test_lazy.cpp
  )
  target_include_directories(shl_tests PRIVATE tests tests/third_party)
  target_link_libraries(shl_tests PRIVATE shl_core)
  target_compile_definitions(shl_tests PRIVATE SHL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(MINGW)
    target_link_options(shl_tests PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME shl_tests COMMAND shl_tests)
endif()
