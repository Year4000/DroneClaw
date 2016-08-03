/*
    DroneClaw copyright 2016
*/

#ifndef _PID_
#define _PID_

#include "MPU.hpp"

//#define PITCH_ROLL_DEBUG // Show Pitch and Roll debug values

#define FREQUENCY 0.0000611
#define FREQUENCY_RAD FREQUENCY * DEG_TO_RAD
#define MAX_TILT 400

#define P_ROLL 1.3
#define I_ROLL 0.04
#define D_ROLL 18.0

#define P_PITCH 1.3
#define I_PITCH 0.04
#define D_PITCH 18.0

#define P_YAW 4.0
#define I_YAW 0.02
#define D_YAW 0.0

// mpu
static boolean _gyro_angles;
static float _pitch_angle;
static float _pitch_accelerometer;
static float _pitch;
static float _roll_angle;
static float _roll_accelerometer;
static float _roll;
static float _yaw;
// pid
static float _pid_pitch[2] = {};
static float _pid_roll[2] = {};
static float _pid_yaw[2] = {};

class PID {
  private:
    /** The PID math */
    inline int pid(double p_gain, double i_gain, double d_gain, float &proportional, float &intergral, const float &delta, const float &mpu) {
      float derivative = mpu - delta - proportional;
      proportional = mpu - delta;
      intergral += proportional;
      float pid = p_gain * proportional + i_gain * intergral + d_gain * derivative;
      if (pid > MAX_TILT) {
        return MAX_TILT;
      } else if (pid < -MAX_TILT) {
        return -MAX_TILT;
      }
      return pid;
    }
  public:
    inline PID() {
      MPU mpu;
      Vector<int> gyro = mpu.gyro();
      Vector<long> accel = mpu.accelerometer();
      // Gyro angle calculations
      _pitch_angle += gyro.y * FREQUENCY;
      _roll_angle += gyro.x * FREQUENCY;
      _pitch_angle -= _roll_angle * sin(gyro.z * FREQUENCY_RAD);
      _roll_angle += _pitch_angle * sin(gyro.z * FREQUENCY_RAD);
      // Accelerometer angle calculations
      long accelerometer_total = sqrt(sq(accel.x) + sq(accel.y) + sq(accel.z));
      _pitch_accelerometer = asin((float) accel.y / accelerometer_total) * RAD_TO_DEG;
      _roll_accelerometer = asin((float) accel.x / accelerometer_total) * RAD_TO_DEG;
      if (_gyro_angles) {
        _pitch_angle = _pitch_angle * 0.9996 + _pitch_accelerometer * 0.0004;
        _roll_angle = _roll_angle * 0.9996 + _roll_accelerometer * 0.0004;
      } else {
        _pitch_angle = _pitch_accelerometer;
        _roll_angle = _roll_accelerometer;
        _gyro_angles = true;
      }
      // To dampen the pitch and roll angles a complementary filter is used
      _pitch = _pitch * 0.9 + _pitch_angle * 0.1;
      _roll = _roll * 0.9 + _roll_angle * 0.1;
      _yaw = gyro.z;
    }
    /** Caculate the PID for the pitch */
    inline float pitch(const float &pitch) {
      #ifdef PITCH_ROLL_DEBUG
      Serial.print(_pitch);
      Serial.print(",");
      #endif
      return pid(P_PITCH, I_PITCH, D_PITCH, _pid_pitch[0], _pid_pitch[1], pitch, _pitch);
    }
    /** Caculate the PID for the roll */
    inline float roll(const float &roll) {
      #ifdef PITCH_ROLL_DEBUG
      Serial.print(_roll);
      Serial.print(",");
      #endif
      return pid(P_ROLL, I_ROLL, D_ROLL, _pid_roll[0], _pid_roll[1], roll, _roll);
    }
    /** Caculate the PID for the yaw */
    inline float yaw(const float &yaw) {
      #ifdef PITCH_ROLL_DEBUG
      Serial.print(_yaw);
      Serial.println();
      #endif
      return pid(P_YAW, I_YAW, D_YAW, _pid_yaw[0], _pid_yaw[1], yaw, _yaw);
    }
    /** Reset PID where needed */
    inline static void reset_pid() {
      _pid_pitch[2] = {};
      _pid_roll[2] = {};
      _pid_yaw[2] = {};
    }
};

#endif
