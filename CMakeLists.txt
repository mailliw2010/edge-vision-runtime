cmake_minimum_required(VERSION 3.16)
project(edge_vision_runtime VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

option(EVR_ENABLE_INSTALL "Enable install targets" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EVR_RUNTIME_HEADERS
  include/evr/runtime/session/lifecycle_session.h
  include/evr/runtime/session/session_state.h
  include/evr/runtime/source/source_app.h
  include/evr/runtime/source/source_session.h
  include/evr/runtime/supervisor/supervisor_app.h
  include/evr/runtime/supervisor/supervisor_session.h
  include/evr/runtime/worker/worker_app.h
  include/evr/runtime/worker/worker_session.h)

set(EVR_RUNTIME_SOURCES
  src/session/session_state.cpp
  src/source/source_app.cpp
  src/source/source_session.cpp
  src/supervisor/supervisor_app.cpp
  src/supervisor/supervisor_session.cpp
  src/worker/worker_app.cpp
  src/worker/worker_session.cpp)

function(evr_apply_common_settings target)
  target_compile_features(${target} PUBLIC cxx_std_17)
  target_include_directories(${target}
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

add_library(evr_runtime STATIC
  ${EVR_RUNTIME_HEADERS}
  ${EVR_RUNTIME_SOURCES})
add_library(evr::runtime ALIAS evr_runtime)

evr_apply_common_settings(evr_runtime)
set_target_properties(evr_runtime PROPERTIES
  OUTPUT_NAME evr_runtime
  POSITION_INDEPENDENT_CODE ON)

function(evr_add_runtime_binary target source_file)
  add_executable(${target} ${source_file})
  target_link_libraries(${target} PRIVATE evr::runtime)
  evr_apply_common_settings(${target})
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endfunction()

evr_add_runtime_binary(runtime-supervisor cmd/runtime-supervisor/main.cpp)
evr_add_runtime_binary(runtime-source cmd/runtime-source/main.cpp)
evr_add_runtime_binary(runtime-worker cmd/runtime-worker/main.cpp)

if(EVR_ENABLE_INSTALL)
  install(TARGETS evr_runtime runtime-supervisor runtime-source runtime-worker
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/configs/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/configs)
endif()

if(BUILD_TESTING)
  add_executable(runtime_sessions_smoke_test tests/runtime_sessions_smoke_test.cpp)
  target_link_libraries(runtime_sessions_smoke_test PRIVATE evr::runtime)
  evr_apply_common_settings(runtime_sessions_smoke_test)
  add_test(NAME runtime_sessions_smoke_test COMMAND runtime_sessions_smoke_test)
endif()
