/*
 * Project StoveMate
 * Description:
 * Author:
 * Date:
 */
#include "adafruit-led-backpack.h"
#include <blynk.h>

#define RTC_ADDRESS 0x51
#define SI7021_ADDRESS 0x40
#define MCP9600_ADDRESS 0x60
#define DISPLAY_ADDRESS 0x70

// Commands for RTC

// Commands for Si7021
#define SI7021_TEMP_READ_CMD 0xE3
#define SI7021_HUMIDITY_READ_CMD 0xE5

// Commands for Thermocouple Driver
#define MCP9600_TEMP_HOT_CMD 0x00
#define MCP9600_STATUS_REGISTER_CMD 0x04

#define MCP9600_UPDATE_AVAIL_BITMAP 1 << 6

#define MCP9600_HYSTERISIS 10 // 10 degress of hysteriisis

#define I2C_CLK_SPEED 100000

#define SUCCESS 0x00
#define I2C_ERROR 0x01

#define ALARM_PIN D7
#define MOTION_PIN A0

// In ms
#define INACTIVITY_TIMEOUT 5 * 60 * 1000
#define LOW_ALARM_INTERVAL 500
#define COLLECT_INTERVAL 5 * 1000
#define UPLOAD_INTERVAL 30 * 1000

// Address locations in EEPROM for config info

#define CONFIG_EEPROM_ADDR 2

// Global state flags
bool display_enabled = false;
bool sensor_update = false;
bool sensor_collect = false;

// Global variables
uint16_t onboard_temp = 0;
uint16_t onboard_humidity = 0;
float thermo_temp = 0;
float previous_thermo_temp = 0;

uint8_t low_alarm_count = 0;

typedef enum { NO_ALARM, HIGH_ALARM, LOW_ALARM, SILENCED } alarm_t;

alarm_t alarm = NO_ALARM;

typedef enum { CELSIUS, FERINHEIT } units_t;

int units = CELSIUS;

struct Config {
  int thermo_lower_bound;
  int thermo_upper_bound;
  int units;
};

Config default_config = {300, 900, CELSIUS};
Config config = default_config;

// 7 segment display
Adafruit_7segment display = Adafruit_7segment();

// Timers
Timer sensor_update_timer(UPLOAD_INTERVAL, sensor_update_timer_evt);
Timer sensor_collect_timer(COLLECT_INTERVAL, sensor_collect_evt);
Timer low_alarm_timer(LOW_ALARM_INTERVAL, low_alarm_evt);
Timer sleep_timer(INACTIVITY_TIMEOUT, sleep_timer_evt);

// Helper function to convert to F
float convert_to_f(float tempc) { return tempc * 9 / 5 + 32.0; }

uint8_t i2c_setup() {
  // I2C Setup
  Wire.setSpeed(I2C_CLK_SPEED);
  Wire.begin();

  // Setup the display.
  display.begin(DISPLAY_ADDRESS);
  display.clear();
  display.setBrightness(255);

  // Test connnection to I2C peripherals
  // if (Wire.requestFrom(RTC_ADDRESS, 1) == 0) {
  //   Serial1.println("Error: RTC I2C Setup");
  //   // return false;
  // }

  // Test connnection to I2C peripherals
  if (Wire.requestFrom(SI7021_ADDRESS, 1) == 0) {
    Serial1.println("Error: Si7021 I2C Setup");
    // return false;
  }

  // Test connnection to I2C peripherals
  if (Wire.requestFrom(MCP9600_ADDRESS, 1) == 0) {
    Serial1.println("Error: Therm I2C Setup");
    // return false;
  }

  // Test connection to ISSI chip (display)

  return SUCCESS;
}

uint16_t get_onboard_temp() {
  // Si7021 Temperature
  Wire.beginTransmission(SI7021_ADDRESS);
  Wire.write(SI7021_TEMP_READ_CMD); // sends one byte
  Wire.endTransmission();           // stop transaction
  Wire.requestFrom(SI7021_ADDRESS, 2);

  // Serial1.print("temp:");

  uint16_t temp_code = (Wire.read() & 0x00ff) << 8 | (Wire.read() & 0x00ff);
  uint16_t temp = ((175.72 * temp_code) / 0xffff - 46.85) * 100;
  // Serial1.printf("%dC", temp); // print the temperature

  // TODO: format this for the web
  return temp;
}

