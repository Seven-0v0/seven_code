#include "imu_attitude.h"

#include <math.h>
#include <stdbool.h>

static const float imu_pi_f = 3.14159265358979323846f;

static imu_quaternionf imu_quaternion_normalize(imu_quaternionf quaternion) {
    const float norm_squared = quaternion.w * quaternion.w +
                               quaternion.x * quaternion.x +
                               quaternion.y * quaternion.y +
                               quaternion.z * quaternion.z;
    const float norm = sqrtf(norm_squared);
    if (norm <= 0.0f) {
        return (imu_quaternionf){1.0f, 0.0f, 0.0f, 0.0f};
    }
    const float inverse_norm = 1.0f / norm;
    return (imu_quaternionf){
        quaternion.w * inverse_norm,
        quaternion.x * inverse_norm,
        quaternion.y * inverse_norm,
        quaternion.z * inverse_norm,
    };
}

static imu_euler_zyx_deg imu_quaternion_to_euler(
    imu_quaternionf quaternion) {
    const float roll_numerator =
        2.0f * (quaternion.w * quaternion.x + quaternion.y * quaternion.z);
    const float roll_denominator =
        1.0f - 2.0f * (quaternion.x * quaternion.x + quaternion.y * quaternion.y);
    const float pitch_sine =
        2.0f * (quaternion.w * quaternion.y - quaternion.z * quaternion.x);
    const float yaw_numerator =
        2.0f * (quaternion.w * quaternion.z + quaternion.x * quaternion.y);
    const float yaw_denominator =
        1.0f - 2.0f * (quaternion.y * quaternion.y + quaternion.z * quaternion.z);
    float limited_pitch_sine = pitch_sine;
    if (limited_pitch_sine > 1.0f) {
        limited_pitch_sine = 1.0f;
    } else if (limited_pitch_sine < -1.0f) {
        limited_pitch_sine = -1.0f;
    }

    const float radians_to_degrees = 180.0f / imu_pi_f;
    return (imu_euler_zyx_deg){
        .roll_deg = atan2f(roll_numerator, roll_denominator) * radians_to_degrees,
        .pitch_deg = asinf(limited_pitch_sine) * radians_to_degrees,
        .yaw_deg = atan2f(yaw_numerator, yaw_denominator) * radians_to_degrees,
    };
}

static imu_attitude_output imu_attitude_output_for(
    const imu_attitude *attitude) {
    return (imu_attitude_output){
        .quaternion = attitude->quaternion,
        .euler_zyx_deg = imu_quaternion_to_euler(attitude->quaternion),
    };
}

void imu_attitude_init(imu_attitude *attitude,
                       const imu_attitude_config *config) {
    *attitude = (imu_attitude){
        .config = *config,
        .quaternion = {1.0f, 0.0f, 0.0f, 0.0f},
    };
}

imu_attitude_output imu_attitude_update(imu_attitude *attitude,
                                        imu_gyro_dps gyro_dps,
                                        imu_acceleration_g acceleration_g,
                                        float dt_s) {
    if (dt_s <= 0.0f) {
        return imu_attitude_output_for(attitude);
    }

    const float acceleration_squared_g2 =
        acceleration_g.x_g * acceleration_g.x_g +
        acceleration_g.y_g * acceleration_g.y_g +
        acceleration_g.z_g * acceleration_g.z_g;
    const float acceleration_magnitude_g = sqrtf(acceleration_squared_g2);
    const bool acceleration_is_valid =
        acceleration_magnitude_g >=
            attitude->config.min_acceleration_magnitude_g &&
        acceleration_magnitude_g <=
            attitude->config.max_acceleration_magnitude_g &&
        acceleration_magnitude_g > 0.0f;

    float corrected_x_rad_s =
        gyro_dps.x_dps * (imu_pi_f / 180.0f);
    float corrected_y_rad_s =
        gyro_dps.y_dps * (imu_pi_f / 180.0f);
    float corrected_z_rad_s =
        gyro_dps.z_dps * (imu_pi_f / 180.0f);

    if (acceleration_is_valid) {
        const float inverse_acceleration_magnitude =
            1.0f / acceleration_magnitude_g;
        const float ax = acceleration_g.x_g * inverse_acceleration_magnitude;
        const float ay = acceleration_g.y_g * inverse_acceleration_magnitude;
        const float az = acceleration_g.z_g * inverse_acceleration_magnitude;
        const imu_quaternionf q = attitude->quaternion;
        const float estimated_gravity_x = 2.0f * (q.x * q.z - q.w * q.y);
        const float estimated_gravity_y = 2.0f * (q.w * q.x + q.y * q.z);
        const float estimated_gravity_z =
            1.0f - 2.0f * (q.x * q.x + q.y * q.y);
        const float error_x = ay * estimated_gravity_z -
                              az * estimated_gravity_y;
        const float error_y = az * estimated_gravity_x -
                              ax * estimated_gravity_z;
        const float error_z = ax * estimated_gravity_y -
                              ay * estimated_gravity_x;

        attitude->integral_error_x_rad_s +=
            attitude->config.integral_gain * error_x * dt_s;
        attitude->integral_error_y_rad_s +=
            attitude->config.integral_gain * error_y * dt_s;
        attitude->integral_error_z_rad_s +=
            attitude->config.integral_gain * error_z * dt_s;
        corrected_x_rad_s += attitude->config.proportional_gain * error_x +
                             attitude->integral_error_x_rad_s;
        corrected_y_rad_s += attitude->config.proportional_gain * error_y +
                             attitude->integral_error_y_rad_s;
        corrected_z_rad_s += attitude->config.proportional_gain * error_z +
                             attitude->integral_error_z_rad_s;
    }

    const float half_dt_s = 0.5f * dt_s;
    const imu_quaternionf q = attitude->quaternion;
    attitude->quaternion.w +=
        (-q.x * corrected_x_rad_s - q.y * corrected_y_rad_s -
         q.z * corrected_z_rad_s) * half_dt_s;
    attitude->quaternion.x +=
        (q.w * corrected_x_rad_s + q.y * corrected_z_rad_s -
         q.z * corrected_y_rad_s) * half_dt_s;
    attitude->quaternion.y +=
        (q.w * corrected_y_rad_s - q.x * corrected_z_rad_s +
         q.z * corrected_x_rad_s) * half_dt_s;
    attitude->quaternion.z +=
        (q.w * corrected_z_rad_s + q.x * corrected_y_rad_s -
         q.y * corrected_x_rad_s) * half_dt_s;
    attitude->quaternion = imu_quaternion_normalize(attitude->quaternion);

    return imu_attitude_output_for(attitude);
}
