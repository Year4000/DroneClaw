/*
    DroneClaw copyright 2016
*/
#include <EventLoop.h>
#include <Servo.h>
#include <SoftwareSerial.h>
#include <Wire.h>

// Will enable debug code throught the program
//#define DEBUG

#define FL_ESC 0
#define FR_ESC 1
#define BL_ESC 2
#define BR_ESC 3
#define FL_ESC_PIN 5
#define FR_ESC_PIN 6
#define BL_ESC_PIN 10
#define BR_ESC_PIN 11
#define CLAW 3
#define BT_RX 8
#define BT_TX 9
#define PACKETS 4
#define BAUD 9600

#ifdef DEBUG
void println(String);
#endif
void attach();
void all(byte);
void working();
void control();

EventLoop &scheduler = EventLoop::get();
struct {
  volatile int throttle = 0;
  volatile int pitch = 0;
  volatile int roll = 0;
} drone;
float p_error = 0;
float r_error = 0;
float i_pitch = 0;
float i_roll = 0;
long offset[3] = {}; // x, y, z
Servo servos[4] = {};
SoftwareSerial bluetooth(BT_RX, BT_TX);
Servo claw;

/** The data from the MPU in a struct form */
class MPU {
  #define ADDRESS 0x68
  #define START_ADDRESS 0x3b
  public:
    int accel_x, accel_y, accel_z;
    int gyro_x, gyro_y, gyro_z;
    int temp;
    /** Create the instance of this MPU struct with the values from the sensor */
    inline MPU() {
      Wire.beginTransmission(ADDRESS);
      Wire.write(START_ADDRESS);
      Wire.endTransmission();
      Wire.requestFrom(ADDRESS, 14);
      while (Wire.available() < 14);
      gyro_x = Wire.read() << 8 | Wire.read();
      gyro_y = Wire.read() << 8 | Wire.read();
      gyro_z = Wire.read() << 8 | Wire.read();
      temp = Wire.read() << 8 | Wire.read();
      accel_x = Wire.read() << 8 | Wire.read();
      accel_y = Wire.read() << 8 | Wire.read();
      accel_z = Wire.read() << 8 | Wire.read();
    }
    /** Get the raw output from the MPU */
    inline int* raw_output() {
      int raw[7];
      raw[0] = accel_x;
      raw[1] = accel_y;
      raw[2] = accel_z;
      raw[3] = temp;
      raw[4] = gyro_x;
      raw[5] = gyro_y;
      raw[6] = gyro_z;
      return raw;
    }
    /** Inits the Wire lib and the sensors */
    inline static void init() {
      Wire.begin();
      Wire.beginTransmission(ADDRESS);
      Wire.write(0x6b);
      Wire.write(0);
      Wire.endTransmission();
      Wire.beginTransmission(ADDRESS);
      Wire.write(0x1c);
      Wire.write(0x10);
      Wire.endTransmission();
      Wire.beginTransmission(ADDRESS);
      Wire.write(0x1b);
      Wire.write(0x08);
      Wire.endTransmission();
      Wire.beginTransmission(ADDRESS);
      Wire.write(0x1a);
      Wire.write(0x03);
      Wire.endTransmission();
    }
};

/** The packet class that can encode and decode data*/
class Packet {
  private:
    byte _id;
    void (*_function)(Stream&);
  public:
    inline Packet(byte id, void (*function)(Stream&)) {
      _id = id;
      _function = function;
    }
    /** This will decode the data */
    inline void decode(Stream &data) {
      _function(data);
    }
};

/** The packets the drone knows how to handle */
Packet packets[] = {
  // Ping packet used to make sure there is a connection
  Packet(0x00, [] (Stream &data) {
    // todo if last heart beat fails do something
  }),
  // Prime and arm packet, echo packet
  Packet(0x01, [] (Stream &data) {
    static boolean init = false;
    if (!init) {
      servos[FR_ESC].write(1);
      servos[FL_ESC].write(1);
      servos[BR_ESC].write(1);
      servos[BL_ESC].write(1);
      scheduler.repeat(control, 50, MILLIS);
    }
  }),
  // Send the pos to the claw
  Packet(0x02, [] (Stream &data) {
    #ifdef DEBUG
    int pos = data.parseInt();
    println("Claw Position: " + String(pos));
    claw.write(pos);
    #else
    claw.write(data.parseInt());
    #endif
  }),
  // Send data to all escs
  Packet(0x03, [] (Stream &data) {
    drone.throttle = data.parseInt();
    //drone.roll = data.parseInt();
    //drone.pitch = data.parseInt();
  }),
};

