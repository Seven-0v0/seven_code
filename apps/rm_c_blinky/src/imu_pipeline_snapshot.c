#include "imu_pipeline_snapshot.h"

void imu_pipeline_snapshot_apply(gyro_snapshot_payload *snapshot,
                                 const imu_pipeline_output *output) {
    snapshot->accel_x_g = output->acceleration_g.x_g;
    snapshot->accel_y_g = output->acceleration_g.y_g;
    snapshot->accel_z_g = output->acceleration_g.z_g;
    snapshot->calibrated_gyro_bias_x_dps = output->gyro_bias_dps.x_dps;
    snapshot->calibrated_gyro_bias_y_dps = output->gyro_bias_dps.y_dps;
    snapshot->calibrated_gyro_bias_z_dps = output->gyro_bias_dps.z_dps;
    snapshot->angular_velocity_x_dps = output->angular_velocity_dps.x_dps;
    snapshot->angular_velocity_y_dps = output->angular_velocity_dps.y_dps;
    snapshot->angular_velocity_z_dps = output->angular_velocity_dps.z_dps;
    snapshot->angular_acceleration_x_dps2 =
        output->angular_acceleration_dps2.x_dps2;
    snapshot->angular_acceleration_y_dps2 =
        output->angular_acceleration_dps2.y_dps2;
    snapshot->angular_acceleration_z_dps2 =
        output->angular_acceleration_dps2.z_dps2;
    snapshot->quaternion_w = output->attitude.quaternion.w;
    snapshot->quaternion_x = output->attitude.quaternion.x;
    snapshot->quaternion_y = output->attitude.quaternion.y;
    snapshot->quaternion_z = output->attitude.quaternion.z;
    snapshot->roll_deg = output->attitude.euler_zyx_deg.roll_deg;
    snapshot->pitch_deg = output->attitude.euler_zyx_deg.pitch_deg;
    snapshot->yaw_deg = output->attitude.euler_zyx_deg.yaw_deg;
    snapshot->calibration_accepted_samples = output->calibration_accepted_samples;
    snapshot->calibration_complete = output->calibration_complete;
    snapshot->sample_dt_s = output->sample_dt_s;
    snapshot->skipped_cycles = output->skipped_cycles;
    snapshot->temperature_degc = output->temperature_degc;
    snapshot->temperature_slope_degc_per_s =
        output->temperature_slope_degc_per_s;
    snapshot->temperature_valid = output->temperature_valid;
    snapshot->sample_stationary = output->sample_stationary;
    snapshot->experiment_phase = (uint32_t)output->experiment_phase;
    snapshot->settling_accepted_samples = output->settling_accepted_samples;
    snapshot->calibration_reset_count = output->calibration_reset_count;
    snapshot->hold_out_accepted_samples = output->hold_out_accepted_samples;
    snapshot->experiment_phase_elapsed_s = output->experiment_phase_elapsed_s;
    snapshot->experiment_phase_required_s = output->experiment_phase_required_s;
    snapshot->hold_out_wall_duration_s = output->hold_out_wall_duration_s;
    snapshot->hold_out_accepted_duration_s =
        output->hold_out_accepted_duration_s;
    snapshot->hold_out_unobserved_duration_s =
        output->hold_out_unobserved_duration_s;
    snapshot->bias_frozen = output->bias_frozen;
    snapshot->hold_out_valid = output->hold_out_valid;
    snapshot->hold_out_gyro_bias_x_dps = output->hold_out_bias_dps.x_dps;
    snapshot->hold_out_gyro_bias_y_dps = output->hold_out_bias_dps.y_dps;
    snapshot->hold_out_gyro_bias_z_dps = output->hold_out_bias_dps.z_dps;
    snapshot->drift_sample_count = output->drift.sample_count;
    snapshot->drift_observed_duration_s = output->drift.observed_duration_s;
    snapshot->drift_mean_x_dps = output->drift.mean_dps.x_dps;
    snapshot->drift_mean_y_dps = output->drift.mean_dps.y_dps;
    snapshot->drift_mean_z_dps = output->drift.mean_dps.z_dps;
    snapshot->drift_rms_x_dps = output->drift.rms_dps.x_dps;
    snapshot->drift_rms_y_dps = output->drift.rms_dps.y_dps;
    snapshot->drift_rms_z_dps = output->drift.rms_dps.z_dps;
    snapshot->yaw_drift_deg = output->drift.yaw_drift_deg;
    snapshot->yaw_drift_deg_per_min = output->drift.yaw_drift_deg_per_min;
    snapshot->calibrated_drift_mean_x_dps =
        output->calibrated_drift.mean_dps.x_dps;
    snapshot->calibrated_drift_mean_y_dps =
        output->calibrated_drift.mean_dps.y_dps;
    snapshot->calibrated_drift_mean_z_dps =
        output->calibrated_drift.mean_dps.z_dps;
    snapshot->calibrated_drift_rms_x_dps =
        output->calibrated_drift.rms_dps.x_dps;
    snapshot->calibrated_drift_rms_y_dps =
        output->calibrated_drift.rms_dps.y_dps;
    snapshot->calibrated_drift_rms_z_dps =
        output->calibrated_drift.rms_dps.z_dps;
    snapshot->calibrated_yaw_drift_deg =
        output->calibrated_drift.yaw_drift_deg;
    snapshot->calibrated_yaw_drift_deg_per_min =
        output->calibrated_drift.yaw_drift_deg_per_min;
}
