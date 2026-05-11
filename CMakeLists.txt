cmake_minimum_required(VERSION 3.16)
project(edge_vision_runtime VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

option(EVR_ENABLE_INSTALL "Enable install targets" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EVR_RUNTIME_HEADERS
  include/evr/runtime/config/runtime_config_loader.h
  include/evr/runtime/deployment/phase1_deployment.h
  include/evr/runtime/graph/graph.h
  include/evr/runtime/session/lifecycle_session.h
  include/evr/runtime/session/session_state.h
  include/evr/runtime/source/source_app.h
  include/evr/runtime/source/source_session.h
  include/evr/runtime/supervisor/supervisor_app.h
  include/evr/runtime/supervisor/supervisor_session.h
  include/evr/runtime/worker/worker_app.h
  include/evr/runtime/worker/worker_session.h)

set(EVR_RUNTIME_SOURCES
  src/config/runtime_config_loader.cpp
  src/deployment/phase1_deployment.cpp
  src/graph/graph.cpp
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
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/algorithm/yolov8_person_detection/include>
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

target_link_libraries(evr_runtime PRIVATE evr::yolov8_person_detection)

evr_apply_common_settings(evr_runtime)
set_target_properties(evr_runtime PROPERTIES
  OUTPUT_NAME evr_runtime
  POSITION_INDEPENDENT_CODE ON)

function(evr_add_runtime_binary target source_file)
  add_executable(${target} ${source_file})
  target_link_libraries(${target} PRIVATE evr::runtime evr::yolov8_person_detection)
  evr_apply_common_settings(${target})
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endfunction()

add_subdirectory(algorithm/yolov8_person_detection)

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
  set(EVR_YOLOV8_PERSON_DETECTION_DIR
    "${PROJECT_SOURCE_DIR}/algorithm/yolov8_person_detection")
  set(EVR_YOLOV8_MODEL_PATH
    "${PROJECT_SOURCE_DIR}/tests/fixtures/readable-yolov8s.model")
  set(EVR_YOLOV8_RUNTIME_CONFIG_PATH
    "${EVR_YOLOV8_PERSON_DETECTION_DIR}/configs/yolov8_person_detection.v1.example.yaml")

  add_executable(runtime_sessions_smoke_test tests/runtime_sessions_smoke_test.cpp)
  target_link_libraries(runtime_sessions_smoke_test PRIVATE evr::runtime evr::yolov8_person_detection)
  evr_apply_common_settings(runtime_sessions_smoke_test)
  add_test(NAME runtime_sessions_smoke_test COMMAND runtime_sessions_smoke_test)

  add_executable(runtime_worker_algorithm_loading_smoke_test tests/runtime_worker_algorithm_loading_smoke_test.cpp)
  target_link_libraries(runtime_worker_algorithm_loading_smoke_test PRIVATE evr::yolov8_person_detection)
  evr_apply_common_settings(runtime_worker_algorithm_loading_smoke_test)
  target_compile_definitions(runtime_worker_algorithm_loading_smoke_test
    PRIVATE "EVR_TEST_YOLOV8S_MODEL_PATH=\"${EVR_YOLOV8_MODEL_PATH}\"")
  add_test(NAME runtime_worker_algorithm_loading_smoke_test COMMAND runtime_worker_algorithm_loading_smoke_test)

  add_executable(runtime_video_detect_result_smoke_test tests/runtime_video_detect_result_smoke_test.cpp)
  target_link_libraries(runtime_video_detect_result_smoke_test
    PRIVATE evr::runtime evr::yolov8_person_detection)
  evr_apply_common_settings(runtime_video_detect_result_smoke_test)
  target_compile_definitions(runtime_video_detect_result_smoke_test
    PRIVATE "EVR_TEST_YOLOV8S_MODEL_PATH=\"${EVR_YOLOV8_MODEL_PATH}\"")
  add_test(NAME runtime_video_detect_result_smoke_test COMMAND runtime_video_detect_result_smoke_test)

  add_executable(runtime_file_video_detect_result_smoke_test
    tests/runtime_file_video_detect_result_smoke_test.cpp)
  target_link_libraries(runtime_file_video_detect_result_smoke_test
    PRIVATE evr::yolov8_person_detection)
  evr_apply_common_settings(runtime_file_video_detect_result_smoke_test)
  target_compile_definitions(runtime_file_video_detect_result_smoke_test
    PRIVATE "EVR_TEST_YOLOV8S_MODEL_PATH=\"${EVR_YOLOV8_MODEL_PATH}\"")
  add_test(NAME runtime_file_video_detect_result_smoke_test
    COMMAND runtime_file_video_detect_result_smoke_test)
  set_tests_properties(runtime_file_video_detect_result_smoke_test
    PROPERTIES SKIP_RETURN_CODE 77)

  add_executable(runtime_rtsp_video_detect_result_smoke_test
    tests/runtime_rtsp_video_detect_result_smoke_test.cpp)
  target_link_libraries(runtime_rtsp_video_detect_result_smoke_test
    PRIVATE evr::yolov8_person_detection)
  evr_apply_common_settings(runtime_rtsp_video_detect_result_smoke_test)
  target_compile_definitions(runtime_rtsp_video_detect_result_smoke_test
    PRIVATE "EVR_TEST_YOLOV8S_MODEL_PATH=\"${EVR_YOLOV8_MODEL_PATH}\"")
  add_test(NAME runtime_rtsp_video_detect_result_smoke_test
    COMMAND runtime_rtsp_video_detect_result_smoke_test)
  set_tests_properties(runtime_rtsp_video_detect_result_smoke_test
    PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)

  add_test(NAME runtime_zlm_video_detect_result_smoke_test
    COMMAND runtime_rtsp_video_detect_result_smoke_test EVR_TEST_ZLM_RTSP_URI zlm)
  set_tests_properties(runtime_zlm_video_detect_result_smoke_test
    PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)

  add_test(NAME runtime_source_cli_smoke_test
    COMMAND $<TARGET_FILE:runtime-source> --config
            ${PROJECT_SOURCE_DIR}/configs/runtime-source.v1.example.yaml)
  add_test(NAME runtime_worker_cli_smoke_test
    COMMAND $<TARGET_FILE:runtime-worker> --config
            ${PROJECT_SOURCE_DIR}/configs/runtime-worker.v1.example.yaml
            --engine-path=${EVR_YOLOV8_MODEL_PATH}
            --algorithm-package-uri=file://${EVR_YOLOV8_PERSON_DETECTION_DIR}
            --algorithm-entry-point=evr::algorithm::yolov8_person_detection::YoloV8PersonDetector
            --algorithm-runtime-config-uri=file://${EVR_YOLOV8_RUNTIME_CONFIG_PATH})
  add_test(NAME runtime_supervisor_apply_cli_smoke_test
    COMMAND $<TARGET_FILE:runtime-supervisor> apply --config
            ${PROJECT_SOURCE_DIR}/configs/runtime-phase1-deployment.v1.example.yaml)
  add_test(NAME runtime_supervisor_status_cli_smoke_test
    COMMAND $<TARGET_FILE:runtime-supervisor> status --config
            ${PROJECT_SOURCE_DIR}/configs/runtime-phase1-deployment.v1.example.yaml)
endif()