/** Will process the incomming packets */
void process_packets() {
  byte packet = -1;
  if (bluetooth.available()) {
    packet = bluetooth.parseInt(); // Get the packet id
  } else if (Serial.available()) {
    packet = Serial.parseInt();
  }
  if (packet >= 0 && packet < PACKETS) {
    #ifdef DEBUG
    println("Packet ID: " + String(packet));
    #endif
    if (bluetooth.available()) {
      packets[packet].decode(bluetooth); // Lets the packet process the rest of the data
    } else {
      packets[packet].decode(Serial); // Lets the packet process the rest of the data
    }
  } else {
    #ifdef DEBUG
    println("Not a valid packet id");
    #endif
  }
}

/** The main control loop */
void control() {
  // when throttle is less than 1000 disable escs
  if (drone.throttle < 1000) {
    servos[FR_ESC].write(0);
    servos[FL_ESC].write(0);
    servos[BR_ESC].write(0);
    servos[BL_ESC].write(0);
    return;
  }
  // caculate the pids
  MPU mpu;
  float pitch = map(mpu.gyro_y - offset[1], -4096, 4096, -90, 90);
  float roll = map(mpu.gyro_x - offset[0], -4096, 4096, -90, 90);
  // derivative
  float d_pitch = pitch - drone.pitch - p_error;
  float d_roll = roll - drone.roll - r_error;
  // proportional
  p_error = pitch - drone.pitch;
  r_error = roll - drone.roll;
  // intergral
  i_pitch += p_error;
  i_roll += r_error;
  #define MAX_TILT 150
  #define P_GAIN 0.5
  #define I_GAIN 0.02
  #define D_GAIN 10.0
  float pid_pitch = (P_GAIN * p_error +  I_GAIN * i_pitch + D_GAIN * d_pitch);
  float pid_roll = (P_GAIN * r_error +  I_GAIN * i_roll + D_GAIN * d_roll);
  if (pid_pitch > MAX_TILT) {
    pid_pitch = MAX_TILT;
  } else if (pid_pitch < -MAX_TILT) {
    pid_pitch = -MAX_TILT;
  }
  if (pid_roll > MAX_TILT) {
    pid_roll = MAX_TILT;
  } else if (pid_roll < -MAX_TILT) {
    pid_roll = -MAX_TILT;
  }
  int fl = drone.throttle - pid_pitch + pid_roll,
  fr = drone.throttle - pid_pitch - pid_roll,
  bl = drone.throttle + pid_pitch + pid_roll,
  br = drone.throttle + pid_pitch - pid_roll;
  #ifdef DEBUG // print motor values to graphs
  Serial.print(fr);
  Serial.print(",");
  Serial.print(br);
  Serial.print(",");
  Serial.print(fl);
  Serial.print(",");
  Serial.print(bl);
  Serial.println();
  #endif
  // write the data to the servos
  servos[FR_ESC].writeMicroseconds(fr);
  servos[BR_ESC].writeMicroseconds(br);
  servos[FL_ESC].writeMicroseconds(fl);
  servos[BL_ESC].writeMicroseconds(bl);
}

#ifdef DEBUG
/** Print a message to the Serial only when debug is defined */
void println(String msg) {
  Serial.println(msg);
  bluetooth.println(msg);
}
#endif

void setup() {
  // Get the sensors and ect ready before bluetooth connection is established
  bluetooth.begin(BAUD);
  Serial.begin(BAUD);
  claw.attach(CLAW);
  MPU::init();
  // close claw slowly before calibrate
  byte pos = 45;
  claw.write(pos);
  while (pos < 120) {
    claw.write(pos++);
    delay(125);
  }
  // Calibrate the gyro
  #define COUNT 2000
  for (int i = 0 ; i < COUNT; i++) {
    MPU mpu;
    offset[0] += mpu.gyro_x;
    offset[1] += mpu.gyro_y;
    offset[2] += mpu.gyro_z;
    delay(5);
  }
  offset[0] /= COUNT;
  offset[1] /= COUNT;
  offset[2] /= COUNT;
  // Make sure the connection is established
  unsigned long x = 0;
  while ((!Serial.available() && !bluetooth.available()) || (Serial.parseInt() != 0 && bluetooth.parseInt() != 0)) {
    claw.write(38 * sin(x++  * 0.0125) + 83); // min of 45~, max of 120~, period of 258
    delay(10);
  }
  claw.write(45);
  // show the header of the program
  #ifdef DEBUG
  println("\n----- DroneClaw -----\n");
  #endif
  // Set up escs
  #ifdef DEBUG
  println("Setting up the escs");
  #endif
  servos[FR_ESC].attach(FR_ESC_PIN);
  servos[FL_ESC].attach(FL_ESC_PIN);
  servos[BR_ESC].attach(BR_ESC_PIN);
  servos[BL_ESC].attach(BL_ESC_PIN);
  // Handle the packets
  scheduler.repeat(process_packets, 250, MILLIS);
}

void loop() {
  scheduler.process(); // The backbone of the system
}