uint16_t get_onboard_humidity() {
  // Si7021 Humidity
  Wire.beginTransmission(SI7021_ADDRESS);
  Wire.write(SI7021_HUMIDITY_READ_CMD); // sends one byte
  Wire.endTransmission();               // stop transaction
  Wire.requestFrom(SI7021_ADDRESS, 2);

  // Serial1.print("\nhumidity:");

  uint16_t rh_code = (Wire.read() & 0x00ff) << 8 | (Wire.read() & 0x00ff);
  uint16_t rh = ((125 * rh_code) / 0xffff - 6) * 100;
  // Serial1.printf("%d%%", rh); // print the humidity

  // TODO: format this for the web
  return rh;
}

float get_thermocouple_temp() {

  // MCP9600 Temperature
  Wire.beginTransmission(MCP9600_ADDRESS);
  Wire.write(MCP9600_STATUS_REGISTER_CMD); // sends one byte
  Wire.endTransmission();                  // stop transaction
  Wire.requestFrom(MCP9600_ADDRESS, 1);

  uint8_t status = Wire.read();

  // Check to make sure the sample is ready
  if (status & MCP9600_UPDATE_AVAIL_BITMAP) {
    // Serial1.println("Sample is ready");
  } else {
    Serial1.println("Error: Sample is NOT ready");
  }

  // MCP9600 Temperature
  Wire.beginTransmission(MCP9600_ADDRESS);
  Wire.write(MCP9600_TEMP_HOT_CMD); // sends one byte
  Wire.endTransmission();           // stop transaction
  Wire.requestFrom(MCP9600_ADDRESS, 2);

  // Serial1.print("\nt-temp:");

  uint8_t upper = Wire.read();
  uint8_t lower = Wire.read();
  float temp = 0;

  // Depending on if its positive or negative
  if ((upper & 0x80) == 0x80) {
    temp = ((upper * 16 + lower / 16.0) - 4096);
  } else {
    temp = (upper * 16 + lower / 16.0);
  }

  // Serial1.printf("%f\n", temp);

  // TODO: format this temp for the web
  return temp;
}

void pre_setup() {

  // Latch power GPIO

  // Turns off RGB
  // RGB.control(true);
  // RGB.brightness(0);
}

// Pre-start function
STARTUP(pre_setup());

// Syste mode macro
// SYSTEM_MODE(MANUAL);

// Function for updating data once connected
void cloud_event(system_event_t event, int param) {

  Serial1.println("cloud event");

  // if (param == cloud_status_connected) {
  //   Serial1.println("c.");
  //
  //   // bool ok = Particle.publish(
  //   //     "data",
  //   //     String::format("{\"ob_temp\":%f,\"ob_hum\":%d,\"thermo:\"%d}",
  //   //                    onboard_temp / 100.0, onboard_humidity,
  //   thermo_temp),
  //   //     PRIVATE, WITH_ACK);
  //   //
  //   // if (!ok) {
  //   //   Serial1.println("Error: data pub");
  //   // }
  //   //
  //
  //   bool ok = Particle.publish("humidity", String::format("%.2f",
  //   onboard_temp),
  //                              PRIVATE, WITH_ACK);
  //
  //   if (!ok) {
  //     Serial1.println("Error: temp pub");
  //   }
  //
  //   ok = Particle.publish("humidity", String::format("%.2f",
  //   onboard_humidity),
  //                         PRIVATE, WITH_ACK);
  //
  //   if (!ok) {
  //     Serial1.println("Error: humidity pub");
  //   }
  //
  //   ok = Particle.publish("thermo_temp", String::format("%d", thermo_temp),
  //                         PRIVATE, WITH_ACK);
  //
  //   if (!ok) {
  //     Serial1.println("Error: therm temp pub");
  //   }
  //
  //   // // Once we get the echo on this go to sleep.
  //   // ok = Particle.publish("sleep", PRIVATE, WITH_ACK);
  //   //
  //   // if (!ok) {
  //   //   Serial1.println("Error: sleep pub");
  //   // }
  //
  //   Serial1.println(".");
  // }
}

// Once we get the evt, sleep!
// void sleep_evt_handler(const char *event, const char *data) {
//   Serial1.println("sleep");
//
//   Particle.disconnect();
//   WiFi.disconnect();
//   WiFi.off();
//
//   System.sleep(SLEEP_MODE_DEEP, 0);
// }

