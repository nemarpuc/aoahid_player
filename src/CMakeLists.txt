# Core library shared by aoa_touch, aoa_record and aoahid_player_gui: device
# and session management over libaoahid, CSV scripts, the playback engine,
# high-precision timing, adb-based recording, and the ADB Bridge. It never
# touches stdin or stdout; status reaches the front ends through EventSink.
add_library(aoahid_player_core STATIC
  adb.cpp
  context.cpp
  device.cpp
  device_group.cpp
  event_script.cpp
  getevent_parser.cpp
  input_state.cpp
  key_names.cpp
  paths.cpp
  player.cpp
  process.cpp
  recorder.cpp
  session.cpp
  timing.cpp
  # aoahid_adb_proxy v3.2.1 (MIT), vendored unmodified from
  # github.com/nemarpuc/aoahid_adb_proxy at ea22c00. It needs libaoahid 4.0.
  "${PROJECT_SOURCE_DIR}/third_party/aoahid_adb_proxy/aoahid_adb_proxy.cpp"
)
add_library(aoahid_player::aoahid_player_core ALIAS aoahid_player_core)

target_include_directories(aoahid_player_core PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_include_directories(aoahid_player_core PRIVATE
  "${PROJECT_SOURCE_DIR}/third_party/aoahid_adb_proxy")

target_link_libraries(aoahid_player_core PUBLIC aoahid::aoahid Threads::Threads)
if(WIN32)
  target_link_libraries(aoahid_player_core PRIVATE ws2_32)
endif()
target_compile_features(aoahid_player_core PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(aoahid_player_core PRIVATE /W4)
else()
  target_compile_options(aoahid_player_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

install(TARGETS aoahid_player_core
  EXPORT aoahid_player_coreTargets
  ARCHIVE DESTINATION lib COMPONENT development
  LIBRARY DESTINATION lib COMPONENT development
)
