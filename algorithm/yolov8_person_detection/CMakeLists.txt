# YOLOv8 person detection algorithm package

set(EVR_YOLOV8_PERSON_DET_HEADERS
  include/evr/algorithm/yolov8_person_detection/yolov8_person_detector.h)

set(EVR_YOLOV8_PERSON_DET_SOURCES
  src/yolov8_person_detector.cpp)

add_library(evr_yolov8_person_detection STATIC
  ${EVR_YOLOV8_PERSON_DET_HEADERS}
  ${EVR_YOLOV8_PERSON_DET_SOURCES})
add_library(evr::yolov8_person_detection ALIAS evr_yolov8_person_detection)

evr_apply_common_settings(evr_yolov8_person_detection)
set_target_properties(evr_yolov8_person_detection PROPERTIES
  OUTPUT_NAME evr_yolov8_person_detection
  POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
  add_executable(yolov8_person_detector_smoke_test
    tests/yolov8_person_detector_smoke_test.cpp)
  target_link_libraries(yolov8_person_detector_smoke_test PRIVATE evr::yolov8_person_detection)
  evr_apply_common_settings(yolov8_person_detector_smoke_test)
  target_compile_definitions(yolov8_person_detector_smoke_test
    PRIVATE "EVR_TEST_YOLOV8S_MODEL_PATH=\"${PROJECT_SOURCE_DIR}/tests/fixtures/readable-yolov8s.model\"")
  add_test(NAME yolov8_person_detector_smoke_test COMMAND yolov8_person_detector_smoke_test)
endif()
