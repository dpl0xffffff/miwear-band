cmake_minimum_required(VERSION 3.16)
project(miwear_band C)

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
pkg_check_modules(GEOCLUE REQUIRED IMPORTED_TARGET libgeoclue-2.0)
find_library(BLUETOOTH_LIB bluetooth REQUIRED)

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
    src/core/local_time.c
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
    src/activity/daily_details.c
    src/activity/manual.c
    src/activity/workout_summary.c
    src/activity/workout_record.c
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
    src/spp/workout_gps.c
    src/sleep/mi_sleep_external.c
    src/activity/mi_fitness_sport_report.c
    src/activity/mi_fitness_report_tables.c
    src/ui/ui.c
    src/ui/dashboard.c
    src/ui/widgets.c
    src/ui/sleep_view.c
    src/ui/workouts_view.c
    src/ui/device_view.c
    src/ui/health_data.c
    src/db/db.c
    src/db/workout_store.c
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
    PkgConfig::GEOCLUE
    PkgConfig::PROTOBUF_C
    PkgConfig::SQLITE3
    ${BLUETOOTH_LIB}
    OpenSSL::Crypto
    m
)

target_compile_definitions(miwear-band PRIVATE
    MIWEAR_SCHEMA_DIR="${MIWEAR_SCHEMA_EXTRACT_DIR}"
    MIWEAR_ICONS_DIR="${CMAKE_SOURCE_DIR}/src/resources/icons"
)

add_dependencies(miwear-band miwear_schemas)

add_executable(miwear-schema-dump tools/schema_dump.c
    src/activity/schema_reader.c
    src/activity/mi_fitness_data.c
    src/activity/mi_fitness_validity.c
    src/activity/mi_fitness_oneden.c
    src/activity/workout_types.c
    src/activity/mi_fitness_report_tables.c
)
target_include_directories(miwear-schema-dump PRIVATE include)
target_link_libraries(miwear-schema-dump PRIVATE PkgConfig::GLIB PkgConfig::JSON m)
target_compile_definitions(miwear-schema-dump PRIVATE
    MIWEAR_SCHEMA_DIR="${MIWEAR_SCHEMA_EXTRACT_DIR}"
)
add_dependencies(miwear-schema-dump miwear_schemas)

enable_testing()
add_executable(miwear-parser-test
    test/parser_test.c
    src/activity/daily_details.c
    src/sleep/mi_sleep_packets.c
    src/sleep/mi_sleep_duration.c
)
target_include_directories(miwear-parser-test PRIVATE include)
target_link_libraries(miwear-parser-test PRIVATE PkgConfig::GLIB PkgConfig::GTK PkgConfig::ADW PkgConfig::PROTOBUF_C
    PkgConfig::SQLITE3 PkgConfig::JSON)
add_test(NAME parsers COMMAND miwear-parser-test)

add_executable(miwear-report-test
    test/report_test.c
    src/activity/mi_fitness_sport_report.c
    src/activity/mi_fitness_report_tables.c
    src/activity/mi_fitness_oneden.c
    src/activity/mi_fitness_validity.c
    src/activity/mi_fitness_data.c
    src/activity/schema_reader.c
    src/activity/workout_types.c
)
target_include_directories(miwear-report-test PRIVATE include)
target_link_libraries(miwear-report-test PRIVATE PkgConfig::GLIB PkgConfig::JSON m)
target_compile_definitions(miwear-report-test PRIVATE MIWEAR_SCHEMA_DIR="${MIWEAR_SCHEMA_EXTRACT_DIR}")
add_dependencies(miwear-report-test miwear_schemas)
add_test(NAME reports COMMAND miwear-report-test)
add_executable(miwear-local-time-test test/local_time_test.c src/core/local_time.c)
target_include_directories(miwear-local-time-test PRIVATE include)
target_link_libraries(miwear-local-time-test PRIVATE PkgConfig::GLIB)
foreach(_tz Europe/Berlin America/New_York Australia/Lord_Howe Asia/Kolkata UTC)
    string(REPLACE "/" "_" _tz_name "${_tz}")
    add_test(NAME local_time_${_tz_name} COMMAND miwear-local-time-test)
    set_tests_properties(local_time_${_tz_name} PROPERTIES ENVIRONMENT "TZ=${_tz}")
endforeach()

install(TARGETS miwear-band RUNTIME DESTINATION bin)
