# Pure IMU math middleware. The interface target keeps the source portable
# across the host test compiler and a later MCU consumer.
cmake_minimum_required(VERSION 3.20)

add_library(middleware_imu INTERFACE)

target_sources(middleware_imu INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_stationarity.c
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_stationary_experiment.c
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_temperature_gate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_calibration.c
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_angular_acceleration.c
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_attitude.c
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_quaternion.c
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_drift.c
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_zaru.c
)

target_include_directories(middleware_imu INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(middleware_imu INTERFACE m)
