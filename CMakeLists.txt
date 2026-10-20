cmake_minimum_required(VERSION 3.16)
project(miwear_band C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(ADW REQUIRED IMPORTED_TARGET libadwaita-1)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0)
pkg_check_modules(PROTOBUF_C REQUIRED IMPORTED_TARGET libprotobuf-c)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(JSON REQUIRED IMPORTED_TARGET json-glib-1.0)
find_library(BLUETOOTH_LIB bluetooth REQUIRED)

set(MIWEAR_SLEEP_MODEL_DEFAULT "${CMAKE_SOURCE_DIR}/third_party/mi-fitness-sleep/mi_stage_model_0_1_1.tflite")
option(MIWEAR_ENABLE_SLEEP_ML "In-process TensorFlow Lite sleep stage inference (Linux native)" ON)
set(TENSORFLOW_SOURCE_DIR "" CACHE PATH "TensorFlow source tree (contains tensorflow/lite/CMakeLists.txt)")
set(MIWEAR_TFLITE_BUILD_DIR "" CACHE PATH "Existing TensorFlow Lite CMake build dir (optional; else add_subdirectory)")

set(MIWEAR_SCHEMA_EXTRACT_DIR "${CMAKE_BINARY_DIR}/mi-fitness-schemas")
set(MIWEAR_SCHEMA_STAMP "${MIWEAR_SCHEMA_EXTRACT_DIR}/.extracted")
add_custom_command(
    OUTPUT "${MIWEAR_SCHEMA_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MIWEAR_SCHEMA_EXTRACT_DIR}"
    COMMAND unzip -qo "${CMAKE_SOURCE_DIR}/third_party/mi-fitness-schemas/schemas.zip"
            -d "${MIWEAR_SCHEMA_EXTRACT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/third_party/mi-fitness-schemas/index.json"
            "${MIWEAR_SCHEMA_EXTRACT_DIR}/index.json"
    COMMAND ${CMAKE_COMMAND} -E touch "${MIWEAR_SCHEMA_STAMP}"
    DEPENDS "${CMAKE_SOURCE_DIR}/third_party/mi-fitness-schemas/schemas.zip"
            "${CMAKE_SOURCE_DIR}/third_party/mi-fitness-schemas/index.json"
    COMMENT "Extract Mi Fitness schemas.zip for schema reader"
)
add_custom_target(miwear_schemas DEPENDS "${MIWEAR_SCHEMA_STAMP}")

add_executable(miwear-band
    src/main.c
    src/core/config.c
    src/core/profile.c
    src/core/find_phone.c
    src/core/weight_log.c
    src/spp/crypto.c
    src/spp/bluez.c
    src/spp/wire.c
    src/spp/fitness_sync.c
    src/spp/weather.c
    src/notifications/notifications.c
    src/activity/activity.c
    src/activity/manual.c
    src/activity/workout_summary.c
    src/activity/workout_types.c
    src/activity/schema_reader.c
    src/activity/mi_fitness_data.c
    src/activity/mi_fitness_validity.c
    src/activity/mi_fitness_oneden.c
    src/activity/mi_fitness_dispatch.c
    src/activity/mi_fitness_full_day_sleep.c
    src/activity/mi_fitness_health.c
    src/activity/mi_fitness_sleep_stages.c
    src/sleep/mi_sleep_duration.c
    src/sleep/mi_sleep_packets.c
    src/sleep/mi_sleep_pipeline.c
    src/sleep/mi_sleep_external.c
    src/sleep/mi_sleep_inference.cpp
    src/activity/mi_fitness_sport_tables.c
    src/activity/mi_fitness_sport_report.c
    src/activity/outdoor_step_report_fields.c
    src/activity/outdoor_sport_report_fields.c
    src/activity/abnormal_record_fields.c
    src/activity/weight_record_fields.c
    src/activity/sport_gps_fields.c
    src/activity/ecg_measure_fields.c
    src/activity/temperature_measure_fields.c
    src/ui/ui.c
    src/ui/dashboard.c
    src/ui/health_data.c
    src/db/db.c
    src/proto/xiaomi.pb-c.c
)

target_include_directories(miwear-band PRIVATE
    include
    src
    src/proto
)

