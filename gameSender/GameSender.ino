#include <WIFI.h>
#include <WiFiUDP.h>

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

// OTA libraries
#include <WebServer.h>
#include <Update.h>
#include <Adafruit_NeoPixel.h> // For signaling when OTA is active

// Pin defines
#define BTN_PIN 4
#define SDA_PIN 5
#define SCL_PIN 6

// OTA definitions
#define WIFI_MAX_WAIT_MILLIS 10000 // 10 secs

#define RGB_LED_PIN 21 // ESP32-C3 built-in RGB led
#define NUMPIXELS 1

Adafruit_NeoPixel rgbLed (NUMPIXELS, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// Network definitions
#include <network_protocol.hpp>


const char* ssid = "ESP32_AP";
const char* password = "password123";

WiFiUDP Udp;
unsigned int localPort = 1234;
char packetBuffer[PACKET_MAXLEN];

uint8_t playerId;

Adafruit_MPU6050 mpu;

float currentAngle = 0.0f;
unsigned long lastTime = 0;
const float ALPHA = 0.95f;

// OTA
WebServer server(80);
const char* uploadPage = R"rawliteral(
<form method='POST' action='/update' enctype='multipart/form-data'>
  <input type='file' name='update'>
  <input type='submit' value='Update'>
</form>
)rawliteral";

bool enabledOTAmode = false;

sensors_event_t a, g, temp;
float gyroBiasZ = 0;

void setupOTA();
void sendIntro();

void setup() {
  Serial.begin(115200);
  delay(10);
  pinMode(BTN_PIN, INPUT);

  rgbLed.begin();
  rgbLed.clear();
  rgbLed.show();


  Serial.println("");
  delay(100);
  
  unsigned long startAttemptTime = millis();

  WiFi.begin(ssid, password);

  Serial.print("Connecting to ");
  Serial.println(ssid);

  bool wifiConnected = false;

  // skip check if button pressed
  if(digitalRead(BTN_PIN) != HIGH) {
    while (millis() - startAttemptTime < WIFI_MAX_WAIT_MILLIS) {
      if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        break;
      }
      delay(200);
      Serial.print(".");
    }
  }

  enabledOTAmode = !wifiConnected;
  
  if (wifiConnected) {
    Serial.println('\n');
    Serial.println("Connected to the WiFi AP");
    Serial.print("IP address:\t");
    Serial.println(WiFi.localIP()); // Send IP to serial monitor
    
    Udp.begin(localPort);
    
    Wire.begin(SDA_PIN, SCL_PIN);

    if (!mpu.begin()) {
      Serial.println("Failed to find MPU6050 chip");
      rgbLed.setPixelColor(0, rgbLed.Color(255,0,255));
      rgbLed.show();
      while (1) {
        delay(10);
      }
    }
    Serial.println("MPU6050 Found!");
    rgbLed.clear();
    rgbLed.show();

    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);

    for(int i=0; i<100; i++) {
      mpu.getEvent(&a, &g, &temp);
      gyroBiasZ += g.gyro.z;
      delay(2);
    }
    gyroBiasZ /= 100.0f;

    sendIntro();

    delay(2000); // delay before proceeding with input reading/sending
  } else {
    Serial.println("\nFailed to connect. Entering OTA mode...");

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32_OTA", "password123");

    Serial.print("OTA IP: ");
    Serial.println(WiFi.softAPIP());

    setupOTA();
  }
}

void loop() {
  if(enabledOTAmode) {
    server.handleClient();
    if(digitalRead(BTN_PIN) == HIGH) {
      rgbLed.clear();
      rgbLed.show();
    }else {
      rgbLed.setPixelColor(0, rgbLed.Color(0,255,0));
      rgbLed.show();
    }
    return;
  }

  PacketResult r = receivePacket();
  switch(r.type) {
    case PACKET_ASSIGN_TYPE:
      playerId = r.data.assign.playerId;
      break;
    case PACKET_RECONNECT_TYPE:
      delay(random(100,500));
      sendIntro();
      delay(random(100,500));
      return;
  }

  mpu.getEvent(&a, &g, &temp);
  

  C2S_InputPacket p;
  p.type = PACKET_INPUT_TYPE;
  p.playerId = playerId;
  p.x = (int)a.acceleration.x*100;
  p.y = (int)a.acceleration.y*100;
  p.z = (int)a.acceleration.z*100;
  p.rotX = (int)g.gyro.x*100;
  p.rotY = (int)g.gyro.y*100;
  p.rotZ = (int)g.gyro.z*100;
  p.buttonPressed = (int)digitalRead(BTN_PIN) == HIGH;
  
  // Apply Complementary Filter to determine tilt and get a stable number
  unsigned long currentTime = micros();
  float dt = (lastTime > 0) ? ((currentTime - lastTime) / 1000000.0f) : 0.0f;
  lastTime = currentTime;

  float accelAngle = atan2(a.acceleration.x, a.acceleration.z) * (180.0f / PI);

  float gyroRateDeg = (g.gyro.z - gyroBiasZ) * (180.0f / PI);

  currentAngle = ALPHA * (currentAngle + (gyroRateDeg * dt)) + (1.0f - ALPHA) * accelAngle;
  
  p.tilt = (int)currentAngle*100;
  sendPacket(&p, sizeof(C2S_InputPacket));



  
  /* Print out the values */
  // Serial.print("Acceleration X: ");
  // Serial.print(a.acceleration.x);
  // Serial.print(", Y: ");
  // Serial.print(a.acceleration.y);
  // Serial.print(", Z: ");
  // Serial.print(a.acceleration.z);
  // Serial.println(" m/s^2");

  // Serial.print("Rotation X: ");
  // Serial.print(g.gyro.x);
  // Serial.print(", Y: ");
  // Serial.print(g.gyro.y);
  // Serial.print(", Z: ");
  // Serial.print(g.gyro.z);
  // Serial.println(" rad/s");

  // Serial.print("Temperature: ");
  // Serial.print(temp.temperature);
  // Serial.println(" degC");

  // Serial.println("");
  // delay(500);

  delay(10);
}

void sendIntro() {
  C2S_IntroPacket p;
  p.type = PACKET_INTRO_TYPE;
  sendPacket(&p, sizeof(C2S_IntroPacket));
}

void setupOTA() {
  rgbLed.setPixelColor(0, rgbLed.Color(0,255,0));
  rgbLed.show();

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", uploadPage);
  });

  server.on("/update", HTTP_POST,
    []() {
      server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
      delay(1000);
      ESP.restart();
    },
    []() {
      HTTPUpload& upload = server.upload();

      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update Start: %s\n", upload.filename.c_str());

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
        }

      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }

      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
          Serial.printf("Update Success: %u bytes\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      }
    }
  );

  server.begin();
}
