/*
 * Project StoveMate
 * Description:
 * Author:
 * Date:
 */

#define RTC_ADDRESS 0x51
#define SI7021_ADDRESS 0x40
#define MCP9600_ADDRESS 0x60

// Commands for RTC

// Commands for Si7021
#define SI7021_TEMP_READ_CMD 0xE3
#define SI7021_HUMIDITY_READ_CMD 0xE5

// Commands for Thermocouple Driver
#define MCP9600_TEMP_HOT_CMD 0x00

#define I2C_CLK_SPEED 100000

#define SUCCESS 0x00
#define I2C_ERROR 0x01

// Global state flags
bool display_enabled = false;

// Helper function to convert to F
uint16_t convert_to_f(uint16_t tempc) { return tempc * 9 / 5 + 32; }

uint8_t i2c_setup() {
  // I2C Setup
  Wire.setSpeed(I2C_CLK_SPEED);
  Wire.begin();

  // Test connnection to I2C peripherals
  if (Wire.requestFrom(RTC_ADDRESS, 1) == 0) {
    Serial1.println("Error: RTC I2C Setup");
    // return false;
  }

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

  Serial1.print("temp:");

  uint16_t temp_code = (Wire.read() & 0x00ff) << 8 | (Wire.read() & 0x00ff);
  uint16_t temp = ((175.72 * temp_code) / 0xffff - 46.85) * 100;
  Serial1.printf("%dC", temp); // print the temperature

  // TODO: format this for the web
  return temp;
}

uint16_t get_onboard_humidity() {
  // Si7021 Humidity
  Wire.beginTransmission(SI7021_ADDRESS);
  Wire.write(SI7021_HUMIDITY_READ_CMD); // sends one byte
  Wire.endTransmission();               // stop transaction
  Wire.requestFrom(SI7021_ADDRESS, 2);

  Serial1.print("\nhumidity:");

  uint16_t rh_code = (Wire.read() & 0x00ff) << 8 | (Wire.read() & 0x00ff);
  uint16_t rh = ((125 * rh_code) / 0xffff - 6) * 100;
  Serial1.printf("%d%%", rh); // print the humidity

  // TODO: format this for the web
  return rh;
}

uint16_t get_thermocouple_temp() {
  // MCP9600 Temperature
  Wire.beginTransmission(MCP9600_ADDRESS);
  Wire.write(MCP9600_TEMP_HOT_CMD); // sends one byte
  Wire.endTransmission();           // stop transaction
  Wire.requestFrom(MCP9600_ADDRESS, 2);

  Serial1.print("\nt-temp:");

  uint8_t upper = Wire.read();
  uint8_t lower = Wire.read();
  uint16_t temp = 0;

  // Depending on if its positive or negative
  if ((upper & 0x80) == 0x80) {
    temp = ((upper * 16 + lower / 16) - 4096) * 100;
  } else {
    temp = (upper * 16 + lower / 16) * 100;
  }

  Serial1.printf("%d", temp);

  // TODO: format this temp for the web
  return temp;
}

void pre_setup() {

  // Latch power GPIO

  // Turns off RGB
  RGB.control(true);
  RGB.brightness(0);
}

// Pre-start function
STARTUP(pre_setup());

char onboard_humidity_s[12];
char thermo_temp_s[12];
char onboard_temp_s[12];

// setup() runs once, when the device is first turned on.
void setup() {

  // Set LED to blink green
  RGB.brightness(63);
  RGB.color(0, 255, 0);

  // Debug serial
  Serial1.begin(115200);
  Serial1.println("\n\nStoveMate");

  // Check RTC Pin interrupt status

  // If interrupt, reset interrupt for next cycle

  // Check Motion pin interrupt status

  // If motion, set flag to turn on display

  // If button press, load the config from the server
  // Connect and get temperature ranges from server
  // Connect and get °C or °F mode
  // And enable display

  // Set Up I2C (check to make sure all devices are present)
  if (i2c_setup() != SUCCESS) {
    Serial1.println("Error: I2C Setup");
  }

  // Make measurements
  uint16_t onboard_temp = get_onboard_temp();
  uint16_t onboard_humidity = get_onboard_humidity();
  uint16_t thermo_temp = get_thermocouple_temp();

  // Publish em
  sprintf(onboard_temp_s, "%d", onboard_temp);
  sprintf(onboard_humidity_s, "%d", onboard_humidity);
  sprintf(thermo_temp_s, "%d", thermo_temp);

  // Set x second sleep cycle via RTC
}

// loop() runs over and over again, as quickly as it can execute.
void loop() {

  // Connect to cloud and publish
  Particle.connect();

  // Wait until connected
  while (!Particle.connected()) {
    Serial1.println("Error: nc");
  };

  bool ok = Particle.publish("temp", onboard_temp_s, PRIVATE, WITH_ACK);

  if (!ok) {
    Serial1.println("Error: temp pub");
  }

  ok = Particle.publish("humidity", onboard_humidity_s, PRIVATE, WITH_ACK);

  if (!ok) {
    Serial1.println("Error: humidity pub");
  }

  ok = Particle.publish("thermo_temp", thermo_temp_s, PRIVATE, WITH_ACK);

  if (!ok) {
    Serial1.println("Error: therm temp pub");
  }

  // Shutdown
  RGB.brightness(0);

  for (;;) {
  };

  // Go to bed
  // System.sleep(SLEEP_MODE_DEEP, 0);

  // Process variables
  // Particle.process();

  // Set gpio state here

  // Then loop infinitely
}