void make_measurements() {
  // Make measurements
  onboard_temp = get_onboard_temp();
  onboard_humidity = get_onboard_humidity();
  previous_thermo_temp = thermo_temp;
  thermo_temp = get_thermocouple_temp();

  // Serial1.println();
}

bool push_measurements() {
  bool ok = Particle.publish(
      "temp", String::format("%.2f", onboard_temp / 100.0), PRIVATE, WITH_ACK);

  if (!ok) {
    Serial1.println("Error: temp pub");
    return ok;
  }

  ok = Particle.publish("humidity",
                        String::format("%.2f", onboard_humidity / 100.0),
                        PRIVATE, WITH_ACK);

  if (!ok) {
    Serial1.println("Error: humidity pub");
    return ok;
  }

  ok = Particle.publish("thermo_temp", String::format("%.3f", thermo_temp),
                        PRIVATE, WITH_ACK);

  if (!ok) {
    Serial1.println("Error: therm temp pub");
    return ok;
  }

  // Now write to Blynk
  Blynk.virtualWrite(V0, String::format("%.2f", onboard_humidity / 100.0));
  Blynk.virtualWrite(V1, String::format("%.2f", onboard_temp / 100.0));
  Blynk.virtualWrite(V2, String::format("%.3f", thermo_temp));

  return ok;
}

bool process_alarm() {

  alarm_t old_alarm_state = alarm;

  if (thermo_temp > config.thermo_upper_bound) {
    Serial1.println("h alarm");
    alarm = HIGH_ALARM;
  } else if (thermo_temp < config.thermo_lower_bound &&
             previous_thermo_temp > config.thermo_lower_bound) {
    Serial1.println("l alarm");
    alarm = LOW_ALARM;
    // Reset only if the hysteriisis has been surpassed
  } else if (alarm == HIGH_ALARM &&
             (thermo_temp < config.thermo_upper_bound - MCP9600_HYSTERISIS)) {
    alarm = NO_ALARM;
  } else if (alarm == LOW_ALARM &&
             (thermo_temp > config.thermo_lower_bound + MCP9600_HYSTERISIS)) {
    alarm = NO_ALARM;
  }

  // Send alarm to cloud
  if (old_alarm_state != alarm) {
    bool ok = Particle.publish("alarm", String::format("%d", alarm), PRIVATE,
                               WITH_ACK);

    if (!ok) {
      Serial1.println("Error: alarm pub");
      return ok;
    }
  }

  // Then set the PWM output accordingly
  // TODO: take over RGB led here
  switch (alarm) {
  case HIGH_ALARM:
    digitalWrite(ALARM_PIN, HIGH);
    break;
  case LOW_ALARM:
    digitalWrite(ALARM_PIN, HIGH);
    low_alarm_timer.start();
    break;
  case NO_ALARM:
    digitalWrite(ALARM_PIN, LOW);
    break;
  default:
    break;
  }

  return true;
}

// Forces the system to sleep if below the minimum threshold for 5 minutes
void sleep_timer_evt() {
  Serial1.println("sleep");

  // TODO: push sleep event and listen for the echo. Then sleep.
  display.writeDigitRaw(0, 0);
  display.writeDigitRaw(1, 0);
  display.writeDigitRaw(2, 0);
  display.writeDigitRaw(3, 0);

  System.sleep(SLEEP_MODE_DEEP, 0);
};

void sensor_update_timer_evt() { sensor_update = true; }
void sensor_collect_evt() { sensor_collect = true; }
void low_alarm_evt() {

  low_alarm_count++;

  if (low_alarm_count == 2) {
    digitalWrite(ALARM_PIN, HIGH);
  } else {
    digitalWrite(ALARM_PIN, LOW);
  }

  // Reset the count
  if (low_alarm_count >= 6) {
    low_alarm_count = 0;

    digitalWrite(ALARM_PIN, HIGH);
  }
}

void click_event(system_event_t event, int param) {

  // Set the alarm off
  digitalWrite(ALARM_PIN, LOW);

  // Turn off alarm
  alarm = NO_ALARM;

  // TODO: silence alarm for a certain amount of time
}

int update_thermo_upper_bound(String upper_bound) {
  config.thermo_upper_bound = upper_bound.toInt();

  Serial1.println("save upper bound");

  EEPROM.put(CONFIG_EEPROM_ADDR, config);
  return 0;
}

