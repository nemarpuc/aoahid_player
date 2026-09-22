cmake_minimum_required(VERSION 3.21)
project(aoahid-player VERSION 0.16.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/EHsc /utf-8 /wd4244)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(AOAHID_PLAYER_BUILD_GUI "Build the Dear ImGui front end (aoahid_player_gui)" ON)
option(AOAHID_PLAYER_BUNDLE_RUNTIME
  "Install libaoahid, libusb and, with MSVC, the C++ runtime next to the executables" ON)

# 0.3.0 removed aoahid_gamepad_options.application (see spec_builder.hpp's
# build_gamepad()); an older libaoahid still compiles against this source but
# would reject every Gamepad Spec at runtime, so the minimum is pinned here to
# fail at configure time instead. 0.5.0 renamed aoahid_touch_options to
# aoahid_touchscreen_options (build_touch() follows), which an older libaoahid
# would fail to even compile against, so the pin moves with it. 0.5.3 fixed a
# runtime AOAHID_ERR_PARAM ("HID 1.11 section 8.4 forbids one field from
# spanning more than four bytes") that an older libaoahid raises for
# reachable Gamepad settings here: the GUI's D-pad default is Hat and its
# axis-bits slider (build_gamepad(), 2-32 bits) allows 29-32, a combination
# an older libaoahid always rejected regardless of any other setting.
find_package(aoahid 0.5.3 CONFIG REQUIRED)
find_package(Threads REQUIRED)

# Every executable lands in one folder next to csv/, in the build tree
# (out/<config>) and in the install tree (its root), matching the release
# archive. Recordings are saved to that csv/ folder.
set(AOAHID_PLAYER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/out/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${AOAHID_PLAYER_OUTPUT_DIR}")

include(cmake/AoahidPlayerApp.cmake)

add_subdirectory(src)
add_subdirectory(apps/aoa_touch)
add_subdirectory(apps/aoa_record)
if(AOAHID_PLAYER_BUILD_GUI)
  add_subdirectory(apps/aoahid_player_gui)
endif()

# Unit tests (tests/) are only built when this is the top-level project.
include(CTest)
if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(tests)
endif()

# --- runtime component: what the release archive contains ------------------
install(DIRECTORY csv/ DESTINATION csv COMPONENT runtime)
install(FILES README.md LICENSE DESTINATION . COMPONENT runtime)
aoahid_player_install_runtime()

# --- development component: aoahid_player_core as a CMake package ----------
install(DIRECTORY include/ DESTINATION include COMPONENT development)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/aoahid_player_coreConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/aoahid_player_coreConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/aoahid_player_coreConfig.cmake"
  INSTALL_DESTINATION lib/cmake/aoahid_player_core
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/aoahid_player_coreConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/aoahid_player_coreConfigVersion.cmake"
  DESTINATION lib/cmake/aoahid_player_core
  COMPONENT development
)
install(EXPORT aoahid_player_coreTargets
  FILE aoahid_player_coreTargets.cmake
  NAMESPACE aoahid_player::
  DESTINATION lib/cmake/aoahid_player_core
  COMPONENT development
)
