//
// Types.hpp - Common data types for robot platform interface
//
// These types define the interface between the controller and the platform (hardware/simulation).
// RobotState: sensor data read from platform
// RobotCommand: actuator commands sent to platform
//

#ifndef RBQ_COMMON_TYPES_HPP
#define RBQ_COMMON_TYPES_HPP

#include <Eigen/Dense>
#include "Constants.hpp"
#include "ENumClasses.hpp"

namespace rbq10 {

/**
 * @brief Gamepad input data (from network/console, not robot hardware)
 */
struct Gamepad {
    float leftStickX = 0.0f;
    float leftStickY = 0.0f;
    float rightStickX = 0.0f;
    float rightStickY = 0.0f;
    float leftTrigger = 0.0f;
    float rightTrigger = 0.0f;
    bool buttons[16] = {false};
    bool connected = false;
};

/**
 * @brief Robot state data read from sensors (hardware or simulation)
 *
 * This struct contains only the raw sensor data that comes from the platform.
 * Estimated/filtered states should be stored in ControlSharedMemory.
 *
 * Based on RBQ_API sensor interface:
 * - Joint: position, velocity, torque (Kp/Kd are command-side)
 * - IMU: quaternion, gyro, acc
 * - Status: controlStart, canCheck, findHome
 * - Power: batteryVoltage
 */
struct RobotState {
    // IMU data (from RBQ_API::Imu)
    Eigen::Quaterniond imuQuat; ///< Orientation quaternion (w, x, y, z)
    Eigen::Vector3d imuRpy;     ///< Roll-Pitch-Yaw [rad]
    Eigen::Vector3d imuGyro;    ///< Angular velocity [rad/s]
    Eigen::Vector3d imuAcc;     ///< Linear acceleration [m/s²]

    // Motor feedback data (from RBQ_API::Joint)
    double motorPosition[MAX_JOINT]; ///< Joint position [rad]
    double motorVelocity[MAX_JOINT]; ///< Joint velocity [rad/s]
    double motorTorque[MAX_JOINT];   ///< Joint torque [Nm]

    // Drive-side telemetry. Nothing in the control path reads these -- they are
    // carried for the log, so a recording can say what the DRIVE was given and
    // what it drew, independently of what this process commanded. Under the
    // vendor stack our own command fields are idle values; these are not.
    double motorCurrent[MAX_JOINT] = {};  ///< Phase current [A]
    double motorRefPos[MAX_JOINT]  = {};  ///< pos_ref as the drive holds it [rad]
    double motorRefVel[MAX_JOINT]  = {};  ///< drive's q̇_des [rad/s]; 0 under Dgain Mode 2
    double motorRefTau[MAX_JOINT]  = {};  ///< feedforward torque the drive received [Nm]
    double motorKp[MAX_JOINT]      = {};  ///< gain the drive actually holds
    double motorKd[MAX_JOINT]      = {};
    double motorTempCoil[MAX_JOINT] = {}; ///< Coil temperature [C]
    int    motorOwner[MAX_JOINT]    = {}; ///< Motion owner id; who is driving this joint

    // System status (from RBQ_API::Status, PowerControl)
    double batteryVoltage = 0.0; ///< Battery voltage [V], higher of the two packs
    double batteryVoltagePack[2] = {0.0, 0.0}; ///< Per-pack [V]
    double batteryCurrentPack[2] = {0.0, 0.0}; ///< Per-pack [A]; sum(V*I) is drawn power
    bool controlStart = false;   ///< Motor control enabled (CON_START)
    bool canCheck = false;       ///< CAN communication OK
    bool findHome = false;       ///< Encoder homing complete
    bool isInitialized = false;  ///< Platform initialization complete

    RobotState() {
        imuQuat = Eigen::Quaterniond::Identity();
        imuRpy.setZero();
        imuGyro.setZero();
        imuAcc.setZero();
        for (int i = 0; i < MAX_JOINT; i++) {
            motorPosition[i] = 0.0;
            motorVelocity[i] = 0.0;
            motorTorque[i] = 0.0;
        }
    }
};

/**
 * @brief Robot command data to send to actuators
 *
 * Commands are sent to the platform via RobotInterface::Write()
 */
struct RobotCommand {
    double motorPositionDes[MAX_JOINT];
    double motorVelocityDes[MAX_JOINT];
    double motorTorqueDes[MAX_JOINT];
    double motorKp[MAX_JOINT];
    double motorKd[MAX_JOINT];

    RobotCommand() {
        for (int i = 0; i < MAX_JOINT; i++) {
            motorPositionDes[i] = 0.0;
            motorVelocityDes[i] = 0.0;
            motorTorqueDes[i] = 0.0;
            motorKp[i] = 0.0;
            motorKd[i] = 0.0;
        }
    }
};

} // namespace rbq10

#endif // RBQ_COMMON_TYPES_HPP
