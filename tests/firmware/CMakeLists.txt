# Minimal, dependency-free host test harness for firmware logic.
#
# This is a STANDALONE CMake project (its own project()/enable_testing()),
# never add_subdirectory()'d from the root ARM firmware build. It is
# configured directly by tools/run_host_tests.sh using the host compiler,
# so firmware-only logic (decode/init math, formatters, etc.) can be unit
# tested without any embedded toolchain or board.
cmake_minimum_required(VERSION 3.20)
project(firmware_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Werror)

enable_testing()

add_executable(test_harness_meta test_harness_meta.c)
target_include_directories(test_harness_meta PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME harness_meta COMMAND test_harness_meta)

# Portable drivers are HAL-free by contract, so they compile against the host
# compiler unchanged. Each driver exposes an INTERFACE target carrying its
# sources and include path, which is all a host test needs to link.
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_subdirectory(${REPO_ROOT}/drivers/imu/bmi088 ${CMAKE_CURRENT_BINARY_DIR}/drivers_imu_bmi088)
add_subdirectory(${REPO_ROOT}/drivers/util/fmt ${CMAKE_CURRENT_BINARY_DIR}/drivers_util_fmt)
add_subdirectory(${REPO_ROOT}/middleware/imu ${CMAKE_CURRENT_BINARY_DIR}/middleware_imu)

# Registers one host test executable per source file, linked against the
# driver under test and the shared harness headers.
function(add_firmware_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${test_name} PRIVATE ${ARGN})
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_firmware_test(test_bmi088_gyro_init test_bmi088_gyro_init.c driver_bmi088_gyro)
add_firmware_test(test_bmi088_gyro_init_errors test_bmi088_gyro_init_errors.c driver_bmi088_gyro)
add_firmware_test(test_bmi088_gyro_read test_bmi088_gyro_read.c driver_bmi088_gyro)
add_firmware_test(test_bmi088_gyro_scale test_bmi088_gyro_scale.c driver_bmi088_gyro)
add_firmware_test(test_bmi088_accel test_bmi088_accel.c driver_bmi088_accel)
add_firmware_test(test_imu_stationarity test_imu_stationarity.c middleware_imu)
add_firmware_test(test_imu_calibration test_imu_calibration.c middleware_imu)
add_firmware_test(test_imu_angular_acceleration test_imu_angular_acceleration.c middleware_imu)
add_firmware_test(test_imu_attitude test_imu_attitude.c middleware_imu)
add_firmware_test(test_imu_drift test_imu_drift.c middleware_imu)
add_firmware_test(test_rm_c_imu_pipeline test_rm_c_imu_pipeline.c middleware_imu)
target_sources(test_rm_c_imu_pipeline PRIVATE
    ${REPO_ROOT}/apps/rm_c_blinky/src/imu_pipeline.c
)
target_include_directories(test_rm_c_imu_pipeline PRIVATE
    ${REPO_ROOT}/apps/rm_c_blinky/src
)
add_firmware_test(test_fmt_i32 test_fmt_i32.c util_fmt_i32)
add_firmware_test(test_fmt_u32 test_fmt_u32.c util_fmt_i32)
add_executable(test_gyro_snapshot
    test_gyro_snapshot.c
    ${REPO_ROOT}/apps/rm_c_blinky/src/gyro_snapshot.c
)
target_include_directories(test_gyro_snapshot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REPO_ROOT}/apps/rm_c_blinky/src
)
target_link_libraries(test_gyro_snapshot PRIVATE driver_bmi088_gyro)
add_test(NAME test_gyro_snapshot COMMAND test_gyro_snapshot)
