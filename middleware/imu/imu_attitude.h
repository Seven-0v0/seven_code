/* Six-axis Mahony attitude estimation in the sensor frame. */
#ifndef MIDDLEWARE_IMU_ATTITUDE_H
#define MIDDLEWARE_IMU_ATTITUDE_H

#include "imu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Proportional and integral gains for the accelerometer correction. */
    float proportional_gain;
    float integral_gain;
    /* Inclusive acceleration magnitude gate in standard gravity units (g). */
    float min_acceleration_magnitude_g;
    float max_acceleration_magnitude_g;
} imu_attitude_config;

typedef struct {
    imu_attitude_config config;
    imu_quaternionf quaternion;
    float integral_error_x_rad_s;
    float integral_error_y_rad_s;
    float integral_error_z_rad_s;
} imu_attitude;

typedef struct {
    imu_quaternionf quaternion;
    imu_euler_zyx_deg euler_zyx_deg;
} imu_attitude_output;

/* Starts at the identity quaternion. The quaternion is scalar-first and
 * represents sensor-frame orientation relative to the reference frame.
 * Sensor axes are used unchanged; board axis/sign mapping is unvalidated.
 */
void imu_attitude_init(imu_attitude *attitude,
                       const imu_attitude_config *config);

/* Updates with angular rate in dps, acceleration in g, and caller-supplied
 * fixed dt_s. Acceleration correction is applied only inside the magnitude
 * gate; this rejects dynamic acceleration and makes zero acceleration safe.
 *
 * Axis-sign convention: with the sensor level the accelerometer reads
 * (0, 0, +1) g and all angles are zero. A right-hand rotation about the
 * sensor +x axis tips +y upward and reports positive roll; a right-hand
 * rotation about +y tips +x downward and reports positive pitch. Board
 * mounting may invert or permute axes; that mapping is the caller's and is
 * not applied here. Roll and pitch converge toward the measured gravity
 * vector. Yaw has no accelerometer reference and therefore only integrates
 * gyro z rate, with gyro bias causing yaw drift. Non-positive dt_s leaves
 * state unchanged.
 */
imu_attitude_output imu_attitude_update(imu_attitude *attitude,
                                        imu_gyro_dps gyro_dps,
                                        imu_acceleration_g acceleration_g,
                                        float dt_s);

#ifdef __cplusplus
}
#endif

#endif /* MIDDLEWARE_IMU_ATTITUDE_H */
