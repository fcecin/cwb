cmake_minimum_required(VERSION 3.28)
project(cwb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Inherit the ces Qt6 discovery: globs ~/Qt/<ver>/<platform>, env vars, qmake on
# PATH. Populates CMAKE_PREFIX_PATH before find_package.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/QtDiscovery.cmake)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg Test Multimedia MultimediaWidgets)
qt_standard_project_setup()

# litehtml: portable HTML/CSS layout engine, no JS. Its document_container is
# where the QPainter surface and (later) the CesPlex resource loader attach.
include(FetchContent)
FetchContent_Declare(litehtml
  GIT_REPOSITORY https://github.com/litehtml/litehtml.git
  GIT_TAG        master
  GIT_SHALLOW    ON)
# Do not lint/analyze the third-party dependency (its clang-tidy runs with
# -warnings-as-errors and needs a clang-configured libstdc++).
set(LITEHTML_ENABLE_LINT OFF CACHE BOOL "" FORCE)
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(litehtml)

# Pinned CES: fetch the client engine (ceslib) from the ces remote at a fixed
# commit -- never the local working tree. MakeAvailable also exposes the ces
# server and cesluajitd as build targets, so the local demo runs on the same pin.
FetchContent_Declare(ces
  GIT_REPOSITORY https://github.com/fcecin/ces
  GIT_TAG        aaaa1a4)
set(CES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ces)

# md4c: a small, fast, MIT-licensed Markdown parser (GFM tables/tasklists). Its
# md4c-html target renders Markdown straight to an HTML fragment that litehtml
# then styles + lays out. Forced static so the app needs no extra runtime .so.
FetchContent_Declare(md4c
  GIT_REPOSITORY https://github.com/mity/md4c
  GIT_TAG        release-0.5.2
  GIT_SHALLOW    ON)
set(BUILD_SHARED_LIBS OFF)
set(MD4C_USE_UTF8 ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(md4c)

# cwb_core: the Widgets-free, unit-testable logic (URL parsing, file-kind rules).
# Linked by both the app and the tests, so a rule is compiled and asserted once.
add_library(cwb_core STATIC
  src/cesurl.h
  src/cesurl.cpp
  src/filekind.h
  src/filekind.cpp
  src/cache.h
  src/cache.cpp
  src/minermath.h
  src/minermath.cpp
  src/url_history.h
  src/url_history.cpp
  src/settings.h
  src/settings.cpp
  src/codehighlight.h
  src/codehighlight.cpp
  src/csvtable.h
  src/csvtable.cpp
  src/names.h
  src/names.cpp
  src/worktree.h
  src/worktree.cpp
  src/credits.h
)
target_link_libraries(cwb_core PUBLIC Qt6::Core)
target_include_directories(cwb_core PUBLIC src)

# Native embedded widgets: each .cpp dropped in src/widgets/ is compiled in and
# self-registers (src/widget.h). CONFIGURE_DEPENDS re-globs on build, so adding a
# capability is just adding a file. Exclude one by removing/renaming it.
file(GLOB CWB_WIDGET_SRC CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets/*.cpp)

qt_add_executable(cwb
  src/main.cpp
  src/widget.h
  src/widget.cpp
  ${CWB_WIDGET_SRC}
  src/mainwindow.h
  src/mainwindow.cpp
  src/miningwindow.h
  src/miningwindow.cpp
  src/miner.h
  src/miner.cpp
  src/hashrategraph.h
  src/hashrategraph.cpp
  src/renderpane.h
  src/renderpane.cpp
  src/imagepane.h
  src/imagepane.cpp
  src/mediapane.h
  src/mediapane.cpp
  src/cesdial.h
  src/cesdial.cpp
  src/cesidentity.h
  src/cesidentity.cpp
  src/identityreg.h
  src/identityreg.cpp
  src/terminalpane.h
  src/terminalpane.cpp
  src/recorder.h
  src/recorder.cpp
)

# App icon (Araucaria mark) embedded as a Qt resource at :/icons/cwb.svg.
qt_add_resources(cwb "appicons"
  PREFIX "/icons"
  BASE "resources"
  FILES resources/cwb.svg)

target_link_libraries(cwb PRIVATE cwb_core Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Multimedia Qt6::MultimediaWidgets litehtml ceslib CLI11::CLI11 md4c-html)
target_include_directories(cwb PRIVATE ${md4c_SOURCE_DIR}/src)  # md4c-html.h

# Executable / bundle icon per platform (the in-app window icon is set from the
# SVG at runtime and needs none of this). Linux uses the runtime window icon
# plus a .desktop file supplied by packaging.
if(WIN32)
  target_sources(cwb PRIVATE resources/cwb.rc)
elseif(APPLE)
  set_source_files_properties(resources/cwb.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(cwb PRIVATE resources/cwb.icns)
  set_target_properties(cwb PROPERTIES MACOSX_BUNDLE_ICON_FILE cwb.icns)
endif()

# Portable-shell packaging hints. Real per-OS installers come via CPack later.
set_target_properties(cwb PROPERTIES
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
