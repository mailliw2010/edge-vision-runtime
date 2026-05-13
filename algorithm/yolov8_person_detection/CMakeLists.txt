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

find_path(EVR_TENSORRT_INCLUDE_DIR NvInfer.h
  PATHS /usr/include/aarch64-linux-gnu /usr/include /usr/local/include)
find_path(EVR_CUDA_INCLUDE_DIR cuda_runtime_api.h
  PATHS /usr/local/cuda/include /usr/local/cuda/targets/aarch64-linux/include)
find_library(EVR_TENSORRT_NVINFER_LIBRARY nvinfer)
find_library(EVR_TENSORRT_CUDART_LIBRARY cudart
  PATHS /usr/local/cuda/lib64 /usr/local/cuda/targets/aarch64-linux/lib)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(EVR_LIBJPEG QUIET libjpeg)
endif()
find_path(EVR_LIBJPEG_INCLUDE_DIR jpeglib.h
  HINTS ${EVR_LIBJPEG_INCLUDE_DIRS})
find_library(EVR_LIBJPEG_LIBRARY jpeg
  HINTS ${EVR_LIBJPEG_LIBRARY_DIRS})

if(EVR_LIBJPEG_INCLUDE_DIR AND EVR_LIBJPEG_LIBRARY)
  target_compile_definitions(evr_yolov8_person_detection PUBLIC EVR_WITH_LIBJPEG=1)
  target_include_directories(evr_yolov8_person_detection PUBLIC ${EVR_LIBJPEG_INCLUDE_DIR})
  target_link_libraries(evr_yolov8_person_detection PUBLIC ${EVR_LIBJPEG_LIBRARY})
endif()

if(EVR_TENSORRT_INCLUDE_DIR AND EVR_CUDA_INCLUDE_DIR AND
   EVR_TENSORRT_NVINFER_LIBRARY AND EVR_TENSORRT_CUDART_LIBRARY)
  target_compile_definitions(evr_yolov8_person_detection PRIVATE EVR_WITH_TENSORRT=1)
  target_include_directories(evr_yolov8_person_detection
    PRIVATE ${EVR_TENSORRT_INCLUDE_DIR} ${EVR_CUDA_INCLUDE_DIR})
  target_link_libraries(evr_yolov8_person_detection
    PRIVATE ${EVR_TENSORRT_NVINFER_LIBRARY} ${EVR_TENSORRT_CUDART_LIBRARY})
endif()

if(BUILD_TESTING)
  add_executable(yolov8_person_detector_smoke_test
    tests/yolov8_person_detector_smoke_test.cpp)
  target_link_libraries(yolov8_person_detector_smoke_test PRIVATE evr::yolov8_person_detection)
  evr_apply_common_settings(yolov8_person_detector_smoke_test)
  target_compile_definitions(yolov8_person_detector_smoke_test
    PRIVATE "EVR_TEST_YOLOV8S_MODEL_PATH=\"${PROJECT_SOURCE_DIR}/tests/fixtures/readable-yolov8s.model\"")
  add_test(NAME yolov8_person_detector_smoke_test COMMAND yolov8_person_detector_smoke_test)
endif()