int update_thermo_lower_bound(String lower_bound) {
  config.thermo_lower_bound = lower_bound.toInt();

  Serial1.println("save lower bound");

  EEPROM.put(CONFIG_EEPROM_ADDR, config);
  return 0;
}

int update_units(String unit) {
  config.units = unit.toInt();

  Serial1.println("save units");

  EEPROM.put(CONFIG_EEPROM_ADDR, config);
  return 0;
}

void motion() { Serial1.println("\r\nmotion"); }

// setup() runs once, when the device is first turned on.
void setup() {

  // Debug serial
  Serial1.begin(115200);
  Serial1.println("\n\nStoveMate");

  // Update data when connected
  System.on(button_final_click, click_event);

  // Get EEPROM
  EEPROM.get(CONFIG_EEPROM_ADDR, config);

  if (uint32_t(config.units) == 0xffffffff) {

    Serial1.println("empty ee");

    config = default_config;

    EEPROM.put(CONFIG_EEPROM_ADDR, config);
  } else {

    Serial1.printf("config ok\nunits:%d\nupper:%d\nlower:%d\n", config.units,
                   config.thermo_upper_bound, config.thermo_lower_bound);
  }

  // Subscribe to sleep event..
  // Spark.subscribe("thermo_temp", sleep_evt_handler, MY_DEVICES);

  //  Cloud variables
  Particle.variable("t_l_bound", config.thermo_lower_bound);
  Particle.variable("t_u_bound", config.thermo_upper_bound);
  Particle.variable("units", config.units);

  Particle.function("up_t_u_bound", update_thermo_upper_bound);
  Particle.function("up_t_l_bound", update_thermo_lower_bound);
  Particle.function("up_units", update_units);

  // Set LED to blink green
  // RGB.brightness(63);
  // RGB.color(0, 255, 0);

  // Alarm output pin
  pinMode(ALARM_PIN, OUTPUT);
  digitalWrite(ALARM_PIN, LOW);

  // Check RTC Pin interrupt status

  // If interrupt, reset interrupt for next cycle

  // Check Motion pin interrupt status

  // If motion, set flag to turn on display
  attachInterrupt(MOTION_PIN, motion, CHANGE);

  // If button press, load the config from the server
  // Connect and get temperature ranges from server
  // Connect and get °C or °F mode
  // And enable display

  // Set Up I2C (check to make sure all devices are present)
  if (i2c_setup() != SUCCESS) {
    Serial1.println("Error: I2C Setup");
  }

  // Start measurement timer
  sensor_update_timer.start();
  sensor_collect_timer.start();

  // Trigger sensor collection + update
  sensor_collect = true;
  sensor_update = true;

  // // Turn on Wifi
  // WiFi.on();
  // WiFi.connect();
  //
  // while (!WiFi.ready()) {
  //   Serial1.println("nr");
  // }

  // Connect to cloud
  Particle.connect();

  // Begin!
  Blynk.begin(blynk_auth);

  // Set x second sleep cycle via RTC
}

// loop() runs over and over again, as quickly as it can execute.
void loop() {

  // Make all the measurements
  if (sensor_collect) {
    sensor_collect = false;

    make_measurements();

    // If we're above minimum, disable sleep timer
    if (thermo_temp > config.thermo_lower_bound) {
      Serial1.println("stop sleep_timer");
      sleep_timer.stop();
    } else {
      if (!sleep_timer.isActive()) {
        Serial1.println("start sleep_timer");
        // sleep_timer.start(); //TODO: re-enable
      }
    }

    // Update display(s) here.
    if (config.units == FERINHEIT) {
      display.print(convert_to_f(thermo_temp), DEC);
    } else {
      display.print(thermo_temp, DEC);
    }

    // Write display
    display.writeDisplay();
  }

  // Update to the cloud
  if (sensor_update) {
    Serial1.println("upload");

    sensor_update = false;

    // push updates to cloud
    push_measurements();

    // Determine if alarm is necessary
    process_alarm();
  }

  Blynk.run();

  // Connect to cloud and publish
  // if (Particle.connected()) {
  //   Particle.process();
  // }

  // Sleep for 30 seconds
  // System.sleep(30);

  // Shutdown
  // RGB.brightness(0);

  // Simulated shutdown

  // Go to bed
  // System.sleep(SLEEP_MODE_DEEP, 0);

  // Process variables
  // Particle.process();

  // Set gpio state here

  // Then loop infinitely
}
