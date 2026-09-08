# Portable BMI088 motion-sensor drivers.
#
# Build integration only: the driver is an INTERFACE target so consumers
# compile the source into their own binary with their own flags. That keeps
# the driver free of any toolchain, chip, or board assumption -- the same
# source is used by the ARM firmware target and by the host test harness.
cmake_minimum_required(VERSION 3.20)

add_library(driver_bmi088_gyro INTERFACE)

target_sources(driver_bmi088_gyro INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/bmi088_gyro.c
)

target_include_directories(driver_bmi088_gyro INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(driver_bmi088_accel INTERFACE)

target_sources(driver_bmi088_accel INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/bmi088_accel.c
)

target_include_directories(driver_bmi088_accel INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