target_link_libraries(miwear-band PRIVATE
    PkgConfig::GTK
    PkgConfig::ADW
    PkgConfig::GLIB
    PkgConfig::JSON
    PkgConfig::PROTOBUF_C
    PkgConfig::SQLITE3
    ${BLUETOOTH_LIB}
    OpenSSL::Crypto
    m
)

target_compile_definitions(miwear-band PRIVATE
    MIWEAR_SCHEMA_DIR="${MIWEAR_SCHEMA_EXTRACT_DIR}"
    MIWEAR_SLEEP_MODEL_DEFAULT="${MIWEAR_SLEEP_MODEL_DEFAULT}"
    MIWEAR_ICONS_DIR="${CMAKE_SOURCE_DIR}/src/resources/icons"
)

if(MIWEAR_ENABLE_SLEEP_ML)
    if(TENSORFLOW_SOURCE_DIR STREQUAL "")
        set(TENSORFLOW_SOURCE_DIR "/tmp/tflite-build/tensorflow" CACHE PATH "TensorFlow source tree" FORCE)
    endif()
    set(_miwear_tflite_linked FALSE)
    if(MIWEAR_TFLITE_BUILD_DIR AND EXISTS "${MIWEAR_TFLITE_BUILD_DIR}/libtensorflow-lite.a")
        file(GLOB_RECURSE _miwear_tflite_archives CONFIGURE_DEPENDS
            "${MIWEAR_TFLITE_BUILD_DIR}/*.a"
            "${MIWEAR_TFLITE_BUILD_DIR}/_deps/*/*.a"
            "${MIWEAR_TFLITE_BUILD_DIR}/pthreadpool/*.a"
        )
        if(_miwear_tflite_archives)
            target_compile_definitions(miwear-band PRIVATE MIWEAR_HAVE_TFLITE)
            target_include_directories(miwear-band PRIVATE "${TENSORFLOW_SOURCE_DIR}")
            target_link_libraries(miwear-band PRIVATE -Wl,--start-group ${_miwear_tflite_archives} -Wl,--end-group)
            set(_miwear_tflite_linked TRUE)
            message(STATUS "Sleep ML: linked TensorFlow Lite from ${MIWEAR_TFLITE_BUILD_DIR}")
        endif()
    endif()
    if(NOT _miwear_tflite_linked AND EXISTS "${TENSORFLOW_SOURCE_DIR}/tensorflow/lite/CMakeLists.txt")
        set(TFLITE_ENABLE_XNNPACK OFF CACHE BOOL "" FORCE)
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        add_subdirectory(
            "${TENSORFLOW_SOURCE_DIR}/tensorflow/lite"
            "${CMAKE_BINARY_DIR}/tflite-lite"
            EXCLUDE_FROM_ALL
        )
        target_compile_definitions(miwear-band PRIVATE MIWEAR_HAVE_TFLITE)
        target_include_directories(miwear-band PRIVATE "${TENSORFLOW_SOURCE_DIR}")
        target_link_libraries(miwear-band PRIVATE tensorflow-lite)
        set(_miwear_tflite_linked TRUE)
        message(STATUS "Sleep ML: built and linked TensorFlow Lite from ${TENSORFLOW_SOURCE_DIR}")
    endif()
    if(NOT _miwear_tflite_linked)
        message(WARNING "Sleep ML: no TensorFlow Lite link (run scripts/build-sleep-tflite.sh); embedded stage packets only")
    endif()
endif()

add_dependencies(miwear-band miwear_schemas)

add_executable(miwear-schema-dump tools/schema_dump.c
    src/activity/schema_reader.c
    src/activity/mi_fitness_data.c
    src/activity/mi_fitness_validity.c
    src/activity/mi_fitness_oneden.c
    src/activity/mi_fitness_sport_report.c
    src/activity/outdoor_step_report_fields.c
    src/activity/workout_types.c
)
target_include_directories(miwear-schema-dump PRIVATE include)
target_link_libraries(miwear-schema-dump PRIVATE PkgConfig::GLIB PkgConfig::JSON m)
target_compile_definitions(miwear-schema-dump PRIVATE
    MIWEAR_SCHEMA_DIR="${MIWEAR_SCHEMA_EXTRACT_DIR}"
)
add_dependencies(miwear-schema-dump miwear_schemas)

install(TARGETS miwear-band RUNTIME DESTINATION bin)
