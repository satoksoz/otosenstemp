#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "segment60pt7b.h"
#include "segment40pt7b.h"
#include <qrcode_espi.h>
#include <math.h>
#include <AsyncWebSocket.h>
#include <WiFiClientSecure.h>
#include "SuperDMZ.h"
#include "arduino_secrets.h"

AsyncWebSocket ws("/ws");
bool clientConnected = false;

// Pin tanımlamaları
#define ADS_CS 34
#define ADS_MOSI 35
#define ADS_MISO 37
#define ADS_SCLK 36
#define ADS_DRDY 38
#define VREF 2.048
#define GAIN 16.0
#define FULL_SCALE 8388607.0

// Register adresleri
#define ADS1220_REG0 0x00
#define ADS1220_REG1 0x01
#define ADS1220_REG2 0x02
#define ADS1220_REG3 0x03

unsigned long calculatedRampTimeSec;
unsigned long remainingTimeSec = 0;
double lastSentTemp = 0;
unsigned long programStartTime = 0;
unsigned long stepStartTime = 0;
int currentStepIndex = 0;
int remainingHours = 0;
int remainingMinutes = 0;
bool inRampPhase = true;
double currentTargetTemp = 0;
double rampStartTemp = 0;
unsigned long lastStepUpdateTime = 0;
int programRepeatCounter = 0;
bool delayCompleted = false;
bool programCompleted = false;
bool delayActive = false;

typedef struct {
  double mV;
  double temp;
} TempLookupEntry;

// K-type thermocouple lookup table
const TempLookupEntry kTypeTable[] = {
  { -6.458, -270.0 }, { -6.404, -260.0 }, { -6.344, -250.0 }, { -6.277, -240.0 }, { -6.203, -230.0 }, { -6.121, -220.0 }, { -6.030, -210.0 }, { -5.929, -200.0 }, { -5.891, -195.0 }, { -5.818, -190.0 }, { -5.740, -180.0 }, { -5.520, -170.0 }, { -5.283, -160.0 }, { -5.027, -150.0 }, { -4.648, -140.0 }, { -4.380, -130.0 }, { -4.095, -120.0 }, { -3.792, -110.0 }, { -3.378, -100.0 }, { -3.096, -90.0 }, { -2.800, -80.0 }, { -2.490, -70.0 }, { -2.165, -60.0 }, { -1.824, -50.0 }, { -1.466, -40.0 }, { -1.091, -30.0 }, { -0.698, -20.0 }, { -0.286, -10.0 }, { 0.000, 0.0 }, { 0.397, 10.0 }, { 0.798, 20.0 }, { 1.196, 30.0 }, { 1.611, 40.0 }, { 2.035, 50.0 }, { 2.431, 60.0 }, { 2.908, 70.0 }, { 3.357, 80.0 }, { 3.715, 90.0 }, { 4.281, 100.0 }, { 4.755, 110.0 }, { 5.054, 120.0 }, { 5.728, 130.0 }, { 6.227, 140.0 }, { 6.458, 150.0 }, { 7.250, 160.0 }, { 7.774, 170.0 }, { 7.941, 180.0 }, { 8.847, 190.0 }, { 9.514, 210.0 }, { 10.520, 220.0 }, { 11.191, 240.0 }, { 12.099, 250.0 }, { 12.999, 270.0 }, { 14.094, 280.0 }, { 14.919, 300.0 }, { 16.050, 310.0 }, { 16.888, 330.0 }, { 18.050, 340.0 }, { 18.874, 360.0 }, { 20.050, 370.0 }, { 20.869, 390.0 }, { 22.050, 400.0 }, { 22.860, 420.0 }, { 24.050, 430.0 }, { 24.843, 450.0 }, { 26.050, 460.0 }, { 26.814, 480.0 }, { 28.050, 490.0 }, { 28.772, 510.0 }, { 30.050, 520.0 }, { 30.715, 540.0 }, { 32.050, 550.0 }, { 32.642, 570.0 }, { 34.050, 580.0 }, { 34.554, 600.0 }, { 36.050, 610.0 }, { 36.449, 630.0 }, { 38.050, 640.0 }, { 38.327, 660.0 }, { 40.050, 670.0 }, { 40.188, 690.0 }, { 42.050, 700.0 }, { 42.032, 720.0 }, { 43.858, 750.0 }, { 45.667, 780.0 }, { 47.458, 810.0 }, { 49.232, 840.0 }, { 50.988, 870.0 }, { 52.726, 900.0 }, { 54.446, 930.0 }, { 54.886, 1372.0 }
};
const int kTypeTableSize = sizeof(kTypeTable) / sizeof(TempLookupEntry);

const TempLookupEntry jTypeTable[] = {
  { -8.095, -210.0 }, { -7.890, -200.0 }, { -7.680, -190.0 }, { -7.465, -180.0 }, { -7.245, -170.0 }, { -7.020, -160.0 }, { -6.790, -150.0 }, { -6.699, -145.0 }, { -6.555, -140.0 }, { -6.315, -130.0 }, { -6.070, -120.0 }, { -5.820, -110.0 }, { -5.386, -100.0 }, { -5.125, -90.0 }, { -4.860, -80.0 }, { -4.590, -70.0 }, { -4.315, -60.0 }, { -3.923, -50.0 }, { -3.635, -40.0 }, { -3.340, -30.0 }, { -3.040, -20.0 }, { -2.735, -10.0 }, { -2.267, 0.0 }, { 0.000, 0.0 }, { 1.019, 20.0 }, { 2.058, 40.0 }, { 3.115, 60.0 }, { 4.190, 80.0 }, { 5.283, 100.0 }, { 6.394, 120.0 }, { 7.522, 140.0 }, { 8.668, 160.0 }, { 9.832, 180.0 }, { 11.013, 200.0 }, { 12.212, 220.0 }, { 13.429, 240.0 }, { 14.663, 260.0 }, { 15.915, 280.0 }, { 17.185, 300.0 }, { 18.472, 320.0 }, { 19.777, 340.0 }, { 21.100, 360.0 }, { 22.441, 380.0 }, { 23.800, 400.0 }, { 25.177, 420.0 }, { 26.572, 440.0 }, { 27.985, 460.0 }, { 29.417, 480.0 }, { 30.867, 500.0 }, { 32.336, 520.0 }, { 33.823, 540.0 }, { 35.329, 560.0 }, { 36.854, 580.0 }, { 38.398, 600.0 }, { 39.961, 620.0 }, { 41.543, 640.0 }, { 42.919, 1200.0 }
};
const int jTypeTableSize = sizeof(jTypeTable) / sizeof(TempLookupEntry);

const TempLookupEntry sTypeTable[] = {
  { -0.236, -50.0 }, { -0.226, -40.0 }, { -0.216, -30.0 }, { -0.205, -20.0 }, { -0.194, -10.0 }, { 0.000, 0.0 }, { 0.000, 0.0 }, { 0.323, 50.0 }, { 0.645, 100.0 }, { 0.982, 150.0 }, { 1.319, 200.0 }, { 1.670, 250.0 }, { 2.031, 300.0 }, { 2.406, 350.0 }, { 2.743, 400.0 }, { 2.743, 400.0 }, { 3.149, 450.0 }, { 3.555, 500.0 }, { 3.961, 550.0 }, { 4.366, 600.0 }, { 4.816, 650.0 }, { 5.267, 700.0 }, { 5.717, 750.0 }, { 6.168, 800.0 }, { 6.662, 850.0 }, { 7.152, 900.0 }, { 7.644, 950.0 }, { 8.136, 1000.0 }, { 8.136, 1000.0 }, { 8.665, 1050.0 }, { 9.194, 1100.0 }, { 9.723, 1150.0 }, { 10.251, 1200.0 }, { 10.812, 1250.0 }, { 11.372, 1300.0 }, { 11.932, 1350.0 }, { 12.492, 1400.0 }, { 13.078, 1450.0 }, { 13.664, 1500.0 }, { 14.250, 1550.0 }, { 14.835, 1600.0 }
};
const int sTypeTableSize = sizeof(sTypeTable) / sizeof(TempLookupEntry);

SPIClass adsSPI(FSPI);
SPISettings adsSPISettings(4000000, MSBFIRST, SPI_MODE1);
unsigned long delayEndTime = 0;
unsigned long rampEndTime = 0;
unsigned long stepEndTime = 0;

char sensorType = '2';
int32_t adcRaw;
double sensorTemp;
double totalTemp;

#define FILTER_SAMPLES 1
double mvHistory[FILTER_SAMPLES];
uint8_t sampleIndex = 0;

unsigned long lastTempMillis = 0;
unsigned long lastVoltMillis = 0;
unsigned long lastSerialCheck = 0;
double tempC = 0;
double adsvolt;

static uint8_t conv2d(const char *p) {
  return 10 * (p[0] - '0') + (p[1] - '0');
}
uint8_t hh = conv2d(__TIME__), mm = conv2d(__TIME__ + 3), ss = conv2d(__TIME__ + 6);

TFT_eSPI tft = TFT_eSPI();
QRcode_eSPI qrcode(&tft);

#define SSR_PIN 14
#define BUZZER_PIN 2
#define QRCODEVERSION 1

unsigned long previousMillis = 0;
const unsigned long interval = 1000;
bool forceAPMode;
byte omm = 99;
byte xcolon = 0;

double temperature;
double temperatureGec;

double pidSetpoint = 0;
double pidInput = 0;
double pidOutput = 0;
unsigned long pidLastTime = 0;
double pidErrorSum = 0;
double pidLastError = 0;
bool pidControlActive = false;

bool autotuneRunning = false;
int autotuneStep = 0;
unsigned long autotuneStartTime = 0;
float autotuneTargetTemp = 0;
float autotuneSetValue = 0;
int autotuneCycles = 0;
int autotuneMaxCycles = 4;
int autotuneProgress = 0;
String autotuneStatus = "Hazır";

double autotune_kp = 0;
double autotune_ki = 0;
double autotune_kd = 0;

float peakTemps[10];
int peakCount = 0;

#define EEPROM_I2C_ADDR 0x50
#define EEPROM_SIZE 65536
#define EEPROM_WIFI_SETTINGS_ADDR 256
#define EEPROM_SETTINGS_ADDR 512
#define EEPROM_LAST_STATE_ADDR 640
#define EEPROM_STEPS_START 1024
#define EEPROM_PAGE_SIZE 128
#define EEPROM_I2C_DATA_CHUNK 30
#define EEPROM_WRITE_TIMEOUT_MS 20
#define SDA_PIN 39
#define SCL_PIN 40

#define STORAGE_VERSION 1
#define SETTINGS_MAGIC 0x53455431UL
#define WIFI_MAGIC 0x57494631UL
#define STATE_MAGIC 0x53544131UL
#define STEPS_MAGIC 0x53544531UL

#define BUTTON1_PIN 3
#define BUTTON2_PIN 4
#define BUTTON3_PIN 5
#define BUTTON4_PIN 6

#define SENSOR_K_TYPE 0
#define SENSOR_J_TYPE 1
#define SENSOR_S_TYPE 2
#define SENSOR_PT100_2WIRE 3
#define SENSOR_PT100_3WIRE 4
#define SENSOR_PT100_4WIRE 5

#define MENU_ITEMS 5
const char *mainMenuItems[MENU_ITEMS] = { "Cikis", "Program", "Ayarlar", "Alarm", "QR Code" };
const char *programSubMenuItems[] = { "Geri", "Step Listesi", "Program Baslat/Durdur" };
const char *settingsSubMenuItems[] = { "Geri", "Sicaklik Ayarlari", "WiFi Ayarlari", "PID Ayarlari" };
const char *alarmSubMenuItems[] = { "Geri", "Alarm Sicakligi", "Alarm Durumu" };

int currentMenuLevel = 0;
int currentMainMenuSelection = 0;
int currentSubMenuSelection = 0;
bool inMenu = false;
unsigned long menuEnterTime = 0;
const unsigned long menuTimeout = 60000;

bool button1State = HIGH;
bool button2State = HIGH;
bool button3State = HIGH;
bool button4State = HIGH;
bool button1PrevState = HIGH;
bool button2PrevState = HIGH;
bool button3PrevState = HIGH;
bool button4PrevState = HIGH;
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
unsigned long lastDebounceTime3 = 0;
unsigned long lastDebounceTime4 = 0;
unsigned long debounceDelay = 50;

unsigned long button1PressStartTime = 0;
unsigned long button2PressStartTime = 0;
bool button1LongPress = false;
bool button2LongPress = false;

bool button4LongPress = false;
unsigned long button4PressStartTime = 0;

bool showingQRCode = false;
unsigned long qrCodeDisplayTime = 0;
const unsigned long longPressTime = 3000;
const unsigned long qrCodeDuration = 30000;

#define MAX_STEPS 10
uint8_t stepCount = 0;
int selectedIndex = -1;

bool isRunning = false;
uint16_t programRepeatCount = 1;

unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 10000;
volatile bool restartRequested = false;
unsigned long restartRequestTime = 0;

double interpolateTemp(const TempLookupEntry *table, int tableSize, double mV) {
  if (mV <= table[0].mV) return table[0].temp;
  if (mV >= table[tableSize - 1].mV) return table[tableSize - 1].temp;
  int i = 0;
  while (i < tableSize - 1 && table[i + 1].mV < mV) i++;
  double mV0 = table[i].mV;
  double mV1 = table[i + 1].mV;
  double temp0 = table[i].temp;
  double temp1 = table[i + 1].temp;
  return temp0 + (mV - mV0) * (temp1 - temp0) / (mV1 - mV0);
}

enum ProgramState {
  STATE_IDLE,
  STATE_DELAY,
  STATE_RAMP,
  STATE_STEP,
  STATE_AUTOTUNE,
  STATE_COMPLETED
};

ProgramState programState = STATE_IDLE;

struct ProgramStateData {
  bool isRunning;
  ProgramState programState;
  int currentStepIndex;
  int programRepeatCounter;
  unsigned long stepStartTime;
  unsigned long remainingTimeSec;
  double currentTemperature;
  double targetTemperature;
};

struct StorageHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  uint32_t crc32;
};

bool writeEEPROMBlock(uint16_t address, const uint8_t *data, size_t size);
bool readEEPROMBlock(uint16_t address, uint8_t *data, size_t size);
bool writeRecord(uint16_t address, uint32_t magic, const void *payload, size_t payloadSize);
bool readRecord(uint16_t address, uint32_t magic, void *payload, size_t payloadSize);

struct WiFiSettings {
  uint8_t mode;
  char ssid[32];
  char password[64];
  char ap_ssid[32];
  char ap_password[64];
  uint8_t useStaticIP;
  uint8_t ip[4];
  uint8_t gateway[4];
  uint8_t subnet[4];
  uint8_t dns1[4];
  uint8_t dns2[4];
  uint8_t ap_useStaticIP;
  uint8_t ap_ip[4];
  uint8_t ap_gateway[4];
  uint8_t ap_subnet[4];
};

struct Step {
  double sicaklik;
  uint8_t rampSaat;
  uint8_t rampDakika;
  uint8_t calismaSaat;
  uint8_t calismaDakika;
  double dakikaDegisim;
  bool dakikaCheck;
};

struct Settings {
  double kalibrasyon;
  double sensorFaktor;
  double kp;
  double ki;
  double kd;
  uint8_t sensorType;
  double autotuneTemp;
  double alarmTemperature;
  bool alarmEnabled;
  bool timedMode;
  uint8_t delayHours;
  uint8_t delayMinutes;
};

constexpr size_t STEPS_PAYLOAD_SIZE = 7 + MAX_STEPS * (sizeof(double) * 2 + 5);
static_assert(EEPROM_WIFI_SETTINGS_ADDR + sizeof(StorageHeader) + sizeof(WiFiSettings) <= EEPROM_SETTINGS_ADDR,
              "EEPROM WiFi ve ayar alanları çakışıyor");
static_assert(EEPROM_SETTINGS_ADDR + sizeof(StorageHeader) + sizeof(Settings) <= EEPROM_LAST_STATE_ADDR,
              "EEPROM ayar ve durum alanları çakışıyor");
static_assert(EEPROM_LAST_STATE_ADDR + sizeof(StorageHeader) + sizeof(ProgramStateData) <= EEPROM_STEPS_START,
              "EEPROM durum ve step alanları çakışıyor");
static_assert(EEPROM_STEPS_START + sizeof(StorageHeader) + STEPS_PAYLOAD_SIZE <= EEPROM_SIZE,
              "EEPROM step alanı bellek sınırını aşıyor");

WiFiSettings wifiSettings = {
  1,
  "modem5",
  "12345678",
  "ESP32-AP",
  "password123",
  0,
  { 192, 168, 100, 1 },
  { 192, 168, 100, 1 },
  { 255, 255, 255, 0 },
  { 8, 8, 8, 8 },
  { 8, 8, 4, 4 },
  1,
  { 192, 168, 100, 1 },
  { 192, 168, 100, 1 },
  { 255, 255, 255, 0 }
};

Settings settings = {
  0.0, 0.0, 0.0, 0.0, 0.0,
  SENSOR_PT100_2WIRE,
  100.0,
  100.0,
  false,
  true,
  0,
  0
};

AsyncWebServer server(80);
Step steps[MAX_STEPS];
SuperDMZ tunnel;

// Platform bağlantı ayarları
#define MIKRODMZ_PLATFORM_URL "https://otosenstemp-production.up.railway.app"
#define MIKRODMZ_ZONE "A1"
#define MIKRODMZ_FIRMWARE_VERSION "dmz33-1.0"
#define MIKRODMZ_USE_OWN_TUNNEL 1
#define MIKRODMZ_TUNNEL_WS_URL "wss://otosenstemp-production.up.railway.app/ws/device"

static const char MIKRODMZ_RAILWAY_ROOT_CA[] PROGMEM = R"CERT(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)CERT";

String platformDeviceId;

String buildPlatformDeviceId() {
  const uint64_t chipId = ESP.getEfuseMac();
  char id[24];
  snprintf(id, sizeof(id), "SAT-%04X%08X",
           static_cast<uint16_t>(chipId >> 32),
           static_cast<uint32_t>(chipId));
  return String(id);
}

WiFiClientSecure platformClient;
bool platformRegistered = false;
unsigned long lastPlatformTelemetry = 0;
unsigned long lastPlatformCommandPoll = 0;
const unsigned long platformTelemetryInterval = 10000;
const unsigned long platformCommandPollInterval = 3000;

bool platformRequest(const char *method, const String &path, const String &body, String &response) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(MIKRODMZ_PLATFORM_URL) + path;
  Serial.printf("MikroDMZ Node.js isteği: %s\n", url.c_str());
  platformClient.setCACert(MIKRODMZ_RAILWAY_ROOT_CA);
  if (!http.begin(platformClient, url)) return false;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-device-key", MIKRODMZ_DEVICE_KEY);
  http.addHeader("x-device-id", platformDeviceId);

  int statusCode = strcmp(method, "POST") == 0 ? http.POST(body) : http.GET();
  response = statusCode > 0 ? http.getString() : "";
  if (statusCode < 200 || statusCode >= 300) {
    Serial.printf("Platform HTTP hatası: %s -> %d (%s)\n", url.c_str(), statusCode, http.errorToString(statusCode).c_str());
  }
  http.end();
  return statusCode >= 200 && statusCode < 300;
}

void registerWithPlatform() {
  StaticJsonDocument<192> doc;
  doc["deviceId"] = platformDeviceId;
  doc["zone"] = MIKRODMZ_ZONE;
  doc["firmware"] = MIKRODMZ_FIRMWARE_VERSION;
  doc["localIp"] = WiFi.localIP().toString();
  String body;
  serializeJson(doc, body);
  String response;

  if (platformRequest("POST", "/api/devices/register", body, response)) {
    platformRegistered = true;
    Serial.println("MikroDMZ platform cihaz kaydı tamamlandı");
  } else {
    Serial.printf("MikroDMZ Node.js cihaz kaydı başarısız: %s\n", response.c_str());
  }
}

void sendPlatformTelemetry() {
  StaticJsonDocument<128> doc;
  doc["deviceId"] = platformDeviceId;
  doc["temperature"] = temperature;
  String body;
  serializeJson(doc, body);
  String response;

  if (platformRequest("POST", String("/api/devices/") + platformDeviceId + "/telemetry", body, response)) {
    Serial.printf("Platform telemetrisi gönderildi: %.1f C\n", temperature);
  }
}

void pollPlatformCommand() {
  String response;
  if (!platformRequest("GET", String("/api/devices/") + platformDeviceId + "/commands/next", "", response)) return;

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, response)) return;
  JsonObject command = doc["command"].as<JsonObject>();
  if (command.isNull()) return;

  const char *name = command["name"] | "";
  if (strcmp(name, "toggleRun") == 0) {
    bool requestedState = command["payload"]["run"] | false;
    if (requestedState != isRunning) toggleProgramState();
    Serial.printf("Platform komutu uygulandı: toggleRun=%s\n", requestedState ? "true" : "false");
  }
}

bool otaUpdateStarted = false;

void handleFirmwareUpload(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (index == 0) {
    otaUpdateStarted = Update.begin(total);
    if (!otaUpdateStarted) Update.printError(Serial);
  }
  if (otaUpdateStarted && Update.write(data, len) != len) Update.printError(Serial);
  if (otaUpdateStarted && index + len == total) {
    if (!Update.end(true)) Update.printError(Serial);
  }
}

// ========== YARDIMCI FONKSİYONLAR ==========

// Duruma göre renk döndür
uint16_t getPhaseColor() {
  switch (programState) {
    case STATE_DELAY: return TFT_ORANGE;
    case STATE_RAMP: return TFT_YELLOW;
    case STATE_STEP: 
      return settings.timedMode ? TFT_GREEN : TFT_CYAN;
    case STATE_COMPLETED: return TFT_MAGENTA;
    case STATE_AUTOTUNE: return TFT_PURPLE;
    default: return TFT_PURPLE;
  }
}

// Duruma göre metin döndür
const char* getPhaseDisplayText() {
  switch (programState) {
    case STATE_DELAY: return "BEKLEMEDE";
    case STATE_RAMP: return "RAMPA";
    case STATE_STEP: 
      return settings.timedMode ? "CALISIYOR" : "SURESIZ";
    case STATE_COMPLETED: return "TAMAMLANDI";
    case STATE_AUTOTUNE: return "AUTOTUNE";
    default: return "HAZIR";
  }
}

// ========== MEVCUT FONKSİYONLAR ==========

unsigned long calculateRemainingHours() {
  if (!isRunning) return 0;
  unsigned long remaining = 0;
  unsigned long now = millis();
  if (programState == STATE_DELAY) {
    remaining = (delayEndTime > now) ? (delayEndTime - now) / 1000 : 0;
  } else if (programState == STATE_RAMP) {
    remaining = (rampEndTime > now) ? (rampEndTime - now) / 1000 : 0;
  } else if (programState == STATE_STEP && settings.timedMode) {
    remaining = (stepEndTime > now) ? (stepEndTime - now) / 1000 : 0;
  }
  return remaining / 3600;
}

unsigned long calculateRemainingMinutes() {
  if (!isRunning) return 0;
  unsigned long remaining = 0;
  unsigned long now = millis();
  if (programState == STATE_DELAY) {
    remaining = (delayEndTime > now) ? (delayEndTime - now) / 1000 : 0;
  } else if (programState == STATE_RAMP) {
    remaining = (rampEndTime > now) ? (rampEndTime - now) / 1000 : 0;
  } else if (programState == STATE_STEP && settings.timedMode) {
    remaining = (stepEndTime > now) ? (stepEndTime - now) / 1000 : 0;
  }
  return (remaining % 3600) / 60;
}

unsigned long calculateRemainingSeconds() {
  if (!isRunning) return 0;
  unsigned long remaining = 0;
  unsigned long now = millis();
  if (programState == STATE_DELAY) {
    remaining = (delayEndTime > now) ? (delayEndTime - now) / 1000 : 0;
  } else if (programState == STATE_RAMP) {
    remaining = (rampEndTime > now) ? (rampEndTime - now) / 1000 : 0;
  } else if (programState == STATE_STEP && settings.timedMode) {
    remaining = (stepEndTime > now) ? (stepEndTime - now) / 1000 : 0;
  }
  return remaining % 60;
}

String getCurrentPhaseName() {
  switch (programState) {
    case STATE_DELAY: return "DELAY";
    case STATE_RAMP: return "RAMP";
    case STATE_STEP: return settings.timedMode ? "STEP" : "SURESIZ";
    case STATE_AUTOTUNE: return "AUTOTUNE";
    case STATE_COMPLETED: return "COMPLETED";
    default: return "IDLE";
  }
}

volatile bool powerFailureDetected = false;
bool powerFailureHold = false;

void IRAM_ATTR powerFailureISR() {
  powerFailureDetected = true;
}

void saveLastStateToEEPROM() {
  ProgramStateData state;
  state.isRunning = isRunning;
  state.programState = programState;
  state.currentStepIndex = currentStepIndex;
  state.programRepeatCounter = programRepeatCounter;
  state.stepStartTime = stepStartTime;
  state.currentTemperature = temperature;
  state.targetTemperature = pidSetpoint;
  if (programState == STATE_DELAY) {
    state.remainingTimeSec = (delayEndTime > millis()) ? (delayEndTime - millis()) / 1000 : 0;
  } else if (programState == STATE_RAMP) {
    state.remainingTimeSec = (rampEndTime > millis()) ? (rampEndTime - millis()) / 1000 : 0;
  } else if (programState == STATE_STEP && settings.timedMode) {
    state.remainingTimeSec = (stepEndTime > millis()) ? (stepEndTime - millis()) / 1000 : 0;
  } else {
    state.remainingTimeSec = 0;
  }
  if (writeRecord(EEPROM_LAST_STATE_ADDR, STATE_MAGIC, &state, sizeof(state))) {
    Serial.println("Son durum EEPROM'a kaydedildi");
  } else {
    Serial.println("HATA: Son durum EEPROM'a kaydedilemedi");
  }
}

void loadLastStateFromEEPROM() {
  ProgramStateData state = {};
  if (!readRecord(EEPROM_LAST_STATE_ADDR, STATE_MAGIC, &state, sizeof(state))) {
    isRunning = false;
    programState = STATE_IDLE;
    pidControlActive = false;
    digitalWrite(SSR_PIN, LOW);
    Serial.println("Geçerli çalışma durumu bulunamadı; cihaz güvenli şekilde durdu");
    return;
  }

  bool validState = state.programState >= STATE_IDLE && state.programState <= STATE_COMPLETED;
  validState = validState && state.currentStepIndex >= 0 && state.currentStepIndex < stepCount;
  validState = validState && state.programRepeatCounter >= 0;
  validState = validState && isfinite(state.targetTemperature);
  if (!state.isRunning || !validState || stepCount == 0) {
    isRunning = false;
    programState = STATE_IDLE;
    pidControlActive = false;
    digitalWrite(SSR_PIN, LOW);
    Serial.println("Kayıtlı program çalışmıyor veya durum geçersiz; güvenli duruş");
    return;
  }

  isRunning = state.isRunning;
  programState = state.programState;
  currentStepIndex = state.currentStepIndex;
  programRepeatCounter = state.programRepeatCounter;
  stepStartTime = millis();
  pidSetpoint = state.targetTemperature;
  unsigned long now = millis();
  if (programState == STATE_DELAY) {
    delayEndTime = now + (state.remainingTimeSec * 1000UL);
    pidControlActive = false;
    digitalWrite(SSR_PIN, LOW);
  } else if (programState == STATE_RAMP) {
    rampStartTemp = temperature;
    currentTargetTemp = temperature;
    rampEndTime = now + (state.remainingTimeSec * 1000UL);
    pidSetpoint = currentTargetTemp;
    pidControlActive = true;
  } else if (programState == STATE_STEP) {
    if (settings.timedMode) {
      stepEndTime = now + (state.remainingTimeSec * 1000UL);
    } else {
      stepEndTime = ULONG_MAX;
    }
    pidSetpoint = steps[currentStepIndex].sicaklik;
    pidControlActive = true;
  } else {
    isRunning = false;
    programState = STATE_IDLE;
    pidControlActive = false;
    digitalWrite(SSR_PIN, LOW);
  }
  Serial.println("Son durum EEPROM'dan yüklendi");
}

void showQRCode() {
  showingQRCode = true;
  qrCodeDisplayTime = millis();
  tft.fillScreen(TFT_BLACK);
  if (WiFi.status() == WL_CONNECTED || wifiSettings.mode == 2) {
    String qrContent = "http://" + WiFi.localIP().toString();
    qrcode.init();
    tft.fillScreen(TFT_WHITE);
    qrcode.create(qrContent);
    tft.setTextColor(TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 10);
    tft.println("IP: " + WiFi.localIP().toString());
  } else {
    tft.setTextColor(TFT_RED);
    tft.setTextSize(1);
    tft.setCursor(10, 100);
    tft.println("WiFi baglantisi yok!");
    showingQRCode = false;
  }
}

const char **getCurrentSubMenu() {
  switch (currentMainMenuSelection) {
    case 1: return programSubMenuItems;
    case 2: return settingsSubMenuItems;
    case 3: return alarmSubMenuItems;
    default: return NULL;
  }
}

int getCurrentSubMenuSize() {
  switch (currentMainMenuSelection) {
    case 1: return sizeof(programSubMenuItems) / sizeof(programSubMenuItems[0]);
    case 2: return sizeof(settingsSubMenuItems) / sizeof(settingsSubMenuItems[0]);
    case 3: return sizeof(alarmSubMenuItems) / sizeof(alarmSubMenuItems[0]);
    default: return 0;
  }
}

void handleSubMenuSelection() {
  switch (currentMainMenuSelection) {
    case 1:
      if (currentSubMenuSelection == 2) {
        toggleProgramState();
      }
      break;
    case 2:
      break;
    case 3:
      if (currentSubMenuSelection == 2) {
        settings.alarmEnabled = !settings.alarmEnabled;
        saveSettingsToEEPROM();
      }
      break;
  }
  exitMenu();
}

void showMenu() {
  showingQRCode = false;
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextFont(1);
  if (currentMenuLevel == 0) {
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 10);
    tft.println("ANA MENU");
    for (int i = 0; i < MENU_ITEMS; i++) {
      if (i == currentMainMenuSelection) {
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
      } else {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
      }
      tft.setCursor(20, 40 + i * 30);
      tft.println(mainMenuItems[i]);
    }
  } else {
    const char *title = "";
    switch (currentMainMenuSelection) {
      case 1: title = "PROGRAM"; break;
      case 2: title = "AYARLAR"; break;
      case 3: title = "ALARM"; break;
    }
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 10);
    tft.println(title);
    const char **subItems = getCurrentSubMenu();
    int subMenuSize = getCurrentSubMenuSize();
    for (int i = 0; i < subMenuSize; i++) {
      if (i == currentSubMenuSelection) {
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
      } else {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
      }
      tft.setCursor(20, 40 + i * 30);
      tft.println(subItems[i]);
    }
  }
}

void exitMenu() {
  inMenu = false;
  currentMenuLevel = 0;
  currentMainMenuSelection = 0;
  currentSubMenuSelection = 0;
  tft.fillScreen(TFT_BLACK);
  displayTemperatureBig(temperature);
}

int getSubMenuCount() {
  switch (currentMainMenuSelection) {
    case 1: return sizeof(programSubMenuItems) / sizeof(programSubMenuItems[0]);
    case 2: return sizeof(settingsSubMenuItems) / sizeof(settingsSubMenuItems[0]);
    case 3: return sizeof(alarmSubMenuItems) / sizeof(alarmSubMenuItems[0]);
    default: return 0;
  }
}

void handleMenuNavigation() {
  unsigned long now = millis();
  bool reading1 = digitalRead(BUTTON1_PIN);
  if (reading1 != button1PrevState) {
    lastDebounceTime1 = now;
  }
  if ((now - lastDebounceTime1) > debounceDelay) {
    if (reading1 != button1State) {
      button1State = reading1;
      if (button1State == LOW) {
        button1PressStartTime = now;
        button1LongPress = false;
        menuEnterTime = now;
      } else {
        if (!button1LongPress && (now - button1PressStartTime) < longPressTime) {
          menuEnterTime = now;
          if (inMenu) {
            if (currentMenuLevel == 0) {
              switch (currentMainMenuSelection) {
                case 0:
                  exitMenu();
                  break;
                case 1:
                case 2:
                case 3:
                  currentMenuLevel = 1;
                  currentSubMenuSelection = 0;
                  showMenu();
                  break;
                case 4:
                  showQRCode();
                  break;
              }
            } else {
              if (currentSubMenuSelection == 0) {
                currentMenuLevel = 0;
                showMenu();
              } else {
                switch (currentMainMenuSelection) {
                  case 1:
                    if (currentSubMenuSelection == 2) {
                      toggleProgramState();
                    }
                    break;
                  case 2:
                    break;
                  case 3:
                    if (currentSubMenuSelection == 2) {
                      settings.alarmEnabled = !settings.alarmEnabled;
                      saveSettingsToEEPROM();
                    }
                    break;
                }
              }
            }
          } else {
            inMenu = true;
            currentMenuLevel = 0;
            currentMainMenuSelection = 0;
            showMenu();
          }
        }
      }
    }
  }
  if (button1State == LOW && !button1LongPress && (now - button1PressStartTime) >= longPressTime) {
    button1LongPress = true;
    menuEnterTime = now;
    inMenu = !inMenu;
    if (inMenu) {
      currentMenuLevel = 0;
      currentMainMenuSelection = 0;
      currentSubMenuSelection = 0;
      showMenu();
    } else {
      exitMenu();
    }
  }
  button1PrevState = reading1;

  bool reading3 = digitalRead(BUTTON3_PIN);
  if (reading3 != button3PrevState) {
    lastDebounceTime3 = now;
  }
  if ((now - lastDebounceTime3) > debounceDelay) {
    if (reading3 != button3State) {
      button3State = reading3;
      if (button3State == LOW && inMenu) {
        menuEnterTime = now;
        if (currentMenuLevel == 0) {
          currentMainMenuSelection = (currentMainMenuSelection - 1 + MENU_ITEMS) % MENU_ITEMS;
        } else {
          int subMenuCount = getSubMenuCount();
          currentSubMenuSelection = (currentSubMenuSelection - 1 + subMenuCount) % subMenuCount;
        }
        showMenu();
      }
    }
  }
  button3PrevState = reading3;

  bool reading2 = digitalRead(BUTTON2_PIN);
  if (reading2 != button2PrevState) {
    lastDebounceTime2 = now;
  }
  if ((now - lastDebounceTime2) > debounceDelay) {
    if (reading2 != button2State) {
      button2State = reading2;
      if (button2State == LOW && inMenu) {
        menuEnterTime = now;
        if (currentMenuLevel == 0) {
          currentMainMenuSelection = (currentMainMenuSelection + 1) % MENU_ITEMS;
        } else {
          int subMenuCount = getSubMenuCount();
          currentSubMenuSelection = (currentSubMenuSelection + 1) % subMenuCount;
        }
        showMenu();
      }
    }
  }
  button2PrevState = reading2;

  bool reading4 = digitalRead(BUTTON4_PIN);
  if (reading4 != button4PrevState) {
    lastDebounceTime4 = now;
  }
  if ((now - lastDebounceTime4) > debounceDelay) {
    if (reading4 != button4State) {
      button4State = reading4;
      if (button4State == LOW && !inMenu) {
        toggleProgramState();
      }
    }
  }
  button4PrevState = reading4;

  if (inMenu && (now - menuEnterTime > menuTimeout)) {
    exitMenu();
  }
}

float mvToTemperature_PT2WIRE(float mV) {
  float Uupt = 2428187;
  float Uopt = 3921975;
  float Rupt = 99.0;
  float Ropt = 221.1;
  long Umesspt = adcRaw;
  float Rx1pt = ((((Ropt - Rupt) / (Uopt - Uupt)) * (Umesspt - Uupt)) + Rupt);
  float t1pt = -247.29 + 2.3992 * Rx1pt + 6.3962 * pow(10, -4) * pow(Rx1pt, 2) + 1.0241 * pow(10, -6) * pow(Rx1pt, 3);
  return t1pt;
}

float mvToTemperature_PT3WIRE(float mV) {
  float Uupt = 1217719;
  float Uopt = 2724300;
  float Rupt = 98.8;
  float Ropt = 220.9;
  long Umesspt = adcRaw;
  float Rx1pt = ((((Ropt - Rupt) / (Uopt - Uupt)) * (Umesspt - Uupt)) + Rupt);
  float t1pt = -247.29 + 2.3992 * Rx1pt + 6.3962 * pow(10, -4) * pow(Rx1pt, 2) + 1.0241 * pow(10, -6) * pow(Rx1pt, 3);
  return t1pt;
}

float mvToTemperature_PT4WIRE(float mV) {
  float Uupt = 1217719;
  float Uopt = 2724300;
  float Rupt = 98.8;
  float Ropt = 220.9;
  long Umesspt = adcRaw;
  float Rx1pt = ((((Ropt - Rupt) / (Uopt - Uupt)) * (Umesspt - Uupt)) + Rupt);
  float t1pt = -247.29 + 2.3992 * Rx1pt + 6.3962 * pow(10, -4) * pow(Rx1pt, 2) + 1.0241 * pow(10, -6) * pow(Rx1pt, 3);
  return t1pt;
}

double mvToTemperature_K(double mV) {
  return interpolateTemp(kTypeTable, kTypeTableSize, mV);
}

double mvToTemperature_J(double mV) {
  return interpolateTemp(jTypeTable, jTypeTableSize, mV);
}

double mvToTemperature_S(double mV) {
  return interpolateTemp(sTypeTable, sTypeTableSize, mV);
}

double mvToTemperature(char type, double mV) {
  switch (type) {
    case 'K': return mvToTemperature_K(mV);
    case 'J': return mvToTemperature_J(mV);
    case 'S': return mvToTemperature_S(mV);
    case '2': return mvToTemperature_PT2WIRE(mV);
    case '3': return mvToTemperature_PT3WIRE(mV);
    case '4': return mvToTemperature_PT4WIRE(mV);
    default: return mvToTemperature_PT2WIRE(mV);
  }
}

void resetADS1220() {
  digitalWrite(ADS_CS, LOW);
  adsSPI.beginTransaction(adsSPISettings);
  adsSPI.transfer(0x06);
  adsSPI.endTransaction();
  digitalWrite(ADS_CS, HIGH);
  delay(500);
}

void writeRegister(uint8_t reg, uint8_t value) {
  digitalWrite(ADS_CS, LOW);
  adsSPI.beginTransaction(adsSPISettings);
  adsSPI.transfer(0x40 | (reg << 2));
  adsSPI.transfer(value);
  adsSPI.endTransaction();
  digitalWrite(ADS_CS, HIGH);
  delay(1);
}

void configureADS1220DiffAVDD_AVSS() {
  uint8_t reg0 = 0b11010001;
  uint8_t reg1 = 0b00001100;
  uint8_t reg2 = 0b00010000;
  uint8_t reg3 = 0b00000000;
  writeRegister(ADS1220_REG0, reg0);
  writeRegister(ADS1220_REG1, reg1);
  writeRegister(ADS1220_REG2, reg2);
  writeRegister(ADS1220_REG3, reg3);
}

void configureADS1220DiffAIN1_AIN2() {
  uint8_t reg0 = 0b00111000;
  uint8_t reg1 = 0b00000100;
  uint8_t reg2 = 0b00010000;
  uint8_t reg3 = 0b00000000;
  writeRegister(ADS1220_REG0, reg0);
  writeRegister(ADS1220_REG1, reg1);
  writeRegister(ADS1220_REG2, reg2);
  writeRegister(ADS1220_REG3, reg3);
}

void configureADS1220InternalTemp() {
  uint8_t reg0 = 0b00000000;
  uint8_t reg1 = 0b00000110;
  uint8_t reg2 = 0b00010000;
  uint8_t reg3 = 0b00000000;
  writeRegister(ADS1220_REG0, reg0);
  writeRegister(ADS1220_REG1, reg1);
  writeRegister(ADS1220_REG2, reg2);
  writeRegister(ADS1220_REG3, reg3);
}

void configureADS1220Pt2wire() {
  pinMode(41, OUTPUT);
  digitalWrite(41, LOW);
  uint8_t reg0 = 0b00110010;
  uint8_t reg1 = 0b00010100;
  uint8_t reg2 = 0b00001111;
  uint8_t reg3 = 0b01000000;
  writeRegister(ADS1220_REG0, reg0);
  writeRegister(ADS1220_REG1, reg1);
  writeRegister(ADS1220_REG2, reg2);
  writeRegister(ADS1220_REG3, reg3);
}

void configureADS1220Pt3wire() {
  uint8_t reg0 = 0b00110010;
  uint8_t reg1 = 0b00010100;
  uint8_t reg2 = 0b00001111;
  uint8_t reg3 = 0b00100000;
  writeRegister(ADS1220_REG0, reg0);
  writeRegister(ADS1220_REG1, reg1);
  writeRegister(ADS1220_REG2, reg2);
  writeRegister(ADS1220_REG3, reg3);
}

void configureADS1220Pt4wire() {
  uint8_t reg0 = 0b00000001;
  uint8_t reg1 = 0b00000100;
  uint8_t reg2 = 0b00010100;
  uint8_t reg3 = 0b00000000;
  writeRegister(ADS1220_REG0, reg0);
  writeRegister(ADS1220_REG1, reg1);
  writeRegister(ADS1220_REG2, reg2);
  writeRegister(ADS1220_REG3, reg3);
}

void startContinuousConversion() {
  digitalWrite(ADS_CS, LOW);
  adsSPI.beginTransaction(adsSPISettings);
  adsSPI.transfer(0x08);
  adsSPI.endTransaction();
  digitalWrite(ADS_CS, HIGH);
}

int32_t readADS1220Data() {
  digitalWrite(ADS_CS, LOW);
  adsSPI.beginTransaction(adsSPISettings);
  adsSPI.transfer(0x10);
  uint8_t b1 = adsSPI.transfer(0xFF);
  uint8_t b2 = adsSPI.transfer(0xFF);
  uint8_t b3 = adsSPI.transfer(0xFF);
  adsSPI.endTransaction();
  digitalWrite(ADS_CS, HIGH);
  int32_t value = (b1 << 16) | (b2 << 8) | b3;
  if (value & 0x00800000) value |= 0xFF000000;
  return value;
}

int16_t readADS1220Temp14Bit() {
  digitalWrite(ADS_CS, LOW);
  adsSPI.beginTransaction(adsSPISettings);
  adsSPI.transfer(0x10);
  uint8_t b1 = adsSPI.transfer(0xFF);
  uint8_t b2 = adsSPI.transfer(0xFF);
  uint8_t b3 = adsSPI.transfer(0xFF);
  adsSPI.endTransaction();
  digitalWrite(ADS_CS, HIGH);
  int32_t raw24 = (b1 << 16) | (b2 << 8) | b3;
  if (raw24 & 0x00800000) raw24 |= 0xFF000000;
  return (raw24 >> 10);
}

double convert14BitToTemp(int16_t temp14) {
  return temp14 * 0.03125;
}

double applyFilter(double newSample) {
  mvHistory[sampleIndex] = newSample;
  sampleIndex = (sampleIndex + 1) % FILTER_SAMPLES;
  double sum = 0;
  for (int i = 0; i < FILTER_SAMPLES; i++) {
    sum += mvHistory[i];
  }
  return sum / FILTER_SAMPLES;
}

bool waitForEEPROMReady() {
  unsigned long start = millis();
  do {
    Wire.beginTransmission(EEPROM_I2C_ADDR);
    if (Wire.endTransmission() == 0) return true;
    delay(1);
  } while (millis() - start < EEPROM_WRITE_TIMEOUT_MS);
  return false;
}

bool writeEEPROMBlock(uint16_t address, const uint8_t *data, size_t size) {
  if (data == nullptr || size == 0) return false;
  if ((uint32_t)address + size > EEPROM_SIZE) return false;

  size_t bytesWritten = 0;
  while (bytesWritten < size) {
    uint16_t currentAddr = address + bytesWritten;
    uint16_t pageEndAddr = (currentAddr / EEPROM_PAGE_SIZE + 1) * EEPROM_PAGE_SIZE - 1;
    size_t pageSpace = pageEndAddr - currentAddr + 1;
    size_t bytesToWrite = min(size - bytesWritten, pageSpace);
    bytesToWrite = min(bytesToWrite, (size_t)EEPROM_I2C_DATA_CHUNK);
    Wire.beginTransmission(EEPROM_I2C_ADDR);
    Wire.write(currentAddr >> 8);
    Wire.write(currentAddr & 0xFF);
    for (size_t i = 0; i < bytesToWrite; i++) {
      if (Wire.write(data[bytesWritten + i]) != 1) return false;
    }
    if (Wire.endTransmission() != 0) return false;
    if (!waitForEEPROMReady()) return false;
    bytesWritten += bytesToWrite;
  }
  return true;
}

bool readEEPROMBlock(uint16_t address, uint8_t *data, size_t size) {
  if (data == nullptr || size == 0) return false;
  if ((uint32_t)address + size > EEPROM_SIZE) return false;

  size_t bytesRead = 0;
  while (bytesRead < size) {
    uint16_t currentAddr = address + bytesRead;
    size_t bytesToRead = min(size - bytesRead, (size_t)32);
    Wire.beginTransmission(EEPROM_I2C_ADDR);
    Wire.write(currentAddr >> 8);
    Wire.write(currentAddr & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    size_t received = Wire.requestFrom((uint8_t)EEPROM_I2C_ADDR, (uint8_t)bytesToRead);
    if (received != bytesToRead) return false;
    for (size_t i = 0; i < bytesToRead; i++) {
      if (!Wire.available()) return false;
      data[bytesRead++] = Wire.read();
    }
  }
  return true;
}

uint32_t calculateCRC32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < size; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

bool writeRecord(uint16_t address, uint32_t magic, const void *payload, size_t payloadSize) {
  if (payload == nullptr || payloadSize == 0 || payloadSize > UINT16_MAX) return false;
  if ((uint32_t)address + sizeof(StorageHeader) + payloadSize > EEPROM_SIZE) return false;

  StorageHeader header;
  header.magic = magic;
  header.version = STORAGE_VERSION;
  header.payloadSize = (uint16_t)payloadSize;
  header.crc32 = calculateCRC32((const uint8_t *)payload, payloadSize);

  if (!writeEEPROMBlock(address + sizeof(StorageHeader), (const uint8_t *)payload, payloadSize)) return false;
  return writeEEPROMBlock(address, (const uint8_t *)&header, sizeof(header));
}

bool readRecord(uint16_t address, uint32_t magic, void *payload, size_t payloadSize) {
  if (payload == nullptr || payloadSize == 0 || payloadSize > UINT16_MAX) return false;
  StorageHeader header = {};
  if (!readEEPROMBlock(address, (uint8_t *)&header, sizeof(header))) return false;
  if (header.magic != magic || header.version != STORAGE_VERSION || header.payloadSize != payloadSize) return false;
  if (!readEEPROMBlock(address + sizeof(StorageHeader), (uint8_t *)payload, payloadSize)) return false;
  return header.crc32 == calculateCRC32((const uint8_t *)payload, payloadSize);
}

void saveWiFiSettingsToEEPROM() {
  if (writeRecord(EEPROM_WIFI_SETTINGS_ADDR, WIFI_MAGIC, &wifiSettings, sizeof(wifiSettings))) {
    Serial.println("WiFi ayarları EEPROM'a kaydedildi");
  } else {
    Serial.println("HATA: WiFi ayarları EEPROM'a kaydedilemedi");
  }
}

void loadWiFiSettingsFromEEPROM() {
  WiFiSettings loaded;
  if (!readRecord(EEPROM_WIFI_SETTINGS_ADDR, WIFI_MAGIC, &loaded, sizeof(loaded)) || loaded.mode > 2) {
    Serial.println("Geçerli WiFi kaydı yok; varsayılan ayarlar kullanılacak");
    return;
  }
  loaded.ssid[sizeof(loaded.ssid) - 1] = '\0';
  loaded.password[sizeof(loaded.password) - 1] = '\0';
  loaded.ap_ssid[sizeof(loaded.ap_ssid) - 1] = '\0';
  loaded.ap_password[sizeof(loaded.ap_password) - 1] = '\0';
  wifiSettings = loaded;
  Serial.println("WiFi ayarları EEPROM'dan yüklendi");
}

void saveStepsToEEPROM() {
  uint8_t buffer[STEPS_PAYLOAD_SIZE] = {};
  uint16_t addr = 0;
  buffer[addr++] = stepCount;
  buffer[addr++] = isRunning ? 1 : 0;
  buffer[addr++] = settings.timedMode ? 1 : 0;
  buffer[addr++] = programRepeatCount & 0xFF;
  buffer[addr++] = (programRepeatCount >> 8) & 0xFF;
  buffer[addr++] = settings.delayHours;
  buffer[addr++] = settings.delayMinutes;
  for (int i = 0; i < stepCount; i++) {
    memcpy(&buffer[addr], &steps[i].sicaklik, sizeof(steps[i].sicaklik));
    addr += sizeof(steps[i].sicaklik);
    buffer[addr++] = steps[i].rampSaat;
    buffer[addr++] = steps[i].rampDakika;
    buffer[addr++] = steps[i].calismaSaat;
    buffer[addr++] = steps[i].calismaDakika;
    memcpy(&buffer[addr], &steps[i].dakikaDegisim, sizeof(steps[i].dakikaDegisim));
    addr += sizeof(steps[i].dakikaDegisim);
    buffer[addr++] = steps[i].dakikaCheck ? 1 : 0;
  }
  if (writeRecord(EEPROM_STEPS_START, STEPS_MAGIC, buffer, sizeof(buffer))) {
    Serial.println("Stepler EEPROM'a kaydedildi");
  } else {
    Serial.println("HATA: Stepler EEPROM'a kaydedilemedi");
  }
}

void loadStepsFromEEPROM() {
  uint8_t buffer[STEPS_PAYLOAD_SIZE] = {};
  if (!readRecord(EEPROM_STEPS_START, STEPS_MAGIC, buffer, sizeof(buffer))) {
    stepCount = 0;
    isRunning = false;
    Serial.println("Geçerli step kaydı yok; step listesi boş başlatıldı");
    return;
  }

  uint16_t addr = 0;
  stepCount = buffer[addr++];
  addr++;  // Çalışma durumu ayrı ve CRC korumalı durum kaydından yüklenir.
  settings.timedMode = buffer[addr++] == 1;
  uint8_t lowByte = buffer[addr++];
  uint8_t highByte = buffer[addr++];
  programRepeatCount = (highByte << 8) | lowByte;
  settings.delayHours = buffer[addr++];
  settings.delayMinutes = buffer[addr++];

  if (stepCount > MAX_STEPS || programRepeatCount == 0 || programRepeatCount > 10000 || settings.delayMinutes > 59) {
    stepCount = 0;
    programRepeatCount = 1;
    isRunning = false;
    Serial.println("Step kaydı sınır kontrollerini geçemedi; kayıt reddedildi");
    return;
  }

  for (int i = 0; i < stepCount; i++) {
    memcpy(&steps[i].sicaklik, &buffer[addr], sizeof(steps[i].sicaklik));
    addr += sizeof(steps[i].sicaklik);
    steps[i].rampSaat = buffer[addr++];
    steps[i].rampDakika = buffer[addr++];
    steps[i].calismaSaat = buffer[addr++];
    steps[i].calismaDakika = buffer[addr++];
    memcpy(&steps[i].dakikaDegisim, &buffer[addr], sizeof(steps[i].dakikaDegisim));
    addr += sizeof(steps[i].dakikaDegisim);
    steps[i].dakikaCheck = buffer[addr++] == 1;
    bool validStep = isfinite(steps[i].sicaklik) && isfinite(steps[i].dakikaDegisim);
    validStep = validStep && steps[i].rampDakika <= 59 && steps[i].calismaDakika <= 59;
    validStep = validStep && (!steps[i].dakikaCheck || steps[i].dakikaDegisim > 0.0);
    if (!validStep) {
      stepCount = 0;
      isRunning = false;
      Serial.println("Geçersiz step verisi bulundu; kayıt reddedildi");
      return;
    }
  }
  isRunning = false;
  Serial.printf("EEPROM'dan %d step yüklendi\n", stepCount);
}

void loadSettingsFromEEPROM() {
  Settings loaded;
  bool valid = readRecord(EEPROM_SETTINGS_ADDR, SETTINGS_MAGIC, &loaded, sizeof(loaded));
  valid = valid && isfinite(loaded.kalibrasyon) && isfinite(loaded.sensorFaktor);
  valid = valid && isfinite(loaded.kp) && isfinite(loaded.ki) && isfinite(loaded.kd);
  valid = valid && isfinite(loaded.autotuneTemp) && isfinite(loaded.alarmTemperature);
  valid = valid && loaded.sensorType <= SENSOR_PT100_4WIRE && loaded.delayMinutes <= 59;
  if (valid) {
    settings = loaded;
  } else {
    Serial.println("Geçerli ayar kaydı yok; güvenli varsayılanlar kullanılacak");
  }
  switch (settings.sensorType) {
    case SENSOR_K_TYPE: sensorType = 'K'; break;
    case SENSOR_J_TYPE: sensorType = 'J'; break;
    case SENSOR_S_TYPE: sensorType = 'S'; break;
    case SENSOR_PT100_2WIRE: sensorType = '2'; break;
    case SENSOR_PT100_3WIRE: sensorType = '3'; break;
    case SENSOR_PT100_4WIRE: sensorType = '4'; break;
    default: sensorType = '2'; break;
  }
}

void saveSettingsToEEPROM() {
  if (!writeRecord(EEPROM_SETTINGS_ADDR, SETTINGS_MAGIC, &settings, sizeof(settings))) {
    Serial.println("HATA: Ayarlar EEPROM'a kaydedilemedi");
  }
}

void computePID() {
  if (!pidControlActive || !isRunning || programState == STATE_DELAY ||
      programState == STATE_IDLE || programState == STATE_COMPLETED) {
    digitalWrite(SSR_PIN, LOW);
    return;
  }
  unsigned long now = millis();
  unsigned long timeChange = now - pidLastTime;
  if (timeChange >= 1000) {
    pidInput = temperature;
    double error = pidSetpoint - pidInput;
    if (error < 0) {
      digitalWrite(SSR_PIN, LOW);
      pidLastTime = now;
      return;
    }
    pidErrorSum += error * timeChange;
    if (pidErrorSum > 1000) pidErrorSum = 1000;
    if (pidErrorSum < -1000) pidErrorSum = -1000;
    double dError = (error - pidLastError) / timeChange;
    pidOutput = settings.kp * error + settings.ki * pidErrorSum + settings.kd * dError;
    if (pidOutput > 1000) pidOutput = 1000;
    if (pidOutput < 0) pidOutput = 0;
    if (pidOutput > 500) {
      digitalWrite(SSR_PIN, HIGH);
    } else {
      digitalWrite(SSR_PIN, LOW);
    }
    pidLastError = error;
    pidLastTime = now;
    Serial.printf("PID: SP=%.1f, PV=%.1f, Error=%.1f, Output=%.1f\n",
                  pidSetpoint, pidInput, error, pidOutput);
  }
}

void runAutotune() {
  if (!autotuneRunning) return;
  unsigned long now = millis();
  unsigned long elapsedTime = now - autotuneStartTime;
  int totalEstimatedTime = 600000;
  autotuneProgress = min(100, (int)((elapsedTime * 100) / totalEstimatedTime));
  float peakToPeak = 0;
  float ku = 0;
  float tu = 0;
  switch (autotuneStep) {
    case 0:
      autotuneStatus = "Isınma başlatılıyor...";
      digitalWrite(SSR_PIN, HIGH);
      peakCount = 0;
      autotuneStep = 1;
      break;
    case 1:
      autotuneStatus = "Hedef sıcaklığa ısınıyor...";
      if (temperature >= autotuneTargetTemp) {
        digitalWrite(SSR_PIN, LOW);
        autotuneStep = 2;
        peakTemps[peakCount++] = temperature;
      }
      break;
    case 2:
      autotuneStatus = "Sıcaklık düşüyor...";
      if (temperature < autotuneTargetTemp) {
        digitalWrite(SSR_PIN, HIGH);
        autotuneStep = 3;
      }
      break;
    case 3:
      autotuneStatus = "Sıcaklık yükseliyor...";
      if (temperature >= autotuneTargetTemp) {
        digitalWrite(SSR_PIN, LOW);
        autotuneStep = 2;
        peakTemps[peakCount++] = temperature;
        if (peakCount >= autotuneMaxCycles * 2) {
          autotuneStep = 4;
        }
      }
      break;
    case 4:
      autotuneStatus = "PID parametreleri hesaplanıyor...";
      peakToPeak = 0;
      for (int i = 1; i < peakCount; i++) {
        peakToPeak += abs(peakTemps[i] - peakTemps[i - 1]);
      }
      peakToPeak /= (peakCount - 1);
      ku = 4.0 * 100.0 / (3.14159 * peakToPeak);
      tu = (elapsedTime / (peakCount / 2)) / 1000.0;
      autotune_kp = 0.6 * ku;
      autotune_ki = 1.2 * ku / tu;
      autotune_kd = 0.075 * ku * tu;
      settings.kp = autotune_kp;
      settings.ki = autotune_ki;
      settings.kd = autotune_kd;
      saveSettingsToEEPROM();
      autotuneStep = 5;
      break;
    case 5:
      autotuneStatus = "Tamamlandı";
      autotuneRunning = false;
      digitalWrite(SSR_PIN, LOW);
      pidSetpoint = autotuneSetValue;
      pidLastTime = millis();
      pidErrorSum = 0;
      pidLastError = 0;
      pidControlActive = true;
      break;
  }
}

void startAPMode() {
  WiFi.mode(WIFI_AP);
  if (wifiSettings.ap_useStaticIP) {
    IPAddress apIP(wifiSettings.ap_ip[0], wifiSettings.ap_ip[1], wifiSettings.ap_ip[2], wifiSettings.ap_ip[3]);
    IPAddress gateway(wifiSettings.ap_gateway[0], wifiSettings.ap_gateway[1], wifiSettings.ap_gateway[2], wifiSettings.ap_gateway[3]);
    IPAddress subnet(wifiSettings.ap_subnet[0], wifiSettings.ap_subnet[1], wifiSettings.ap_subnet[2], wifiSettings.ap_subnet[3]);
    WiFi.softAPConfig(apIP, gateway, subnet);
  }
  WiFi.softAP(wifiSettings.ap_ssid, wifiSettings.ap_password);
  Serial.println("AP Mode Started");
  Serial.println("AP SSID: " + String(wifiSettings.ap_ssid));
  Serial.println("AP IP: " + WiFi.softAPIP().toString());
}

void applyWiFiSettings() {
  bool buttonPressed = (digitalRead(BUTTON1_PIN) == LOW);
  switch (wifiSettings.mode) {
    case 0:
      WiFi.mode(WIFI_OFF);
      WiFi.disconnect(true);
      Serial.println("WiFi kapatıldı");
      break;
    case 1:
      {
        WiFi.mode(WIFI_STA);
        if (wifiSettings.useStaticIP) {
          IPAddress ip(wifiSettings.ip[0], wifiSettings.ip[1], wifiSettings.ip[2], wifiSettings.ip[3]);
          IPAddress gateway(wifiSettings.gateway[0], wifiSettings.gateway[1], wifiSettings.gateway[2], wifiSettings.gateway[3]);
          IPAddress subnet(wifiSettings.subnet[0], wifiSettings.subnet[1], wifiSettings.subnet[2], wifiSettings.subnet[3]);
          IPAddress dns1(wifiSettings.dns1[0], wifiSettings.dns1[1], wifiSettings.dns1[2], wifiSettings.dns1[3]);
          IPAddress dns2(wifiSettings.dns2[0], wifiSettings.dns2[1], wifiSettings.dns2[2], wifiSettings.dns2[3]);
          if (!WiFi.config(ip, gateway, subnet, dns1, dns2)) {
            Serial.println("Statik IP yapılandırma hatası!");
          } else {
            Serial.print("Statik IP: "); Serial.println(ip);
            Serial.print("Gateway: "); Serial.println(gateway);
            Serial.print("Subnet: "); Serial.println(subnet);
            Serial.print("DNS1: "); Serial.println(dns1);
            Serial.print("DNS2: "); Serial.println(dns2);
          }
        }
        WiFi.begin(wifiSettings.ssid, wifiSettings.password);
        Serial.print("WiFi'ye bağlanıyor");
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
          delay(500);
          Serial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println();
          Serial.println("WiFi bağlandı: " + WiFi.localIP().toString());
          Serial.print("DNS Sunucusu: ");
          Serial.println(WiFi.dnsIP());
        } else {
          Serial.println("\nWiFi bağlantı hatası!");
          if (buttonPressed) {
            startAPModeWithFixedIP("otosens", "12345678");
          } else {
            startAPMode();
          }
        }
        break;
      }
    case 2:
      {
        if (buttonPressed) {
          startAPModeWithFixedIP("otosens", "12345678");
        } else {
          startAPMode();
        }
        break;
      }
  }
}

void startAPModeWithFixedIP(const char *ssid, const char *password) {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  IPAddress apIP(192, 168, 100, 1);
  IPAddress gateway(192, 168, 100, 1);
  IPAddress subnet(255, 255, 255, 0);
  const bool configOk = WiFi.softAPConfig(apIP, gateway, subnet);
  const bool apStarted = WiFi.softAP(ssid, password, 6, false, 4);
  delay(500);
  Serial.printf("AP config: %s, AP start: %s\n", configOk ? "OK" : "FAILED", apStarted ? "OK" : "FAILED");
  Serial.printf("WiFi mode: %d, AP SSID: %s, AP IP: %s\n",
                WiFi.getMode(), WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
  if (!configOk || !apStarted || WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    Serial.println("HATA: ESP32 access point baslatilamadi");
    return;
  }
  Serial.println("Sabit IP ile AP Modu Başlatıldı");
}

void checkWiFiConnection() {
  if (wifiSettings.mode != 1 || forceAPMode == 1) return;
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastReconnectAttempt >= reconnectInterval) {
      lastReconnectAttempt = currentMillis;
      Serial.println("WiFi bağlantısı kesildi. Yeniden bağlanılıyor...");
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      if (wifiSettings.useStaticIP) {
        IPAddress ip(wifiSettings.ip[0], wifiSettings.ip[1], wifiSettings.ip[2], wifiSettings.ip[3]);
        IPAddress gateway(wifiSettings.gateway[0], wifiSettings.gateway[1], wifiSettings.gateway[2], wifiSettings.gateway[3]);
        IPAddress subnet(wifiSettings.subnet[0], wifiSettings.subnet[1], wifiSettings.subnet[2], wifiSettings.subnet[3]);
        IPAddress dns1(wifiSettings.dns1[0], wifiSettings.dns1[1], wifiSettings.dns1[2], wifiSettings.dns1[3]);
        IPAddress dns2(wifiSettings.dns2[0], wifiSettings.dns2[1], wifiSettings.dns2[2], wifiSettings.dns2[3]);
        if (!WiFi.config(ip, gateway, subnet, dns1, dns2)) {
          Serial.println("Statik IP yapılandırma hatası!");
        } else {
          Serial.print("Statik IP yeniden yapılandırıldı: "); Serial.println(ip);
          Serial.print("DNS1: "); Serial.println(dns1);
          Serial.print("DNS2: "); Serial.println(dns2);
        }
      }
      WiFi.begin(wifiSettings.ssid, wifiSettings.password);
      Serial.print("WiFi'ye bağlanıyor");
      WiFi.reconnect();
      unsigned long startTime = millis();
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi'ye yeniden bağlandı: " + WiFi.localIP().toString());
        Serial.print("DNS Sunucusu: ");
        Serial.println(WiFi.dnsIP());
      } else {
        Serial.println("\nWiFi'ye yeniden bağlanılamadı!");
      }
    }
  }
}

// ========== GÜNCELLENMİŞ EKRAN FONKSİYONLARI ==========

void displayTemperatureBig(float temp) {
  if (inMenu || showingQRCode) return;
  
  // Sadece sıcaklığı göster
  tft.setTextSize(1);
  tft.setFreeFont(&segment60pt7b);
  tft.setTextColor(TFT_RED);
  tft.fillRect(0, 0, 240, 110, TFT_BLACK);
  tft.setCursor(10, 100);
  if (temp >= 1000.0) {
    tft.printf("%.0f", temp);
  } else {
    tft.printf("%.1f", temp);
  }
  
  // Derece işareti
  tft.setFreeFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED);
  tft.setCursor(160, 70);
  tft.print("°C");
}

void displayTime7Segment(int hh, int mm, int sss) {
  if (showingQRCode) return;
  
  tft.setTextFont(1);
  tft.setTextSize(1);
  
  int yPos = 145;
  
  // Arka planı temizle
  tft.fillRect(0, 130, 240, 110, TFT_BLACK);
  
  if (isRunning && stepCount > 0) {
    // ===== PROGRAM ÇALIŞIYOR =====
    
    // 1. SATIR: Step ve Durum
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, yPos);
    tft.printf("Step: %d/%d", currentStepIndex + 1, stepCount);
    
    tft.setTextColor(getPhaseColor());
    tft.setCursor(140, yPos);
    tft.print(getPhaseDisplayText());
    
    // 2. SATIR: Hedef ve Kalan Süre
    yPos += 26;
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, yPos);
    double target = (programState == STATE_RAMP) ? currentTargetTemp : steps[currentStepIndex].sicaklik;
    tft.printf("Hedef: %.1f°C", target);
    
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(140, yPos);
    if (programState == STATE_STEP && !settings.timedMode) {
      tft.print("SURESIZ");
    } else {
      unsigned long remaining = 0;
      unsigned long now = millis();
      if (programState == STATE_DELAY) {
        remaining = (delayEndTime > now) ? (delayEndTime - now) / 1000 : 0;
      } else if (programState == STATE_RAMP) {
        remaining = (rampEndTime > now) ? (rampEndTime - now) / 1000 : 0;
      } else if (programState == STATE_STEP && settings.timedMode) {
        remaining = (stepEndTime > now) ? (stepEndTime - now) / 1000 : 0;
      }
      int hours = remaining / 3600;
      int minutes = (remaining % 3600) / 60;
      int seconds = remaining % 60;
      tft.printf("%02d:%02d:%02d", hours, minutes, seconds);
    }
    
    // 3. SATIR: Tekrar
    yPos += 26;
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, yPos);
    tft.printf("Tekrar: %d/%d", programRepeatCounter + 1, programRepeatCount);
    
  } else {
    // ===== PROGRAM DURAKLATILDI =====
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(40, yPos + 10);
    tft.print("DURAKLATILDI");
  }
}

// ========== DİĞER FONKSİYONLAR (DEVAMI) ==========

void printHelp() {
  Serial.println("\nKullanılabilir komutlar:");
  Serial.println("K - Sensör tipini K tipi termokupla ayarla");
  Serial.println("J - Sensör tipini J tipi termokupla ayarla");
  Serial.println("S - Sensör tipini S tipi termokupla ayarla");
  Serial.println("2 - Sensör tipini PT100/1000 2-wire ayarla");
  Serial.println("3 - Sensör tipini PT100/1000 3-wire ayarla");
  Serial.println("4 - Sensör tipini PT100/1000 4-wire ayarla");
  Serial.println("? - Bu yardım mesajını göster");
  Serial.println("-----------------------");
}

void checkSerialInput() {
  if (Serial.available() > 0) {
    char input = toupper(Serial.read());
    switch (input) {
      case 'K':
      case 'J':
      case 'S':
      case '2':
      case '3':
      case '4':
        sensorType = input;
        Serial.print("Sensör tipi ayarlandı: ");
        Serial.println(sensorType);
        for (int i = 0; i < FILTER_SAMPLES; i++) {
          mvHistory[i] = 0;
        }
        if (sensorType >= 'A' && sensorType <= 'Z') {
          configureADS1220DiffAIN1_AIN2();
        } else {
          switch (sensorType) {
            case '2': configureADS1220Pt2wire(); break;
            case '3': configureADS1220Pt3wire(); break;
            case '4': configureADS1220Pt4wire(); break;
            default: configureADS1220Pt2wire(); break;
          }
        }
        startContinuousConversion();
        delay(150);
        break;
      case '?':
        printHelp();
        break;
      default:
        Serial.println("Bilinmeyen komut. Yardım için ? yazın.");
        break;
    }
    while (Serial.available() > 0) {
      Serial.read();
    }
  }
}

// index_html, program_html, tempsettings_html, wifisettings_html - WebSocket uyumlu
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Ana Sayfa</title>
  <style>
    body {
      font-family: Arial;
      margin: 0;
      padding: 10px;
      max-width: 600px;
      margin-left: auto;
      margin-right: auto;
      box-sizing: border-box;
      width: 100%;
    }
    .header {
      text-align: center;
      padding: 20px;
      margin-bottom: 20px;
      background-color: #fff;
    }
    h1 {color: #333; margin-bottom: 5px;}
    
    .temperature-display {
      text-align: center;
      font-size: 64px;
      font-weight: bold;
      margin: 20px auto;
      padding: 20px;
      background-color: #f0f0f0;
      border-radius: 12px;
      color: #FF0000;
      width: 100%;
      max-width: 400px;
      box-sizing: border-box;
      box-shadow: 0 4px 8px rgba(0,0,0,0.15);
      position: relative;
      overflow: hidden;
    }
    .temperature-label {
      font-size: 14px;
      color: #666;
      position: absolute;
      top: 8px;
      left: 12px;
    }
    .temperature-unit {
      font-size: 36px;
      position: relative;
      top: -8px;
    }
    .menu-button {
      display: block;
      width: 100%;
      padding: 20px;
      font-size: 24px;
      margin: 10px 0;
      color: white;
      border: none;
      border-radius: 8px;
      cursor: pointer;
      text-align: center;
      text-decoration: none;
      box-sizing: border-box;
    }
    .program-btn {background-color: #4CAF50;}
    .settings-btn {background-color: #2196F3;}
    .wifi-btn {background-color: #FF9800;}
    
    @media screen and (max-width: 600px) {
      body {
        padding: 10px;
        width: 100%;
        margin: 0;
        max-width: 100%;
      }
      .menu-button {
        width: 100%;
        font-size: 20px;
        padding: 15px;
      }
      .temperature-display {
        width: 95%;
        max-width: 95%;
        font-size: 48px;
        padding: 15px 5px;
        margin: 15px auto;
      }
      .temperature-unit {
        font-size: 28px;
      }
    }
    
    @media screen and (max-width: 360px) {
      body {
        padding: 5px;
      }
      .menu-button {
        font-size: 18px;
        padding: 12px;
      }
      .temperature-display {
        width: 98%;
        max-width: 98%; 
        margin: 10px auto;
        font-size: 38px;
        padding: 10px 5px;
      }
      .temperature-unit {
        font-size: 22px;
      }
      .temperature-label {
        font-size: 12px;
      }
    }
  </style>
</head>
<body>
  <div class="header">
    <h1>Kontrol Paneli</h1>
    <p>Lütfen bir seçenek seçin</p>
  </div>
  
  <div class="temperature-display">
    <span class="temperature-label">Sıcaklık:</span>
    <div id="temperatureDisplay">--.-<span class="temperature-unit">°C</span></div>
  </div>

  <a href="/program" class="menu-button program-btn">Program Set</a>
  <a href="/tempsettings" class="menu-button settings-btn">Sıcaklık Kontrol Ayarları</a>
  <a href="/wifisettings" class="menu-button wifi-btn">WiFi Ayarları</a>
  
  <script>
    let lastTemp = null;
    let tempCheckInterval;
    
    const wsProtocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    let ws;

    function initWebSocket() {
        try {
            ws = new WebSocket(`${wsProtocol}${window.location.host}/ws`);
            
            ws.onopen = function() {
                console.log("WebSocket bağlandı!");
                ws.send("getStatus");
            };
            
            ws.onmessage = function(event) {
                try {
                    const data = JSON.parse(event.data);
                    if (data.type === "fullUpdate" || data.type === "status") {
                        if (data.currentTemp !== undefined) {
                            updateTemperatureDisplay(data.currentTemp);
                        }
                    }
                } catch (e) {
                    if (event.data === "running" || event.data === "stopped") {
                        const btn = document.getElementById('startStopBtn');
                        if (btn) {
                            const isRunning = (event.data === "running");
                            btn.textContent = isRunning ? "Durdur" : "Başlat";
                            btn.style.backgroundColor = isRunning ? "#f44336" : "#4CAF50";
                        }
                    }
                }
            };
            
            ws.onerror = function(error) {
                console.error("WebSocket hatası:", error);
            };
            
            ws.onclose = function() {
                console.log("WebSocket kapandı, 5 saniye sonra yeniden bağlanılacak.");
                setTimeout(initWebSocket, 5000);
            };
            
        } catch (e) {
            console.error("WebSocket başlatılamadı:", e);
        }
    }

    function updateTemperatureDisplay(temp) {
        const tempDisplay = document.getElementById('temperatureDisplay');
        if (tempDisplay) {
            tempDisplay.innerHTML = `${temp.toFixed(1)}<span class="temperature-unit">°C</span>`;
        }
    }

    function updateTemperature(force = false) {
        fetch('/getTemperature')
            .then(response => response.json())
            .then(data => {
                updateTemperatureDisplay(data.temperature);
                
                const btn = document.getElementById('startStopBtn');
                if (btn) {
                    if (data.isRunning !== (btn.textContent === "Durdur")) {
                        btn.textContent = data.isRunning ? "Durdur" : "Başlat";
                        btn.style.backgroundColor = data.isRunning ? "#f44336" : "#4CAF50";
                    }
                }
            })
            .catch(error => {
                console.error('AJAX güncelleme hatası:', error);
                setTimeout(() => updateTemperature(), 3000);
            });
    }
    
    initWebSocket();
    updateTemperature(true);
    tempCheckInterval = setInterval(() => updateTemperature(), 1000);
  </script>
</body>
</html>
)rawliteral";

// program_html - WebSocket uyumlu
const char program_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Program Set - Step Girişi</title>
  <style>
    * {box-sizing: border-box;}
    body {
      font-family: Arial, sans-serif; 
      margin: 0; 
      padding: 10px; 
      max-width: 1200px; 
      margin: 0 auto;
      display: flex;
      flex-direction: column;
      min-height: 100vh;
    }
    
    .header-container {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 15px;
    }
    
    .program-status {
      display: block;
      background-color: #f8f8f8;
      border-radius: 8px;
      padding: 12px;
      margin-bottom: 15px;
      box-shadow: 0 2px 4px rgba(0,0,0,0.1);
      border-left: 4px solid #4CAF50;
      width: 100%;
      max-width: 400px;
      margin: 0 auto 15px;
    }
    
    .status-grid {
      display: grid;
      grid-template-columns: 100px 1fr;
      gap: 5px;
      margin-bottom: 6px;
      align-items: center;
    }
    
    .status-label {
      font-weight: bold;
      color: #555;
      text-align: right;
    }
    
    .status-value {
      color: #333;
      padding-left: 10px;
    }
    
    .phase-delay { color: #FF9800; }
    .phase-ramp { color: #FF9800; }
    .phase-step { color: #4CAF50; }
    .phase-untimed { color: #2196F3; }
    .phase-completed { color: #9C27B0; }
    .phase-error { color: #f44336; }
    
    .temperature-display {
      text-align: center;
      font-size: 24px;
      font-weight: bold;
      margin: 0 auto 15px;
      padding: 15px;
      background-color: #f0f0f0;
      border-radius: 12px;
      color: #FF0000;
      width: 100%;
      max-width: 400px;
      box-sizing: border-box;
      box-shadow: 0 4px 8px rgba(0,0,0,0.15);
    }
    
    .table-container {
      margin-top: 5px;
    }
    
    .table-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 10px;
    }
    
    table {
      border-collapse: collapse;
      width: 100%;
    }
    
    th, td {
      border: 1px solid #ddd;
      padding: 8px;
      text-align: center;
    }
    
    th {
      background-color: #f2f2f2;
    }

    tr.selected {
      background-color: #e4f3e8;
    }

    .stepNo {
      cursor: pointer;
      font-weight: bold;
      user-select: none;
    }

    .stepNo:hover {
      background-color: #d5eadb;
    }

    tr.selected .stepNo {
      background-color: #b9ddc4;
      color: #174d2b;
    }
    
    input[type=number] {
      width: 65px;
      padding: 5px;
      font-size: 16px;
      font-weight: bold;
    }
    
    input[type=checkbox] {
      transform: scale(1.8);
    }
    
    button {
      margin-top: 10px;
      padding: 8px 16px;
      background-color: #4CAF50;
      color: white;
      border: none;
      cursor: pointer;
      border-radius: 4px;
    }
    
    button:hover {
      background-color: #45a049;
    }
    
    #startStopBtn {
      background-color: #f44336;
      width: 100%;
      padding: 12px;
    }
    
    #startStopBtn:hover {
      background-color: #d32f2f;
    }
    
    .button-container {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      margin: 15px 0;
    }
    
    .settings-row {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      margin-bottom: 15px;
    }
    
    .mode-container, .repeat-container, .delay-container {
      flex: 1;
      min-width: 200px;
      padding: 12px;
      background-color: #f8f8f8;
      border-radius: 8px;
      box-shadow: 0 1px 3px rgba(0,0,0,0.1);
    }

    .mode-container label, 
    .repeat-container label, 
    .delay-container label {
      display: block;
      font-weight: bold;
      margin-bottom: 8px;
      color: #333;
      font-size: 0.95em;
    }

    .mode-options {
      display: flex;
      gap: 8px;
    }

    .mode-options label {
      display: flex;
      align-items: center;
      gap: 4px;
      white-space: nowrap;
    }

    .delay-inputs {
      display: flex;
      gap: 5px;
      align-items: center;
      flex-wrap: nowrap;
    }

    .delay-input-group {
      display: flex;
      align-items: center;
      gap: 3px;
    }

    .delay-input-group input {
      width: 50px;
      padding: 6px;
      border: 1px solid #ddd;
      border-radius: 4px;
      text-align: center;
    }

    .delay-input-group span {
      font-size: 0.9em;
      white-space: nowrap;
    }

    .repeat-input {
      display: flex;
      align-items: center;
    }

    .repeat-input input {
      width: 60px;
      padding: 6px;
      border: 1px solid #ddd;
      border-radius: 4px;
      text-align: center;
    }
    
    .back-button {
      display: block;
      width: fit-content;
      margin: 20px auto 0;
      padding: 10px 20px;
      background-color: #2196F3;
      color: white;
      text-decoration: none;
      border-radius: 4px;
      text-align: center;
      align-self: flex-end;
      margin-top: auto;
    }
    
    .responsive-table {
      overflow-x: auto;
      width: 100%;
    }
    
    @media screen and (max-width: 768px) {
      .status-grid {
        grid-template-columns: 80px 1fr;
      }
      
      input[type=number] {
        width: 55px;
      }
      
      .button-container {
        flex-direction: column;
      }
      
      .button-container button {
        width: 100%;
      }

      .settings-row {
        flex-direction: column;
        gap: 10px;
      }
      
      .mode-container, .repeat-container, .delay-container {
        min-width: 100%;
      }

      .mode-options {
        flex-direction: row;
        gap: 10px;
      }
      
      .delay-inputs {
        flex-direction: row;
        gap: 5px;
      }
      
      .delay-input-group input {
        width: 45px;
      }
    }
    
    @media screen and (max-width: 480px) {
      body {
        font-size: 14px;
        padding: 5px;
      }
      
      .status-grid {
        grid-template-columns: 70px 1fr;
      }
      
      .mode-container, .repeat-container, .delay-container {
        padding: 10px;
      }
      
      .delay-inputs {
        flex-direction: row;
        gap: 3px;
      }
      
      .delay-input-group {
        width: auto;
      }
      
      .delay-input-group input {
        width: 40px;
      }
      
      .repeat-input {
        flex-direction: row;
        align-items: center;
      }
      
      .repeat-input input {
        width: 50px;
      }
      
      input[type="number"], input[type="radio"] {
        min-height: 36px;
      }
    }
  </style>
</head>
<body>
  <div class="header-container">
    <h2>Step Listesi</h2>
  </div>

  <div id="programStatus" class="program-status">
    <div class="status-grid">
      <div class="status-label">Step:</div>
      <div id="currentStep" class="status-value">-/-</div>
      
      <div class="status-label">Durum:</div>
      <div id="stepPhase" class="status-value">-</div>
      
      <div class="status-label">Hedef:</div>
      <div id="targetTemp" class="status-value">- °C</div>
      
      <div class="status-label">Kalan:</div>
      <div id="remainingTime" class="status-value">--:--:--</div>
      
      <div class="status-label">Tekrar:</div>
      <div id="repeatStatus" class="status-value">-/-</div>
    </div>
  </div>
  
  <div class="temperature-display">
    <div id="temperatureDisplay">--.-<span style="font-size: 0.8em;">°C</span></div>
  </div>
  
  <div class="table-container">
    <div class="responsive-table">
      <table id="stepsTable">
        <thead>
          <tr>
            <th>#</th>
            <th>Sıcaklık</th>
            <th>Rampa Saat</th>
            <th>Rampa Dakika</th>
            <th>Çalışma Saat</th>
            <th>Çalışma Dakika</th>
            <th>Dakikada Değişim</th>
            <th>Aktif</th>
            <th>Sil</th>
          </tr>
        </thead>
        <tbody></tbody>
      </table>
    </div>
  </div>

  <div class="button-container">
    <button id="addStepButton" onclick="addStep()">Yeni Step Ekle</button>
    <button onclick="clearSteps()">Tüm Stepleri Sil</button>
    <button onclick="saveSteps()">Kaydet</button>
  </div>

  <button id="startStopBtn" onclick="toggleRun()">Başlat</button>
  
  <div class="settings-row">
    <div class="mode-container" style="flex: 2; min-width: 220px;">
      <label>Çalışma Modu:</label>
      <div class="mode-options" style="display: flex; gap: 10px;">
        <label>
          <input type="radio" name="operationMode" id="timedMode" value="1" checked>
          <span>Süreli Mod</span>
        </label>
        <label>
          <input type="radio" name="operationMode" id="untimedMode" value="0">
          <span>Süresiz Mod</span>
        </label>
      </div>
    </div>
    
    <div class="repeat-container" style="flex: 1; min-width: 120px;">
      <label>Tekrar Sayısı:</label>
      <div class="repeat-input">
        <input type="number" id="repeatCount" min="1" value="1" style="width: 60px;">
      </div>
    </div>
    
    <div class="delay-container" style="flex: 1; min-width: 180px;">
      <label>Gecikme:</label>
      <div class="delay-inputs" style="display: flex; gap: 5px; align-items: center;">
        <div class="delay-input-group">
          <input type="number" id="delayHours" min="0" max="23" value="0" style="width: 45px;">
          <span>sa</span>
        </div>
        <div class="delay-input-group">
          <input type="number" id="delayMinutes" min="0" max="59" value="0" style="width: 45px;">
          <span>dk</span>
        </div>
      </div>
    </div>
  </div>
  
  <a href="/" class="back-button">Ana Sayfaya Dön</a>

  <script>
    let steps = [];
    let selectedIndex = -1;
    let isRunning = false;
    let repeatCount = 1;
    let timedMode = true;
    let delayHours = 0;
    let delayMinutes = 0;
    
    const wsProtocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    const ws = new WebSocket(`${wsProtocol}${window.location.host}/ws`);
    
    ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        if (data.type === "fullUpdate") {
          updateUIFromFullUpdate(data);
        } else if (data.type === "status") {
          updateProgramStatus(data);
        }
      } catch (e) {
        console.error('WebSocket message error:', e);
      }
    };
    
    ws.onopen = function() {
      console.log("WebSocket bağlandı!");
    };
    
    ws.onerror = function(error) {
      console.error("WebSocket hatası:", error);
    };
    
    ws.onclose = function() {
      console.log("WebSocket kapandı, 5 saniye sonra yeniden bağlanılacak.");
      setTimeout(() => {
        const newWs = new WebSocket(`${wsProtocol}${window.location.host}/ws`);
      }, 5000);
    };
    
    function updateUIFromFullUpdate(data) {
      isRunning = data.isRunning;
      updateStartStopBtn();
      
      if (isRunning) {
        document.getElementById('programStatus').style.display = 'block';
        
        document.getElementById('currentStep').textContent = 
          `${data.currentStepIndex + 1}/${data.stepCount}`;
        
        document.getElementById('targetTemp').textContent = 
          `${data.targetTemp?.toFixed(1) || '--'}°C`;
        
        const statusElement = document.getElementById('stepPhase');
        statusElement.textContent = getPhaseDisplayName(data.phase);
        statusElement.className = 'status-value ' + getPhaseClass(data.phase);
        
        document.getElementById('repeatStatus').textContent =
          `${data.programRepeatCounter + 1}/${data.programRepeatCount}`;
        
        const remainingTimeElement = document.getElementById('remainingTime');
        const formatTimeUnit = (unit) => String(unit).padStart(2, '0');
        
        if (data.phase === "DELAY" || data.phase === "RAMP") {
          remainingTimeElement.textContent =
            `${formatTimeUnit(data.remainingHours)}:` +
            `${formatTimeUnit(data.remainingMinutes)}:` +
            `${formatTimeUnit(data.remainingSeconds)}`;
        } 
        else if (data.phase === "STEP" && data.timedMode) {
          remainingTimeElement.textContent =
            `${formatTimeUnit(data.remainingHours)}:` +
            `${formatTimeUnit(data.remainingMinutes)}:` +
            `${formatTimeUnit(data.remainingSeconds)}`;
        }
        else if (data.phase === "STEP" && !data.timedMode) {
          remainingTimeElement.textContent = "SÜRESİZ";
        }
        else {
          remainingTimeElement.textContent = "--:--:--";
        }
      } else {
        document.getElementById('programStatus').style.display = 'none';
      }
    }
    
    function getPhaseDisplayName(phase) {
      switch(phase) {
        case "DELAY": return "BEKLEMEDE";
        case "RAMP": return "RAMPA";
        case "STEP": return timedMode ? "ÇALIŞIYOR" : "SÜRESİZ";
        case "COMPLETED": return "TAMAMLANDI";
        default: return "HAZIR";
      }
    }
    
    function getPhaseClass(phase) {
      switch(phase) {
        case "DELAY": return "phase-delay";
        case "RAMP": return "phase-ramp";
        case "STEP": return timedMode ? "phase-step" : "phase-untimed";
        case "COMPLETED": return "phase-completed";
        default: return "";
      }
    }
    
    function updateProgramStatus(data) {
      if (data.isRunning) {
        document.getElementById('programStatus').style.display = 'block';
        
        if (data.currentStepIndex !== undefined) {
          document.getElementById('currentStep').textContent = 
            `${data.currentStepIndex + 1}/${data.stepCount}`;
        }
        
        if (data.targetTemp !== undefined) {
          document.getElementById('targetTemp').textContent = 
            `${data.targetTemp?.toFixed(1) || '--'}°C`;
        }
        
        if (data.phase !== undefined) {
          const statusElement = document.getElementById('stepPhase');
          statusElement.textContent = getPhaseDisplayName(data.phase);
          statusElement.className = 'status-value ' + getPhaseClass(data.phase);
        }
        
        if (data.programRepeatCounter !== undefined && data.programRepeatCount !== undefined) {
          document.getElementById('repeatStatus').textContent =
            `${data.programRepeatCounter + 1}/${data.programRepeatCount}`;
        }
        
        if (data.remainingHours !== undefined && data.remainingMinutes !== undefined && data.remainingSeconds !== undefined) {
          const remainingTimeElement = document.getElementById('remainingTime');
          const formatTimeUnit = (unit) => String(unit).padStart(2, '0');
          
          if (data.phase === "DELAY" || data.phase === "RAMP") {
            remainingTimeElement.textContent =
              `${formatTimeUnit(data.remainingHours)}:` +
              `${formatTimeUnit(data.remainingMinutes)}:` +
              `${formatTimeUnit(data.remainingSeconds)}`;
          } 
          else if (data.phase === "STEP" && data.timedMode) {
            remainingTimeElement.textContent =
              `${formatTimeUnit(data.remainingHours)}:` +
              `${formatTimeUnit(data.remainingMinutes)}:` +
              `${formatTimeUnit(data.remainingSeconds)}`;
          }
          else if (data.phase === "STEP" && !data.timedMode) {
            remainingTimeElement.textContent = "SÜRESİZ";
          }
          else {
            remainingTimeElement.textContent = "--:--:--";
          }
        }
      } else {
        document.getElementById('programStatus').style.display = 'none';
      }
    }
    
    const statusUpdateInterval = setInterval(fetchProgramStatus, 1000);
    
    function fetchProgramStatus() {
      fetch('/getProgramStatus')
        .then(response => {
          if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
          return response.json();
        })
        .then(data => {
          updateProgramStatus(data);
        })
        .catch(error => {
          console.error('Error fetching program status:', error);
          document.getElementById('stepPhase').textContent = "HATA";
          document.getElementById('stepPhase').className = 'status-value phase-error';
        });
    }

    function loadSteps() {
      fetch('/getSteps')
        .then(resp => resp.json())
        .then(data => {
          steps = data;
          if(data.length > 0 && data[data.length-1].hasOwnProperty("isRunning")){
            isRunning = data[data.length-1].isRunning;
            if(data[data.length-1].hasOwnProperty("repeatCount")){
              repeatCount = data[data.length-1].repeatCount;
              document.getElementById("repeatCount").value = repeatCount;
            }
            if(data[data.length-1].hasOwnProperty("timedMode")){
              timedMode = data[data.length-1].timedMode;
              document.querySelector(`input[name="operationMode"][value="${timedMode ? '1' : '0'}"]`).checked = true;
            }
            if(data[data.length-1].hasOwnProperty("delayHours")){
              delayHours = data[data.length-1].delayHours;
              delayMinutes = data[data.length-1].delayMinutes;
              document.getElementById("delayHours").value = delayHours;
              document.getElementById("delayMinutes").value = delayMinutes;
            }
            data.pop();
          }
          renderTable();
          updateStartStopBtn();
          
          if(isRunning) {
            fetchProgramStatus();
          }
        });
    }

    function renderTable() {
      const tbody = document.querySelector("#stepsTable tbody");
      tbody.innerHTML = "";
      const addStepButton = document.getElementById("addStepButton");
      addStepButton.textContent = selectedIndex >= 0
        ? `Seçilen #${selectedIndex + 1} Sonrasına Step Ekle`
        : "Yeni Step Ekle";
      steps.forEach((step, i) => {
        const tr = document.createElement("tr");
        if (i === selectedIndex) tr.classList.add("selected");

        const saatDakikaFormat = (num) => num.toString().padStart(2, '0');
        
        tr.innerHTML = `
          <td class="stepNo">${i+1}</td>
          <td><input type="number" step="0.1" value="${step.sicaklik.toFixed(1)}" min="0" oninput="updateStep(${i}, 'sicaklik', this.value)"></td>
          <td><input type="number" min="0" max="23" value="${saatDakikaFormat(step.rampSaat)}" oninput="updateStep(${i}, 'rampSaat', this.value)"></td>
          <td><input type="number" min="0" max="59" value="${saatDakikaFormat(step.rampDakika)}" oninput="updateStep(${i}, 'rampDakika', this.value)"></td>
          <td><input type="number" min="0" max="23" value="${saatDakikaFormat(step.calismaSaat)}" oninput="updateStep(${i}, 'calismaSaat', this.value)"></td>
          <td><input type="number" min="0" max="59" value="${saatDakikaFormat(step.calismaDakika)}" oninput="updateStep(${i}, 'calismaDakika', this.value)"></td>
          <td><input type="number" step="0.01" value="${step.dakikaDegisim.toFixed(2)}" min="0" oninput="updateStep(${i}, 'dakikaDegisim', this.value)"></td>
          <td><input type="checkbox" ${step.dakikaCheck ? 'checked' : ''} onchange="updateStep(${i}, 'dakikaCheck', this.checked)"></td>
          <td><button onclick="deleteStep(${i})">Sil</button></td>
        `;

        const stepNumber = tr.querySelector(".stepNo");
        stepNumber.title = `#${i + 1} satırını seç; yeni step bu satırın arkasına eklenir`;
        stepNumber.setAttribute("role", "button");
        stepNumber.tabIndex = 0;
        const selectStep = () => {
          if(selectedIndex === i) {
            selectedIndex = -1;
          } else {
            selectedIndex = i;
          }
          renderTable();
        };
        stepNumber.addEventListener("click", selectStep);
        stepNumber.addEventListener("keydown", (event) => {
          if(event.key === "Enter" || event.key === " ") {
            event.preventDefault();
            selectStep();
          }
        });

        tbody.appendChild(tr);
      });
    }

    function updateStep(index, key, value) {
      if(key === 'dakikaCheck') {
        steps[index][key] = value;
      } else if(key === 'sicaklik' || key === 'dakikaDegisim') {
        steps[index][key] = parseFloat(value) || 0;
        
        if(key === 'sicaklik' && steps[index][key] === Math.floor(steps[index][key])) {
          steps[index][key] = parseFloat(steps[index][key].toFixed(1));
        }
        if(key === 'dakikaDegisim' && steps[index][key] === Math.floor(steps[index][key])) {
          steps[index][key] = parseFloat(steps[index][key].toFixed(2));
        }
      } else {
        steps[index][key] = parseInt(value) || 0;
      }
    }

    const MAX_STEPS = 10;
    
    function addStep() {
      if(steps.length >= MAX_STEPS) {
        alert(`Maksimum step sayısına (${MAX_STEPS}) ulaşıldı!`);
        return;
      }
      
      let newStep = {
        sicaklik: 0.0,
        rampSaat: 0,
        rampDakika: 0,
        calismaSaat: 0,
        calismaDakika: 0,
        dakikaDegisim: 0.00,
        dakikaCheck: false
      };

      if(selectedIndex >= 0) {
        steps.splice(selectedIndex + 1, 0, newStep);
        selectedIndex = selectedIndex + 1;
      } else {
        steps.push(newStep);
        selectedIndex = steps.length - 1;
      }
      renderTable();
    }
    
    function deleteStep(index) {
      steps.splice(index, 1);
      if(selectedIndex === index) selectedIndex = -1;
      else if(selectedIndex > index) selectedIndex--;
      renderTable();
    }

    function clearSteps() {
      if(confirm("Tüm stepleri silmek istediğinize emin misiniz?")) {
        fetch('/clearSteps', {method:'POST'})
        .then(() => {
          steps = [];
          selectedIndex = -1;
          renderTable();
        });
      }
    }
    
    function saveSteps() {
      repeatCount = parseInt(document.getElementById("repeatCount").value);
      if (isNaN(repeatCount) || repeatCount < 1) repeatCount = 1;
      
      timedMode = document.getElementById("timedMode").checked;
      delayHours = parseInt(document.getElementById("delayHours").value) || 0;
      delayMinutes = parseInt(document.getElementById("delayMinutes").value) || 0;
      
      const stepsToSave = steps.map(step => ({...step}));
      
      const dataToSave = [...stepsToSave, {
        repeatCount: repeatCount, 
        isRunning: isRunning,
        timedMode: timedMode,
        delayHours: delayHours,
        delayMinutes: delayMinutes
      }];
      
      fetch('/saveSteps', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(dataToSave)
      })
      .then(response => {
        if (!response.ok) {
          throw new Error('Network response was not ok');
        }
        return response.text();
      })
      .then(() => {
        alert("Kaydedildi!");
      })
      .catch(error => {
        console.error('Error saving steps:', error);
        alert("Kaydetme hatası!");
      });
    }

    function toggleRun() {
      const newState = !isRunning;
      const btn = document.getElementById("startStopBtn");
      
      btn.disabled = true;
      btn.textContent = newState ? "Başlatılıyor..." : "Durduruluyor...";
      
      fetch('/toggleRun', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: 'state=' + (newState ? '1' : '0')
      })
      .then(response => {
        if (!response.ok) {
            throw new Error('Network response was not ok');
        }
        return response.json();
      })
      .then(data => {
        if (data.success) {
            isRunning = data.isRunning;
            updateStartStopBtn();
            
            fetchProgramStatus();
            updateTemperature(true);
        }
      })
      .catch(error => {
        console.error('Error:', error);
        alert("İşlem sırasında hata oluştu!");
      })
      .finally(() => {
        btn.disabled = false;
        updateStartStopBtn();
      });
    }

    function updateStartStopBtn() {
      const btn = document.getElementById("startStopBtn");
      btn.textContent = isRunning ? "Durdur" : "Başlat";
      btn.style.backgroundColor = isRunning ? "#f44336" : "#4CAF50";
      
      if (isRunning) {
        btn.style.boxShadow = "0 0 10px #f44336";
      } else {
        btn.style.boxShadow = "0 0 10px #4CAF50";
      }
    }
    
    function updateTemperature(force = false) {
      fetch('/getTemperature')
        .then(response => response.json())
        .then(data => {
          document.getElementById('temperatureDisplay').innerHTML = 
            `${data.temperature.toFixed(1)}<span style="font-size: 0.8em;">°C</span>`;
            
          if (data.isRunning !== isRunning) {
            isRunning = data.isRunning;
            updateStartStopBtn();
            fetchProgramStatus();
          }
        })
        .catch(error => console.error('Güncelleme hatası:', error));
    }

    loadSteps();
    updateTemperature();
    
    setInterval(updateTemperature, 1000);
    
    window.addEventListener('beforeunload', () => {
      clearInterval(statusUpdateInterval);
      if (ws.readyState === WebSocket.OPEN) {
        ws.close();
      }
    });
  </script>
</body>
</html>
)rawliteral";

// tempsettings_html
const char tempsettings_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Sıcaklık Kontrol Ayarları</title>
  <style>
    body {font-family: Arial; margin: 0; padding: 10px; max-width: 600px; margin: 0 auto;}
    .container {
      padding: 30px;
      margin: 20px auto;
      background-color: #f8f8f8;
      border-radius: 8px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
      width: 100%;
      max-width: 500px;
      box-sizing: border-box;
    }
    h1 {color: #333; text-align: center; margin-bottom: 20px;}
    h2 {color: #555; font-size: 18px; margin-top: 30px; margin-bottom: 15px; border-bottom: 1px solid #ddd; padding-bottom: 5px;}
    .form-group {
      margin-bottom: 15px;
    }
    .form-group label {
      display: inline-block;
      width: 150px;
      font-weight: bold;
    }
    .form-group input, .form-group select {
      width: 150px;
      padding: 8px;
      border: 1px solid #ddd;
      border-radius: 4px;
      font-size: 16px;
      max-width: 100%;
    }
    .button-container {
      text-align: center;
      margin-top: 25px;
    }
    .save-button {
      background-color: #4CAF50;
      color: white;
      border: none;
      padding: 10px 30px;
      font-size: 16px;
      border-radius: 4px;
      cursor: pointer;
      margin-bottom: 10px;
    }
    .save-button:hover {
      background-color: #45a049;
    }
    .autotune-button {
      background-color: #FF9800;
      color: white;
      border: none;
      padding: 10px 30px;
      font-size: 16px;
      border-radius: 4px;
      cursor: pointer;
      margin-top: 15px;
      width: 100%;
    }
    .autotune-button:hover {
      background-color: #F57C00;
    }
    .autotune-button:disabled {
      background-color: #ccc;
      cursor: not-allowed;
    }
    .back-button {
      display: block;
      width: fit-content;
      margin: 20px auto;
      padding: 10px 20px;
      background-color: #2196F3;
      color: white;
      text-decoration: none;
      border-radius: 4px;
      text-align: center;
    }
    .notification {
      display: none;
      position: fixed;
      bottom: 20px;
      left: 50%;
      transform: translateX(-50%);
      padding: 15px;
      background-color: #333;
      color: white;
      border-radius: 5px;
      box-shadow: 0 4px 8px rgba(0,0,0,0.2);
      z-index: 1000;
      min-width: 200px;
      text-align: center;
    }
    .section {
      background-color: white;
      padding: 15px;
      border-radius: 5px;
      margin-bottom: 20px;
      box-shadow: 0 1px 3px rgba(0,0,0,0.1);
    }
    .status-indicator {
      display: inline-block;
      width: 12px;
      height: 12px;
      border-radius: 50%;
      margin-right: 5px;
      background-color: #ccc;
    }
    .status-active {
      background-color: #4CAF50;
    }
    .status-text {
      font-size: 14px;
      color: #666;
    }
    .progress-container {
      width: 100%;
      background-color: #f1f1f1;
      border-radius: 4px;
      margin-top: 10px;
      display: none;
    }
    .progress-bar {
      height: 20px;
      border-radius: 4px;
      background-color: #4CAF50;
      width: 0%;
      text-align: center;
      line-height: 20px;
      color: white;
      font-size: 12px;
    }
    .result-container {
      margin-top: 15px;
      padding: 10px;
      background-color: #f9f9f9;
      border-radius: 4px;
      border-left: 4px solid #2196F3;
      display: none;
    }
    .alarm-settings {
      background-color: white;
      padding: 15px;
      border-radius: 5px;
      margin-bottom: 20px;
      box-shadow: 0 1px 3px rgba(0,0,0,0.1);
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Sıcaklık Kontrol Ayarları</h1>
    
    <div class="temperature-display" style="width: 100%; max-width: 100%; margin: 15px auto;">
      <span class="temperature-label">Sıcaklık:</span>
      <div id="temperatureDisplay">--.-<span class="temperature-unit">°C</span></div>
    </div>
    
    <div class="section">
      <h2>Sensör Ayarları</h2>
      <div class="form-group">
        <label for="sensorType">Sensör Tipi:</label>
        <select id="sensorType" name="sensorType">
          <option value="0">K Tipi Termokupl</option>
          <option value="1">J Tipi Termokupl</option>
          <option value="2">S Tipi Termokupl</option>
          <option value="3">PT100 (2-Wire)</option>
          <option value="4">PT100 (3-Wire)</option>
          <option value="5">PT100 (4-Wire)</option>
        </select>
      </div>
    </div>
    
    <div class="alarm-settings">
      <h2>Alarm Ayarları</h2>
      <div class="form-group">
        <label for="alarmTemp">Alarm Sıcaklığı (°C):</label>
        <input type="number" id="alarmTemp" step="0.1" value="100.0" min="0" max="300">
      </div>
      <div class="form-group">
        <label>
          <input type="checkbox" id="alarmEnabled">
          Alarm Aktif
        </label>
      </div>
    </div>
    
    <div class="section">
      <h2>PID Parametreleri</h2>
      <div class="form-group">
        <label for="kalibrasyon">Kalibrasyon:</label>
        <input type="number" id="kalibrasyon" step="0.001" value="0.000">
      </div>
      
      <div class="form-group">
        <label for="sensorFaktor">Sensör Faktör:</label>
        <input type="number" id="sensorFaktor" step="0.001" value="0.000">
      </div>
      
      <div class="form-group">
        <label for="kp">Kp:</label>
        <input type="number" id="kp" step="0.001" value="0.000">
      </div>
      
      <div class="form-group">
        <label for="ki">Ki:</label>
        <input type="number" id="ki" step="0.001" value="0.000">
      </div>
      
      <div class="form-group">
        <label for="kd">Kd:</label>
        <input type="number" id="kd" step="0.001" value="0.000">
      </div>
      
      <div class="button-container">
        <button class="save-button" onclick="saveSettings()">Kaydet</button>
      </div>
    </div>
    
    <div class="section">
      <h2>Autotune</h2>
      <div class="form-group">
        <label for="autotuneTemp">Hedef Sıcaklık:</label>
        <input type="number" id="autotuneTemp" step="0.1" value="100.0" min="20" max="300">
      </div>
      
      <div style="margin-top: 10px;">
        <span class="status-indicator" id="autotuneStatus"></span>
        <span class="status-text" id="autotuneStatusText">Hazır</span>
      </div>
      
      <div class="progress-container" id="progressContainer">
        <div class="progress-bar" id="progressBar">0%</div>
      </div>
      
      <div class="result-container" id="resultContainer">
        <div id="autotuneResults"></div>
      </div>
      
      <div style="display: flex; gap: 10px; margin-top: 15px;">
        <button class="autotune-button" id="autotuneButton" onclick="startAutotune()">Autotune Başlat</button>
        <button class="autotune-button" id="stopAutotuneButton" onclick="stopAutotune()" style="background-color: #f44336; display: none;">Autotune Durdur</button>
      </div>
    </div>
  </div>
  
  <a href="/" class="back-button">Ana Sayfaya Dön</a>
  
  <div id="notification" class="notification"></div>
  
  <script>
    window.addEventListener('load', function() {
      loadSettings();
      checkAutotuneStatus();
    });
    
    function loadSettings() {
      fetch('/getSettings')
        .then(response => response.json())
        .then(data => {
          console.log("Received settings:", data);
          document.getElementById('kalibrasyon').value = data.kalibrasyon.toFixed(3);
          document.getElementById('sensorFaktor').value = data.sensorFaktor.toFixed(3);
          document.getElementById('kp').value = data.kp.toFixed(3);
          document.getElementById('ki').value = data.ki.toFixed(3);
          document.getElementById('kd').value = data.kd.toFixed(3);
          document.getElementById('sensorType').value = data.sensorType;
          document.getElementById('autotuneTemp').value = data.autotuneTemp.toFixed(1);
          document.getElementById('alarmTemp').value = data.alarmTemperature.toFixed(1);
          document.getElementById('alarmEnabled').checked = data.alarmEnabled;
        })
        .catch(error => {
          console.error('Error loading settings:', error);
          showNotification('Ayarlar yüklenirken hata oluştu!');
        });
    }

    function saveSettings() {
      const settings = {
        kalibrasyon: parseFloat(document.getElementById('kalibrasyon').value) || 0,
        sensorFaktor: parseFloat(document.getElementById('sensorFaktor').value) || 0,
        kp: parseFloat(document.getElementById('kp').value) || 0,
        ki: parseFloat(document.getElementById('ki').value) || 0,
        kd: parseFloat(document.getElementById('kd').value) || 0,
        sensorType: parseInt(document.getElementById('sensorType').value),
        autotuneTemp: parseFloat(document.getElementById('autotuneTemp').value) || 100.0,
        alarmTemperature: parseFloat(document.getElementById('alarmTemp').value) || 100.0,
        alarmEnabled: document.getElementById('alarmEnabled').checked
      };
      
      if (settings.sensorType < 0 || settings.sensorType > 5) {
        settings.sensorType = 3;
      }
      
      fetch('/saveSettings', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(settings)
      })
      .then(response => {
        if (response.ok) {
          showNotification('Ayarlar kaydedildi!');
        } else {
          showNotification('Kaydetme hatası!');
        }
      })
      .catch(error => {
        console.error('Error saving settings:', error);
        showNotification('Bağlantı hatası!');
      });
    }

    function startAutotune() {
      const targetTemp = parseFloat(document.getElementById('autotuneTemp').value);
      
      if (isNaN(targetTemp) || targetTemp < 20 || targetTemp > 300) {
        showNotification('Lütfen 20-300°C arasında geçerli bir sıcaklık değeri girin!');
        return;
      }
      
      document.getElementById('autotuneButton').disabled = true;
      document.getElementById('stopAutotuneButton').style.display = 'block';
      
      document.getElementById('autotuneStatus').classList.add('status-active');
      document.getElementById('autotuneStatusText').textContent = 'Autotune çalışıyor...';
      
      document.getElementById('progressContainer').style.display = 'block';
      
      document.getElementById('resultContainer').style.display = 'none';
      
      fetch('/startAutotune', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          targetTemp: targetTemp
        })
      })
      .then(response => {
        if (response.ok) {
          showNotification('Autotune başlatıldı!');
          pollAutotuneStatus();
        } else {
          showNotification('Autotune başlatma hatası!');
          resetAutotuneUI();
        }
      })
      .catch(error => {
        console.error('Error starting autotune:', error);
        showNotification('Bağlantı hatası!');
        resetAutotuneUI();
      });
    }
    
    function stopAutotune() {
      fetch('/stopAutotune', {
        method: 'POST'
      })
      .then(response => {
        if (response.ok) {
          showNotification('Autotune durduruldu!');
          resetAutotuneUI();
        } else {
          showNotification('Autotune durdurma hatası!');
        }
      })
      .catch(error => {
        console.error('Error stopping autotune:', error);
        showNotification('Bağlantı hatası!');
      });
    }
    
    function pollAutotuneStatus() {
      const statusInterval = setInterval(() => {
        fetch('/getAutotuneStatus')
          .then(response => response.json())
          .then(data => {
            const progressBar = document.getElementById('progressBar');
            progressBar.style.width = data.progress + '%';
            progressBar.textContent = data.progress + '%';
            
            document.getElementById('autotuneStatusText').textContent = data.status;
            
            if (data.complete || !data.running) {
              clearInterval(statusInterval);
              
              if (data.complete) {
                const resultContainer = document.getElementById('resultContainer');
                resultContainer.style.display = 'block';
                
                const resultsDiv = document.getElementById('autotuneResults');
                resultsDiv.innerHTML = `
                  <p><strong>Autotune Sonuçları:</strong></p>
                  <p>Kp: ${data.kp.toFixed(3)}</p>
                  <p>Ki: ${data.ki.toFixed(3)}</p>
                  <p>Kd: ${data.kd.toFixed(3)}</p>
                `;
                
                document.getElementById('kp').value = data.kp.toFixed(3);
                document.getElementById('ki').value = data.ki.toFixed(3);
                document.getElementById('kd').value = data.kd.toFixed(3);
                
                showNotification('Autotune tamamlandı!');
              }
              
              resetAutotuneUI();
            }
          })
          .catch(error => {
            console.error('Error checking autotune status:', error);
            clearInterval(statusInterval);
            resetAutotuneUI();
          });
      }, 2000);
    }
    
    function checkAutotuneStatus() {
      fetch('/getAutotuneStatus')
        .then(response => response.json())
        .then(data => {
          if (data.running) {
            document.getElementById('autotuneButton').disabled = true;
            document.getElementById('stopAutotuneButton').style.display = 'block';
            document.getElementById('autotuneStatus').classList.add('status-active');
            document.getElementById('autotuneStatusText').textContent = data.status;
            document.getElementById('progressContainer').style.display = 'block';
            
            const progressBar = document.getElementById('progressBar');
            progressBar.style.width = data.progress + '%';
            progressBar.textContent = data.progress + '%';
            
            pollAutotuneStatus();
          }
        })
        .catch(error => {
          console.error('Error checking initial autotune status:', error);
        });
    }
    
    function resetAutotuneUI() {
      document.getElementById('autotuneButton').disabled = false;
      document.getElementById('stopAutotuneButton').style.display = 'none';
      document.getElementById('autotuneStatus').classList.remove('status-active');
      document.getElementById('autotuneStatusText').textContent = 'Hazır';
    }
    
    function showNotification(message) {
      const notification = document.getElementById('notification');
      notification.textContent = message;
      notification.style.display = 'block';
      
      setTimeout(() => {
        notification.style.display = 'none';
      }, 2000);
    }
    
    function updateTemperature(force = false) {
      fetch('/getTemperature')
        .then(response => response.json())
        .then(data => {
          if (force || data.changed || lastTemp === null || Math.abs(data.temperature - lastTemp) >= 0.1) {
            const tempDisplay = document.getElementById('temperatureDisplay');
            tempDisplay.innerHTML = `${data.temperature.toFixed(1)}<span class="temperature-unit">°C</span>`;
            lastTemp = data.temperature;
          }
        })
        .catch(error => {
          console.error('Error fetching temperature:', error);
        });
    }
    
    const style = document.createElement('style');
    style.textContent = `
      @keyframes pulse {
        0% { transform: scale(1); }
        50% { transform: scale(1.05); }
        100% { transform: scale(1); }
      }
    `;
    document.head.appendChild(style);
    
    let lastTemp = null;
    updateTemperature(true);
    const tempCheckInterval = setInterval(() => updateTemperature(), 1000);
  </script>
</body>
</html>
)rawliteral";

// wifisettings_html
const char wifisettings_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>WiFi Ayarları</title>
  <style>
    body {font-family: Arial; margin: 0; padding: 10px; max-width: 600px; margin: 0 auto;}
    .container {
      padding: 30px;
      margin: 20px auto;
      background-color: #f8f8f8;
      border-radius: 8px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
      width: 100%;
      max-width: 500px;
      box-sizing: border-box;
    }
    h1 {color: #333; text-align: center; margin-bottom: 20px;}
    .form-group {
      margin-bottom: 15px;
    }
    .form-group label {
      display: inline-block;
      width: 200px;
      font-weight: bold;
      margin-bottom: 5px;
    }
    .form-group input, .form-group select {
      width: 100%;
      padding: 8px;
      border: 1px solid #ddd;
      border-radius: 4px;
      font-size: 16px;
      box-sizing: border-box;
    }
    .ip-group input {
      width: 60px;
      display: inline-block;
      margin-right: 5px;
      text-align: center;
    }
    .ip-group span {
      margin: 0 2px;
    }
    .button-container {
      text-align: center;
      margin-top: 25px;
    }
    .save-button {
      background-color: #4CAF50;
      color: white;
      border: none;
      padding: 10px 30px;
      font-size: 16px;
      border-radius: 4px;
      cursor: pointer;
      margin-bottom: 10px;
    }
    .save-button:hover {
      background-color: #45a049;
    }
    .back-button {
      display: block;
      width: fit-content;
      margin: 20px auto;
      padding: 10px 20px;
      background-color: #2196F3;
      color: white;
      text-decoration: none;
      border-radius: 4px;
      text-align: center;
    }
    .notification {
      display: none;
      position: fixed;
      bottom: 20px;
      left: 50%;
      transform: translateX(-50%);
      padding: 15px;
      background-color: #333;
      color: white;
      border-radius: 5px;
      box-shadow: 0 4px 8px rgba(0,0,0,0.2);
      z-index: 1000;
      min-width: 200px;
      text-align: center;
    }
    .section {
      background-color: white;
      padding: 15px;
      border-radius: 5px;
      margin-bottom: 20px;
      box-shadow: 0 1px 3px rgba(0,0,0,0.1);
    }
    .section-title {
      font-size: 18px;
      font-weight: bold;
      margin-bottom: 15px;
      color: #333;
      border-bottom: 1px solid #eee;
      padding-bottom: 5px;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>WiFi Ayarları</h1>
    
    <form id="wifiForm">
      <div class="section">
        <div class="section-title">Çalışma Modu</div>
        <div class="form-group">
          <label for="wifiMode">WiFi Modu:</label>
          <select id="wifiMode" name="mode" onchange="updateModeFields()">
            <option value="0">Kapalı</option>
            <option value="1">STA (İstemci) Modu</option>
            <option value="2">AP (Erişim Noktası) Modu</option>
          </select>
        </div>
      </div>

      <div id="staSettings" class="section">
        <div class="section-title">STA Modu Ayarları</div>
        <div class="form-group">
          <label for="staSsid">WiFi SSID:</label>
          <input type="text" id="staSsid" name="ssid" placeholder="WiFi ağı adı">
        </div>
        
        <div class="form-group">
          <label for="staPassword">WiFi Şifre:</label>
          <input type="text" id="staPassword" name="password" placeholder="WiFi şifresi">
        </div>
        
        <div class="form-group">
          <label>
            <input type="checkbox" id="useStatic" name="useStatic" onchange="toggleStaticIP()">
            Statik IP Kullan
          </label>
        </div>
        
        <div id="staticIpSettings" style="display:none;">
          <div class="form-group">
            <label for="ipAddress">IP Adresi:</label>
            <div class="ip-group">
              <input type="number" min="0" max="255" name="ip0" class="ip-part"> .
              <input type="number" min="0" max="255" name="ip1" class="ip-part"> .
              <input type="number" min="0" max="255" name="ip2" class="ip-part"> .
              <input type="number" min="0" max="255" name="ip3" class="ip-part">
            </div>
          </div>
          
          <div class="form-group">
            <label for="gateway">Gateway:</label>
            <div class="ip-group">
              <input type="number" min="0" max="255" name="gateway0" class="ip-part"> .
              <input type="number" min="0" max="255" name="gateway1" class="ip-part"> .
              <input type="number" min="0" max="255" name="gateway2" class="ip-part"> .
              <input type="number" min="0" max="255" name="gateway3" class="ip-part">
            </div>
          </div>
          
          <div class="form-group">
            <label for="subnet">Subnet Mask:</label>
            <div class="ip-group">
              <input type="number" min="0" max="255" name="subnet0" class="ip-part"> .
              <input type="number" min="0" max="255" name="subnet1" class="ip-part"> .
              <input type="number" min="0" max="255" name="subnet2" class="ip-part"> .
              <input type="number" min="0" max="255" name="subnet3" class="ip-part">
            </div>
          </div>
          
          <div class="form-group">
            <label for="dns1">DNS 1:</label>
            <div class="ip-group">
              <input type="number" min="0" max="255" name="dns10" class="ip-part"> .
              <input type="number" min="0" max="255" name="dns11" class="ip-part"> .
              <input type="number" min="0" max="255" name="dns12" class="ip-part"> .
              <input type="number" min="0" max="255" name="dns13" class="ip-part">
            </div>
          </div>
          
          <div class="form-group">
            <label for="dns2">DNS 2:</label>
            <div class="ip-group">
              <input type="number" min="0" max="255" name="dns20" class="ip-part"> .
              <input type="number" min="0" max="255" name="dns21" class="ip-part"> .
              <input type="number" min="0" max="255" name="dns22" class="ip-part"> .
              <input type="number" min="0" max="255" name="dns23" class="ip-part">
            </div>
          </div>
        </div>
      </div>

      <div id="apSettings" class="section">
        <div class="section-title">AP Modu Ayarları</div>
        <div class="form-group">
          <label for="apSsid">AP SSID:</label>
          <input type="text" id="apSsid" name="ap_ssid" placeholder="Erişim noktası adı">
        </div>
        
        <div class="form-group">
          <label for="apPassword">AP Şifre:</label>
          <input type="text" id="apPassword" name="ap_password" placeholder="Erişim noktası şifresi">
        </div>
        
        <div class="form-group">
          <label>
            <input type="checkbox" id="ap_useStatic" name="ap_useStatic" onchange="toggleAPStaticIP()">
            Statik IP Kullan
          </label>
        </div>
        
        <div id="apStaticIpSettings" style="display:none;">
          <div class="form-group">
            <label for="ap_ipAddress">IP Adresi:</label>
            <div class="ip-group">
              <input type="number" min="0" max="255" name="ap_ip0" class="ip-part"> .
              <input type="number" min="0" max="255" name="ap_ip1" class="ip-part"> .
              <input type="number" min="0" max="255" name="ap_ip2" class="ip-part"> .
              <input type="number" min="0" max="255" name="ap_ip3" class="ip-part">
            </div>
          </div>
          
          <div class="form-group">
            <label for="ap_gateway">Gateway:</label>
            <div class="ip-group">
              <input type="number" min="0" max="255" name="ap_gateway0" class="ip-part"> .
              <input type="number" min="0" max="255" name="ap_gateway1" class="ip-part"> .
              <input type="number" min="0" max="255" name="ap_gateway2" class="ip-part"> .
              <input type="number" min="0" max="255" name="ap_gateway3" class="ip-part">
            </div>
          </div>
          
          <div class="form-group">
            <label for="ap_subnet">Subnet Mask:</label>
            <div class="ip-group">
              <input type="number" min="0" max="255" name="ap_subnet0" class="ip-part"> .
              <input type="number" min="0" max="255" name="ap_subnet1" class="ip-part"> .
              <input type="number" min="0" max="255" name="ap_subnet2" class="ip-part"> .
              <input type="number" min="0" max="255" name="ap_subnet3" class="ip-part">
            </div>
          </div>
        </div>
      </div>

      <div class="button-container">
        <button type="button" class="save-button" onclick="saveSettings()">Ayarları Kaydet</button>
      </div>
    </form>
  </div>
  
  <a href="/" class="back-button">Ana Sayfaya Dön</a>
  
  <div id="notification" class="notification"></div>
  
  <script>
    function updateModeFields() {
      const mode = document.getElementById('wifiMode').value;
      document.getElementById('staSettings').style.display = mode === '1' ? 'block' : 'none';
      document.getElementById('apSettings').style.display = mode === '2' ? 'block' : 'none';
    }
    
    function toggleStaticIP() {
      const useStatic = document.getElementById('useStatic').checked;
      document.getElementById('staticIpSettings').style.display = useStatic ? 'block' : 'none';
    }
    
    function toggleAPStaticIP() {
      const useStatic = document.getElementById('ap_useStatic').checked;
      document.getElementById('apStaticIpSettings').style.display = useStatic ? 'block' : 'none';
    }
    
    function loadSettings() {
      fetch('/getWiFiSettings')
        .then(response => response.json())
        .then(data => {
          document.getElementById('wifiMode').value = data.mode;
          updateModeFields();
          
          document.getElementById('staSsid').value = data.ssid;
          document.getElementById('staPassword').value = data.password;
          document.getElementById('useStatic').checked = data.useStaticIP;
          toggleStaticIP();
          
          const ipParts = data.ip.split('.');
          for (let i = 0; i < 4; i++) {
            document.querySelector(`input[name="ip${i}"]`).value = ipParts[i];
          }
          
          const gatewayParts = data.gateway.split('.');
          for (let i = 0; i < 4; i++) {
            document.querySelector(`input[name="gateway${i}"]`).value = gatewayParts[i];
          }
          
          const subnetParts = data.subnet.split('.');
          for (let i = 0; i < 4; i++) {
            document.querySelector(`input[name="subnet${i}"]`).value = subnetParts[i];
          }
          
          const dns1Parts = data.dns1.split('.');
          for (let i = 0; i < 4; i++) {
            document.querySelector(`input[name="dns1${i}"]`).value = dns1Parts[i];
          }
          
          const dns2Parts = data.dns2.split('.');
          for (let i = 0; i < 4; i++) {
            document.querySelector(`input[name="dns2${i}"]`).value = dns2Parts[i];
          }
          
          document.getElementById('apSsid').value = data.ap_ssid;
          document.getElementById('apPassword').value = data.ap_password;
          document.getElementById('ap_useStatic').checked = data.ap_useStaticIP;
          toggleAPStaticIP();
          
          const ap_ipParts = data.ap_ip.split('.');
          for (let i = 0; i < 4; i++) {
            document.querySelector(`input[name="ap_ip${i}"]`).value = ap_ipParts[i];
          }
          
          const ap_gatewayParts = data.ap_gateway.split('.');
          for (let i = 0; i < 4; i++) {
            document.querySelector(`input[name="ap_gateway${i}"]`).value = ap_gatewayParts[i];
          }
          
          const ap_subnetParts = data.ap_subnet.split('.');
          for (let i = 0; i < 4; i++) {
            document.querySelector(`input[name="ap_subnet${i}"]`).value = ap_subnetParts[i];
          }
        })
        .catch(error => {
          console.error('Error loading WiFi settings:', error);
          showNotification('Ayarlar yüklenirken hata oluştu!');
        });
    }
    
    function saveSettings() {
      const formData = new FormData(document.getElementById('wifiForm'));
      const settings = {
        mode: parseInt(formData.get('mode')),
        ssid: formData.get('ssid') || '',
        password: formData.get('password') || '',
        ap_ssid: formData.get('ap_ssid') || 'ESP32-AP',
        ap_password: formData.get('ap_password') || 'password123',
        useStaticIP: document.getElementById('useStatic').checked ? 1 : 0,
        ip: `${formData.get('ip0')}.${formData.get('ip1')}.${formData.get('ip2')}.${formData.get('ip3')}`,
        gateway: `${formData.get('gateway0')}.${formData.get('gateway1')}.${formData.get('gateway2')}.${formData.get('gateway3')}`,
        subnet: `${formData.get('subnet0')}.${formData.get('subnet1')}.${formData.get('subnet2')}.${formData.get('subnet3')}`,
        dns1: `${formData.get('dns10')}.${formData.get('dns11')}.${formData.get('dns12')}.${formData.get('dns13')}`,
        dns2: `${formData.get('dns20')}.${formData.get('dns21')}.${formData.get('dns22')}.${formData.get('dns23')}`,
        ap_useStaticIP: document.getElementById('ap_useStatic').checked ? 1 : 0,
        ap_ip: `${formData.get('ap_ip0')}.${formData.get('ap_ip1')}.${formData.get('ap_ip2')}.${formData.get('ap_ip3')}`,
        ap_gateway: `${formData.get('ap_gateway0')}.${formData.get('ap_gateway1')}.${formData.get('ap_gateway2')}.${formData.get('ap_gateway3')}`,
        ap_subnet: `${formData.get('ap_subnet0')}.${formData.get('ap_subnet1')}.${formData.get('ap_subnet2')}.${formData.get('ap_subnet3')}`
      };
      
      fetch('/saveWiFiSettings', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(settings)
      })
      .then(response => {
        if (response.ok) {
          showNotification('WiFi ayarları kaydedildi! Cihaz yeniden başlatılacak.');
          setTimeout(() => {
            window.location.href = '/';
          }, 3000);
        } else {
          showNotification('Kaydetme hatası!');
        }
      })
      .catch(error => {
        console.error('Error saving settings:', error);
        showNotification('Bağlantı hatası!');
      });
    }
    
    function showNotification(message) {
      const notification = document.getElementById('notification');
      notification.textContent = message;
      notification.style.display = 'block';
      
      setTimeout(() => {
        notification.style.display = 'none';
      }, 3000);
    }
    
    window.addEventListener('load', function() {
      loadSettings();
    });
  </script>
</body>
</html>
)rawliteral";

// ========== WEB SOCKET VE PROGRAM FONKSİYONLARI ==========

void sendFullStateUpdate() {
  if (ws.count() == 0) return;
  StaticJsonDocument<256> doc;
  doc["type"] = "fullUpdate";
  doc["isRunning"] = isRunning;
  doc["currentStepIndex"] = currentStepIndex;
  doc["stepCount"] = stepCount;
  doc["currentTemp"] = temperature;
  doc["targetTemp"] = isRunning ? pidSetpoint : 0;
  doc["remainingHours"] = calculateRemainingHours();
  doc["remainingMinutes"] = calculateRemainingMinutes();
  doc["remainingSeconds"] = calculateRemainingSeconds();
  doc["phase"] = getCurrentPhaseName();
  doc["timedMode"] = settings.timedMode;
  String output;
  serializeJson(doc, output);
  ws.textAll(output);
}

unsigned long lastFullUpdate = 0;

void startAutotune() {
  autotuneRunning = true;
  programState = STATE_AUTOTUNE;
  autotuneStep = 0;
  autotuneStartTime = millis();
  autotuneProgress = 0;
  autotuneStatus = "Başlatılıyor...";
  pidControlActive = false;
}

void stopAutotune() {
  autotuneRunning = false;
  programState = isRunning ? STATE_RAMP : STATE_IDLE;
  autotuneStatus = "Kullanıcı tarafından durduruldu";
  digitalWrite(SSR_PIN, LOW);
  if (isRunning) {
    pidControlActive = true;
  }
}

void checkWebSocket() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    if (clientConnected) {
      ws.cleanupClients();
    }
  }
}

void runProgram() {
  if (!isRunning || stepCount == 0) {
    programState = STATE_IDLE;
    digitalWrite(SSR_PIN, LOW);
    updateDisplayStatus("Program STOPPED", TFT_RED);
    return;
  }
  unsigned long now = millis();
  Step currentStep = steps[currentStepIndex];
  switch (programState) {
    case STATE_DELAY:
      pidControlActive = false;
      digitalWrite(SSR_PIN, LOW);
      if (now >= delayEndTime) {
        programState = STATE_RAMP;
        rampStartTemp = temperature;
        currentTargetTemp = rampStartTemp;
        pidSetpoint = currentTargetTemp;
        pidControlActive = true;
        stepStartTime = now;
        if (currentStep.dakikaCheck) {
          if (currentStep.dakikaDegisim <= 0.0) {
            isRunning = false;
            programState = STATE_IDLE;
            pidControlActive = false;
            digitalWrite(SSR_PIN, LOW);
            saveLastStateToEEPROM();
            updateDisplayStatus("Gecersiz rampa hizi", TFT_RED);
            return;
          }
          float tempDifference = fabs(currentStep.sicaklik - rampStartTemp);
          unsigned long rampTimeSec = (unsigned long)((tempDifference / currentStep.dakikaDegisim) * 60);
          rampEndTime = now + rampTimeSec * 1000;
        } else {
          unsigned long rampTimeSec = currentStep.rampSaat * 3600 + currentStep.rampDakika * 60;
          rampEndTime = now + rampTimeSec * 1000;
        }
        saveLastStateToEEPROM();
      }
      break;
    case STATE_RAMP:
      pidControlActive = true;
      if (now >= rampEndTime) {
        programState = STATE_STEP;
        stepStartTime = now;
        if (settings.timedMode) {
          stepEndTime = now + (currentStep.calismaSaat * 3600 + currentStep.calismaDakika * 60) * 1000;
        } else {
          stepEndTime = ULONG_MAX;
        }
        saveLastStateToEEPROM();
      } else {
        float progress = (float)(now - stepStartTime) / (rampEndTime - stepStartTime);
        currentTargetTemp = rampStartTemp + (currentStep.sicaklik - rampStartTemp) * progress;
        pidSetpoint = currentTargetTemp;
      }
      break;
    case STATE_STEP:
      pidControlActive = true;
      pidSetpoint = currentStep.sicaklik;
      if (settings.timedMode && now >= stepEndTime) {
        currentStepIndex++;
        if (currentStepIndex >= stepCount) {
          programRepeatCounter++;
          currentStepIndex = 0;
          if (programRepeatCounter >= programRepeatCount) {
            isRunning = false;
            programState = STATE_COMPLETED;
            pidControlActive = false;
            digitalWrite(SSR_PIN, LOW);
            updateDisplayStatus("Program COMPLETED", TFT_GREEN);
            saveLastStateToEEPROM();
            return;
          }
        }
        programState = STATE_RAMP;
        stepStartTime = now;
        rampStartTemp = temperature;
        currentStep = steps[currentStepIndex];
        if (currentStep.dakikaCheck) {
          if (currentStep.dakikaDegisim <= 0.0) {
            isRunning = false;
            programState = STATE_IDLE;
            pidControlActive = false;
            digitalWrite(SSR_PIN, LOW);
            saveLastStateToEEPROM();
            updateDisplayStatus("Gecersiz rampa hizi", TFT_RED);
            return;
          }
          float tempDifference = fabs(currentStep.sicaklik - rampStartTemp);
          unsigned long rampTimeSec = (unsigned long)((tempDifference / currentStep.dakikaDegisim) * 60);
          rampEndTime = now + rampTimeSec * 1000;
        } else {
          unsigned long rampTimeSec = currentStep.rampSaat * 3600 + currentStep.rampDakika * 60;
          rampEndTime = now + rampTimeSec * 1000;
        }
        saveLastStateToEEPROM();
      }
      break;
    case STATE_AUTOTUNE:
      runAutotune();
      break;
    case STATE_COMPLETED:
      break;
    default:
      break;
  }
  if (now - lastStepUpdateTime >= 1000) {
    lastStepUpdateTime = now;
    updateProgramStatus();
  }
  if (!autotuneRunning && pidControlActive) {
    computePID();
  }
}

void toggleProgramState() {
  if (isRunning) {
    isRunning = false;
    pidControlActive = false;
    digitalWrite(SSR_PIN, LOW);
    programState = STATE_IDLE;
    updateDisplayStatus("Program STOPPED", TFT_RED);
    saveStepsToEEPROM();
    saveLastStateToEEPROM();
    notifyStatusChange();
    // Ekranı güncelle
    if (!inMenu && !showingQRCode) {
      displayTime7Segment(hh, mm, ss);
    }
    return;
  }
  if (stepCount == 0) {
    updateDisplayStatus("No steps defined!", TFT_RED);
    return;
  }
  programStartTime = millis();
  stepStartTime = millis();
  currentStepIndex = 0;
  programRepeatCounter = 0;
  pidErrorSum = 0;
  pidLastError = 0;
  pidLastTime = millis();
  pidSetpoint = temperature;
  pidControlActive = false;
  programState = STATE_DELAY;
  rampStartTemp = temperature;
  unsigned long delayTotalSec = settings.delayHours * 3600 + settings.delayMinutes * 60;
  delayEndTime = millis() + (delayTotalSec > 0 ? delayTotalSec * 1000 : 100);
  isRunning = true;
  digitalWrite(SSR_PIN, LOW);
  updateDisplayStatus("Program STARTED", TFT_GREEN);
  saveStepsToEEPROM();
  saveLastStateToEEPROM();
  notifyStatusChange();
  // Ekranı güncelle
  if (!inMenu && !showingQRCode) {
    displayTime7Segment(hh, mm, ss);
  }
}

void updateProgramStatus() {
  if (ws.count() == 0) return;
  unsigned long now = millis();
  Step currentStep = steps[currentStepIndex];
  StaticJsonDocument<256> doc;
  doc["isRunning"] = isRunning;
  doc["currentStepIndex"] = currentStepIndex;
  doc["stepCount"] = stepCount;
  doc["targetTemp"] = (programState == STATE_RAMP) ? currentTargetTemp : currentStep.sicaklik;
  unsigned long remaining = 0;
  if (programState == STATE_DELAY) {
    remaining = (delayEndTime > now) ? (delayEndTime - now) / 1000 : 0;
  } else if (programState == STATE_RAMP) {
    remaining = (rampEndTime > now) ? (rampEndTime - now) / 1000 : 0;
  } else if (programState == STATE_STEP && settings.timedMode) {
    remaining = (stepEndTime > now) ? (stepEndTime - now) / 1000 : 0;
  }
  doc["remainingHours"] = remaining / 3600;
  doc["remainingMinutes"] = (remaining % 3600) / 60;
  doc["remainingSeconds"] = remaining % 60;
  doc["programRepeatCounter"] = programRepeatCounter;
  doc["programRepeatCount"] = programRepeatCount;
  doc["phase"] = getCurrentPhaseName();
  doc["timedMode"] = settings.timedMode;
  String output;
  serializeJson(doc, output);
  ws.textAll(output);
  
  // TFT ekranı güncelle
  static unsigned long lastTFTUpdate = 0;
  if (millis() - lastTFTUpdate > 1000) {
    lastTFTUpdate = millis();
    if (!inMenu && !showingQRCode) {
      displayTime7Segment(hh, mm, ss);
    }
  }
}

void updateDisplayStatus(const char *message, uint16_t color) {
  static unsigned long lastUpdate = 0;
  static String lastMessage = "";
  if (millis() - lastUpdate < 1000 && lastMessage == message) {
    return;
  }
  tft.fillRect(0, 120, 240, 120, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextFont(1);
  tft.setTextColor(color);
  tft.setCursor(10, 150);
  tft.println(message);
  lastUpdate = millis();
  lastMessage = message;
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String message = (char *)data;
    if (message == "getStatus") {
      String response = isRunning ? "running" : "stopped";
      ws.textAll(response);
    }
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      {
        Serial.printf("WebSocket istemci #%u bağlandı, IP: %s\n", client->id(), client->remoteIP().toString().c_str());
        client->ping();
        clientConnected = true;
        client->keepAlivePeriod(30);
        StaticJsonDocument<256> doc;
        doc["type"] = "fullUpdate";
        doc["isRunning"] = isRunning;
        doc["currentStepIndex"] = currentStepIndex;
        doc["stepCount"] = stepCount;
        doc["currentTemp"] = temperature;
        doc["targetTemp"] = isRunning ? pidSetpoint : 0;
        doc["remainingHours"] = calculateRemainingHours();
        doc["remainingMinutes"] = calculateRemainingMinutes();
        doc["remainingSeconds"] = calculateRemainingSeconds();
        doc["phase"] = getCurrentPhaseName();
        doc["timedMode"] = settings.timedMode;
        String output;
        serializeJson(doc, output);
        client->text(output);
        break;
      }
    case WS_EVT_DISCONNECT:
      {
        Serial.printf("WebSocket istemci #%u bağlantıyı kesti\n", client->id());
        clientConnected = (server->count() > 0);
        break;
      }
    case WS_EVT_DATA:
      {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
          data[len] = 0;
          String message = (char *)data;
          if (message == "getStatus") {
            client->text(isRunning ? "running" : "stopped");
          } else if (message == "getFullStatus") {
            sendFullStateUpdate();
          } else if (message.startsWith("CMD:")) {
            String cmd = message.substring(4);
            if (cmd == "start") {
              if (!isRunning) toggleProgramState();
            } else if (cmd == "stop") {
              if (isRunning) toggleProgramState();
            }
          }
        }
        break;
      }
    case WS_EVT_PONG:
      {
        Serial.printf("WebSocket istemci #%u pong aldı\n", client->id());
        break;
      }
    case WS_EVT_ERROR:
      {
        Serial.printf("WebSocket istemci #%u hatası: %s\n", client->id(), (char *)data);
        break;
      }
  }
}

void notifyStatusChange() {
  if (clientConnected) {
    String message = isRunning ? "running" : "stopped";
    ws.textAll(message);
  }
}

bool collectRequestBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                        size_t index, size_t total, size_t maxSize, String *&body) {
  body = nullptr;
  if (total == 0 || total > maxSize) {
    if (index == 0) request->send(413, "text/plain", "İstek gövdesi çok büyük");
    return false;
  }
  if (index == 0) {
    String *newBody = new String();
    if (newBody == nullptr || !newBody->reserve(total)) {
      delete newBody;
      request->send(500, "text/plain", "Bellek yetersiz");
      return false;
    }
    request->_tempObject = newBody;
  }
  String *requestBody = static_cast<String *>(request->_tempObject);
  if (requestBody == nullptr) return false;
  requestBody->concat((const char *)data, len);
  if (index + len < total) return false;
  request->_tempObject = nullptr;
  body = requestBody;
  return true;
}

// ========== SETUP ==========

void setup() {
  pinMode(42, INPUT_PULLDOWN);
  Serial.begin(115200);
  delay(2000);
  pinMode(41, OUTPUT);
  digitalWrite(41, LOW);
  pinMode(16, OUTPUT);
  digitalWrite(16, LOW);
  pinMode(ADS_CS, OUTPUT);
  pinMode(ADS_DRDY, INPUT);
  digitalWrite(ADS_CS, HIGH);
  adsSPI.begin(ADS_SCLK, ADS_MISO, ADS_MOSI, ADS_CS);
  adsSPI.setFrequency(4000000);
  adsSPI.setBitOrder(MSBFIRST);
  adsSPI.setDataMode(SPI_MODE1);
  resetADS1220();

 Serial.print("oto deneme ");
  Serial.println("3");

  delay(500);
  configureADS1220DiffAVDD_AVSS();
  startContinuousConversion();
  delay(100);
  adcRaw = readADS1220Data();
  adsvolt = (adcRaw * (VREF / 1.0)) / FULL_SCALE * 4.0;
  configureADS1220InternalTemp();
  startContinuousConversion();
  delay(100);
  int16_t temp14 = readADS1220Temp14Bit();
  tempC = convert14BitToTemp(temp14);
  delay(500);
  pinMode(BUTTON1_PIN, INPUT);
  pinMode(BUTTON2_PIN, INPUT);
  pinMode(BUTTON3_PIN, INPUT);
  pinMode(BUTTON4_PIN, INPUT);
  pinMode(SSR_PIN, OUTPUT);
  digitalWrite(SSR_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  tft.init();
  tft.setRotation(1);
  qrcode.init();
  tft.fillScreen(TFT_BLACK);
  Wire.begin(SDA_PIN, SCL_PIN, 200000);
  loadSettingsFromEEPROM();
  loadStepsFromEEPROM();
  loadWiFiSettingsFromEEPROM();
  switch (settings.sensorType) {
    case SENSOR_K_TYPE: sensorType = 'K'; break;
    case SENSOR_J_TYPE: sensorType = 'J'; break;
    case SENSOR_S_TYPE: sensorType = 'S'; break;
    case SENSOR_PT100_2WIRE: sensorType = '2'; break;
    case SENSOR_PT100_3WIRE: sensorType = '3'; break;
    case SENSOR_PT100_4WIRE: sensorType = '4'; break;
    default: sensorType = '2'; break;
  }
  if (sensorType >= 'A' && sensorType <= 'Z') {
    configureADS1220DiffAIN1_AIN2();
  } else {
    switch (sensorType) {
      case '2': configureADS1220Pt2wire(); break;
      case '3': configureADS1220Pt3wire(); break;
      case '4': configureADS1220Pt4wire(); break;
      default: configureADS1220Pt2wire(); break;
    }
  }
  startContinuousConversion();
  delay(500);
  adcRaw = readADS1220Data();
  double mV = (adcRaw * (VREF / GAIN)) / FULL_SCALE * 1000.0;
  double filteredmV = applyFilter(mV);
  sensorTemp = mvToTemperature(sensorType, filteredmV);
  totalTemp = sensorTemp;
  if (sensorType >= 'A' && sensorType <= 'Z') {
    totalTemp += tempC;
  }
  temperature = totalTemp;
  for (int i = 0; i < FILTER_SAMPLES; i++) {
    mvHistory[i] = filteredmV;
  }
  Serial.println("Sıcaklık Sensörü Okuyucu - Hazır");
  Serial.print("Mevcut sensör tipi: ");
  Serial.println(sensorType);
  printHelp();
  forceAPMode = (digitalRead(BUTTON1_PIN) == LOW);
  Serial.print("forceAPMode=");
  Serial.println(forceAPMode);
  delay(2000);
  if (forceAPMode) {
    Serial.println("Başlangıçta buton basılı - Sabit IP ile AP modu zorlanıyor");
    tft.setTextSize(1);
    tft.setTextFont(1);
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(10, 40);
    tft.println("Zorunlu AP Modu");
    tft.setCursor(10, 60);
    tft.println("SSID: otosens");
    tft.setCursor(10, 80);
    tft.println("Sifre: 12345678");
    tft.setCursor(10, 100);
    tft.println("IP: 192.168.100.1");
    delay(2000);
    startAPModeWithFixedIP("otosens", "12345678");
  } else {
    applyWiFiSettings();
  }
  if (wifiSettings.mode == 0) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextFont(1);
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(10, 40);
    tft.println("WiFi KAPALI");
    tft.setCursor(10, 60);
    tft.println("Buton 1 ile degistir");
  }
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/getProgramStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc;
    doc["isRunning"] = isRunning;
    doc["timedMode"] = settings.timedMode;
    doc["currentStepIndex"] = currentStepIndex;
    doc["stepCount"] = stepCount;
    doc["programRepeatCounter"] = programRepeatCounter;
    doc["programRepeatCount"] = programRepeatCount;
    doc["currentTemp"] = temperature;
    if (isRunning) {
      doc["targetTemp"] = (programState == STATE_RAMP) ? currentTargetTemp : steps[currentStepIndex].sicaklik;
      doc["rampStartTemp"] = rampStartTemp;
      unsigned long elapsedStepTimeSec = (millis() - stepStartTime) / 1000;
      if (programState == STATE_DELAY) {
        doc["phase"] = "DELAY";
        doc["phaseStatus"] = "BEKLEMEDE";
        unsigned long delayTotalSec = settings.delayHours * 3600 + settings.delayMinutes * 60;
        unsigned long remaining = (elapsedStepTimeSec >= delayTotalSec) ? 0 : delayTotalSec - elapsedStepTimeSec;
        doc["remainingHours"] = remaining / 3600;
        doc["remainingMinutes"] = (remaining % 3600) / 60;
        doc["remainingSeconds"] = remaining % 60;
      } else if (programState == STATE_RAMP) {
        doc["phase"] = "RAMP";
        doc["phaseStatus"] = "RAMPA";
        unsigned long calculatedRampTimeSec;
        if (steps[currentStepIndex].dakikaCheck) {
          float tempDifference = fabs(steps[currentStepIndex].sicaklik - rampStartTemp);
          float ratePerMinute = steps[currentStepIndex].dakikaDegisim;
          calculatedRampTimeSec = (unsigned long)((tempDifference / ratePerMinute) * 60);
        } else {
          calculatedRampTimeSec = steps[currentStepIndex].rampSaat * 3600 + steps[currentStepIndex].rampDakika * 60;
        }
        unsigned long remaining = (elapsedStepTimeSec >= calculatedRampTimeSec) ? 0 : calculatedRampTimeSec - elapsedStepTimeSec;
        doc["remainingHours"] = remaining / 3600;
        doc["remainingMinutes"] = (remaining % 3600) / 60;
        doc["remainingSeconds"] = remaining % 60;
      } else if (programState == STATE_STEP) {
        doc["phase"] = "STEP";
        doc["phaseStatus"] = settings.timedMode ? "CALISIYOR" : "SURESIZ";
        if (settings.timedMode) {
          unsigned long totalWorkTimeSec = steps[currentStepIndex].calismaSaat * 3600 + steps[currentStepIndex].calismaDakika * 60;
          unsigned long remaining = (elapsedStepTimeSec >= totalWorkTimeSec) ? 0 : totalWorkTimeSec - elapsedStepTimeSec;
          doc["remainingHours"] = remaining / 3600;
          doc["remainingMinutes"] = (remaining % 3600) / 60;
          doc["remainingSeconds"] = remaining % 60;
        }
      }
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  server.on("/getTemperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<128> doc;
    bool changed = (fabs(temperature - lastSentTemp) >= 0.1);
    doc["temperature"] = temperature;
    doc["changed"] = changed;
    doc["isRunning"] = isRunning;
    if (changed) {
      lastSentTemp = temperature;
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  server.on("/getSteps", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < stepCount; i++) {
      JsonObject obj = arr.createNestedObject();
      obj["sicaklik"] = steps[i].sicaklik;
      obj["rampSaat"] = steps[i].rampSaat;
      obj["rampDakika"] = steps[i].rampDakika;
      obj["calismaSaat"] = steps[i].calismaSaat;
      obj["calismaDakika"] = steps[i].calismaDakika;
      obj["dakikaDegisim"] = steps[i].dakikaDegisim;
      obj["dakikaCheck"] = steps[i].dakikaCheck;
    }
    JsonObject stateObj = doc.createNestedObject();
    stateObj["isRunning"] = isRunning;
    stateObj["repeatCount"] = programRepeatCount;
    stateObj["timedMode"] = settings.timedMode;
    stateObj["delayHours"] = settings.delayHours;
    stateObj["delayMinutes"] = settings.delayMinutes;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  server.on(
    "/saveSteps", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String *body;
      if (!collectRequestBody(request, data, len, index, total, 8192, body)) return;
      StaticJsonDocument<4096> doc;
      DeserializationError error = deserializeJson(doc, *body);
      delete body;
      if (error) {
        Serial.println("JSON parse hatası");
        request->send(400, "text/plain", "Geçersiz JSON");
        return;
      }
      JsonArray arr = doc.as<JsonArray>();
      uint8_t actualStepCount = arr.size() > 0 ? arr.size() - 1 : 0;
      if (actualStepCount > MAX_STEPS) {
        actualStepCount = MAX_STEPS;
      }
      stepCount = actualStepCount;
      for (uint8_t i = 0; i < stepCount; i++) {
        JsonObject obj = arr[i];
        steps[i].sicaklik = obj["sicaklik"] | 0.0;
        steps[i].rampSaat = obj["rampSaat"] | 0;
        steps[i].rampDakika = obj["rampDakika"] | 0;
        steps[i].calismaSaat = obj["calismaSaat"] | 0;
        steps[i].calismaDakika = obj["calismaDakika"] | 0;
        steps[i].dakikaDegisim = obj["dakikaDegisim"] | 0.0;
        steps[i].dakikaCheck = obj["dakikaCheck"] | false;
      }
      if (arr.size() > 0) {
        JsonObject lastObj = arr[arr.size() - 1];
        programRepeatCount = lastObj["repeatCount"] | 1;
        if (programRepeatCount < 1) programRepeatCount = 1;
        settings.timedMode = lastObj["timedMode"] | true;
        settings.delayHours = lastObj["delayHours"] | 0;
        settings.delayMinutes = lastObj["delayMinutes"] | 0;
        saveSettingsToEEPROM();
      }
      saveStepsToEEPROM();
      Serial.printf("Web'den %d step kaydedildi\n", stepCount);
      request->send(200, "text/plain", "Kaydedildi");
    });
  server.on("/clearSteps", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (isRunning) {
      isRunning = false;
      pidControlActive = false;
      programState = STATE_IDLE;
      digitalWrite(SSR_PIN, LOW);
    }
    stepCount = 0;
    selectedIndex = -1;
    saveStepsToEEPROM();
    saveLastStateToEEPROM();
    request->send(200, "text/plain", "Temizlendi");
    Serial.println("Web isteği ile stepler temizlendi");
  });
  server.on("/toggleRun", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("state", true)) {
      String val = request->getParam("state", true)->value();
      bool newState = (val == "1");
      if (newState != isRunning) {
        toggleProgramState();
      }
      StaticJsonDocument<96> doc;
      doc["success"] = true;
      doc["isRunning"] = isRunning;
      String output;
      serializeJson(doc, output);
      request->send(200, "application/json", output);
    } else {
      request->send(400, "text/plain", "Missing state parameter");
    }
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  server.on("/program", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", program_html);
  });
  server.on("/getSettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<256> doc;
    doc["kalibrasyon"] = settings.kalibrasyon;
    doc["sensorFaktor"] = settings.sensorFaktor;
    doc["kp"] = settings.kp;
    doc["ki"] = settings.ki;
    doc["kd"] = settings.kd;
    doc["sensorType"] = settings.sensorType;
    doc["autotuneTemp"] = settings.autotuneTemp;
    doc["alarmTemperature"] = settings.alarmTemperature;
    doc["alarmEnabled"] = settings.alarmEnabled;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  server.on(
    "/saveSettings", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String *body;
      if (!collectRequestBody(request, data, len, index, total, 1024, body)) return;
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, *body);
      delete body;
      if (error) {
        Serial.println("JSON parse hatası");
        request->send(400, "text/plain", "Geçersiz JSON");
        return;
      }
      settings.kalibrasyon = doc["kalibrasyon"] | 0.0;
      settings.sensorFaktor = doc["sensorFaktor"] | 0.0;
      settings.kp = doc["kp"] | 0.0;
      settings.ki = doc["ki"] | 0.0;
      settings.kd = doc["kd"] | 0.0;
      settings.sensorType = doc["sensorType"] | SENSOR_PT100_2WIRE;
      settings.autotuneTemp = doc["autotuneTemp"] | 100.0;
      settings.alarmTemperature = doc["alarmTemperature"] | 100.0;
      settings.alarmEnabled = doc["alarmEnabled"] | false;
      switch (settings.sensorType) {
        case SENSOR_K_TYPE: sensorType = 'K'; break;
        case SENSOR_J_TYPE: sensorType = 'J'; break;
        case SENSOR_S_TYPE: sensorType = 'S'; break;
        case SENSOR_PT100_2WIRE: sensorType = '2'; break;
        case SENSOR_PT100_3WIRE: sensorType = '3'; break;
        case SENSOR_PT100_4WIRE: sensorType = '4'; break;
        default: sensorType = '2'; break;
      }
      if (sensorType >= 'A' && sensorType <= 'Z') {
        configureADS1220DiffAIN1_AIN2();
      } else {
        switch (sensorType) {
          case '2': configureADS1220Pt2wire(); break;
          case '3': configureADS1220Pt3wire(); break;
          case '4': configureADS1220Pt4wire(); break;
          default: configureADS1220Pt2wire(); break;
        }
      }
      startContinuousConversion();
      delay(100);
      saveSettingsToEEPROM();
      request->send(200, "text/plain", "Ayarlar kaydedildi");
      Serial.println("Web arayüzünden ayarlar kaydedildi");
    });
  server.on("/tempsettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", tempsettings_html);
  });
  server.on("/wifisettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", wifisettings_html);
  });
  server.on("/getWiFiSettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc;
    doc["mode"] = wifiSettings.mode;
    doc["ssid"] = wifiSettings.ssid;
    doc["password"] = wifiSettings.password;
    doc["ap_ssid"] = wifiSettings.ap_ssid;
    doc["ap_password"] = wifiSettings.ap_password;
    doc["useStaticIP"] = wifiSettings.useStaticIP;
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
             wifiSettings.ip[0], wifiSettings.ip[1], wifiSettings.ip[2], wifiSettings.ip[3]);
    doc["ip"] = ipStr;
    char gatewayStr[16];
    snprintf(gatewayStr, sizeof(gatewayStr), "%d.%d.%d.%d",
             wifiSettings.gateway[0], wifiSettings.gateway[1], wifiSettings.gateway[2], wifiSettings.gateway[3]);
    doc["gateway"] = gatewayStr;
    char subnetStr[16];
    snprintf(subnetStr, sizeof(subnetStr), "%d.%d.%d.%d",
             wifiSettings.subnet[0], wifiSettings.subnet[1], wifiSettings.subnet[2], wifiSettings.subnet[3]);
    doc["subnet"] = subnetStr;
    char dns1Str[16];
    snprintf(dns1Str, sizeof(dns1Str), "%d.%d.%d.%d",
             wifiSettings.dns1[0], wifiSettings.dns1[1], wifiSettings.dns1[2], wifiSettings.dns1[3]);
    doc["dns1"] = dns1Str;
    char dns2Str[16];
    snprintf(dns2Str, sizeof(dns2Str), "%d.%d.%d.%d",
             wifiSettings.dns2[0], wifiSettings.dns2[1], wifiSettings.dns2[2], wifiSettings.dns2[3]);
    doc["dns2"] = dns2Str;
    doc["ap_useStaticIP"] = wifiSettings.ap_useStaticIP;
    char ap_ipStr[16];
    snprintf(ap_ipStr, sizeof(ap_ipStr), "%d.%d.%d.%d",
             wifiSettings.ap_ip[0], wifiSettings.ap_ip[1], wifiSettings.ap_ip[2], wifiSettings.ap_ip[3]);
    doc["ap_ip"] = ap_ipStr;
    char ap_gatewayStr[16];
    snprintf(ap_gatewayStr, sizeof(ap_gatewayStr), "%d.%d.%d.%d",
             wifiSettings.ap_gateway[0], wifiSettings.ap_gateway[1], wifiSettings.ap_gateway[2], wifiSettings.ap_gateway[3]);
    doc["ap_gateway"] = ap_gatewayStr;
    char ap_subnetStr[16];
    snprintf(ap_subnetStr, sizeof(ap_subnetStr), "%d.%d.%d.%d",
             wifiSettings.ap_subnet[0], wifiSettings.ap_subnet[1], wifiSettings.ap_subnet[2], wifiSettings.ap_subnet[3]);
    doc["ap_subnet"] = ap_subnetStr;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  server.on(
    "/saveWiFiSettings", HTTP_POST, [](AsyncWebServerRequest *request) {},
    NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String *body;
      if (!collectRequestBody(request, data, len, index, total, 2048, body)) return;
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, *body);
      delete body;
      if (error) {
        request->send(400, "text/plain", "Geçersiz JSON");
        return;
      }
      wifiSettings.mode = doc["mode"];
      strlcpy(wifiSettings.ssid, doc["ssid"] | "", sizeof(wifiSettings.ssid));
      strlcpy(wifiSettings.password, doc["password"] | "", sizeof(wifiSettings.password));
      strlcpy(wifiSettings.ap_ssid, doc["ap_ssid"] | "ESP32-AP", sizeof(wifiSettings.ap_ssid));
      strlcpy(wifiSettings.ap_password, doc["ap_password"] | "password123", sizeof(wifiSettings.ap_password));
      wifiSettings.useStaticIP = doc["useStaticIP"];
      wifiSettings.ap_useStaticIP = doc["ap_useStaticIP"];
      const char *ip = doc["ip"];
      if (ip) {
        sscanf(ip, "%hhu.%hhu.%hhu.%hhu",
               &wifiSettings.ip[0], &wifiSettings.ip[1],
               &wifiSettings.ip[2], &wifiSettings.ip[3]);
      }
      const char *gateway = doc["gateway"];
      if (gateway) {
        sscanf(gateway, "%hhu.%hhu.%hhu.%hhu",
               &wifiSettings.gateway[0], &wifiSettings.gateway[1],
               &wifiSettings.gateway[2], &wifiSettings.gateway[3]);
      }
      const char *subnet = doc["subnet"];
      if (subnet) {
        sscanf(subnet, "%hhu.%hhu.%hhu.%hhu",
               &wifiSettings.subnet[0], &wifiSettings.subnet[1],
               &wifiSettings.subnet[2], &wifiSettings.subnet[3]);
      }
      const char *dns1 = doc["dns1"];
      if (dns1) {
        sscanf(dns1, "%hhu.%hhu.%hhu.%hhu",
               &wifiSettings.dns1[0], &wifiSettings.dns1[1],
               &wifiSettings.dns1[2], &wifiSettings.dns1[3]);
      }
      const char *dns2 = doc["dns2"];
      if (dns2) {
        sscanf(dns2, "%hhu.%hhu.%hhu.%hhu",
               &wifiSettings.dns2[0], &wifiSettings.dns2[1],
               &wifiSettings.dns2[2], &wifiSettings.dns2[3]);
      }
      const char *ap_ip = doc["ap_ip"];
      if (ap_ip) {
        sscanf(ap_ip, "%hhu.%hhu.%hhu.%hhu",
               &wifiSettings.ap_ip[0], &wifiSettings.ap_ip[1],
               &wifiSettings.ap_ip[2], &wifiSettings.ap_ip[3]);
      }
      const char *ap_gateway = doc["ap_gateway"];
      if (ap_gateway) {
        sscanf(ap_gateway, "%hhu.%hhu.%hhu.%hhu",
               &wifiSettings.ap_gateway[0], &wifiSettings.ap_gateway[1],
               &wifiSettings.ap_gateway[2], &wifiSettings.ap_gateway[3]);
      }
      const char *ap_subnet = doc["ap_subnet"];
      if (ap_subnet) {
        sscanf(ap_subnet, "%hhu.%hhu.%hhu.%hhu",
               &wifiSettings.ap_subnet[0], &wifiSettings.ap_subnet[1],
               &wifiSettings.ap_subnet[2], &wifiSettings.ap_subnet[3]);
      }
      saveWiFiSettingsToEEPROM();
      request->onDisconnect([]() {
        restartRequestTime = millis();
        restartRequested = true;
      });
      request->send(200, "text/plain", "WiFi ayarları kaydedildi");
    });
  server.on(
    "/startAutotune", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String *body;
      if (!collectRequestBody(request, data, len, index, total, 512, body)) return;
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, *body);
      delete body;
      if (error) {
        Serial.println("JSON parse hatası");
        request->send(400, "text/plain", "Geçersiz JSON");
        return;
      }
      autotuneTargetTemp = doc["targetTemp"] | 100.0;
      autotuneSetValue = autotuneTargetTemp;
      pidSetpoint = autotuneTargetTemp;
      saveSettingsToEEPROM();
      autotuneRunning = true;
      autotuneStep = 0;
      autotuneStartTime = millis();
      autotuneProgress = 0;
      autotuneStatus = "Başlatılıyor...";
      Serial.printf("Hedef sıcaklık: %.1f ile autotune başlatılıyor\n", autotuneTargetTemp);
      request->send(200, "text/plain", "Autotune başlatıldı");
    });
  server.on("/stopAutotune", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (autotuneRunning) {
      autotuneRunning = false;
      autotuneStatus = "Kullanıcı tarafından durduruldu";
      digitalWrite(SSR_PIN, LOW);
      Serial.println("Autotune kullanıcı tarafından durduruldu");
    }
    request->send(200, "text/plain", "Autotune durduruldu");
  });
  server.on("/getAutotuneStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<256> doc;
    doc["running"] = autotuneRunning;
    doc["progress"] = autotuneProgress;
    doc["status"] = autotuneStatus;
    doc["complete"] = (autotuneStep == 5);
    if (autotuneStep == 5) {
      doc["kp"] = autotune_kp;
      doc["ki"] = autotune_ki;
      doc["kd"] = autotune_kd;
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      const bool success = otaUpdateStarted && !Update.hasError();
      request->send(success ? 200 : 500, "application/json", success ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"OTA update failed\"}");
      otaUpdateStarted = false;
      if (success) {
        restartRequested = true;
        restartRequestTime = millis();
      }
    },
    nullptr,
    handleFirmwareUpload);
  server.begin();
  const IPAddress serverIp = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
  Serial.printf("ESP32 web server started on port 80, IP: %s\n", serverIp.toString().c_str());
  platformDeviceId = buildPlatformDeviceId();
  Serial.printf("MikroDMZ cihaz seri kimliği: %s\n", platformDeviceId.c_str());
  if ((wifiSettings.mode == 1 || forceAPMode == 1) && WiFi.status() == WL_CONNECTED) {
#if MIKRODMZ_USE_OWN_TUNNEL
    tunnel.beginDirect(MIKRODMZ_TUNNEL_WS_URL, MIKRODMZ_TUNNEL_TOKEN, 80, "", platformDeviceId.c_str(), MIKRODMZ_RAILWAY_ROOT_CA);
    Serial.println("Kendi Node.js tüneli başlatılıyor...");
    Serial.printf("Node.js tunnel WS: %s\n", MIKRODMZ_TUNNEL_WS_URL);
#else
    tunnel.begin(MIKRODMZ_SUPERDMZ_TOKEN, 80);
    Serial.println("SuperDMZ tüneli başlatılıyor (ESP32 web arayüzü için)...");
    Serial.println("ESP32 web arayüzü public URL: https://mikro5.dmzgate.com");
    Serial.printf("Node.js platform API: %s\n", MIKRODMZ_PLATFORM_URL);
    tft.fillRect(0, 230, 240, 20, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextFont(1);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(10, 240);
    tft.println("SuperDMZ: mikro5.dmzgate.com");
#endif
    registerWithPlatform();
  }
  loadLastStateFromEEPROM();
  if (!inMenu && !showingQRCode) {
    displayTemperatureBig(temperature);
    displayTime7Segment(hh, mm, ss);
  }
  powerFailureDetected = false;
  attachInterrupt(digitalPinToInterrupt(42), powerFailureISR, RISING);
}

// ========== LOOP ==========

void loop() {
  if (restartRequested && millis() - restartRequestTime >= 1000) {
    digitalWrite(SSR_PIN, LOW);
    if (isRunning) saveLastStateToEEPROM();
    ESP.restart();
  }
  if ((wifiSettings.mode == 1 || forceAPMode == 1) && WiFi.status() == WL_CONNECTED) {
    tunnel.loop();
  }
  if (powerFailureDetected) {
    powerFailureDetected = false;
    if (digitalRead(42) == HIGH) {
      digitalWrite(SSR_PIN, LOW);
      pidControlActive = false;
      if (!powerFailureHold) {
        saveLastStateToEEPROM();
        Serial.println("State saved to EEPROM due to power failure");
      }
      powerFailureHold = true;
    }
  }
  if (powerFailureHold && digitalRead(42) == HIGH) {
    digitalWrite(SSR_PIN, LOW);
    delay(1);
    return;
  }
  if (powerFailureHold && digitalRead(42) == LOW) {
    powerFailureHold = false;
    pidControlActive = isRunning &&
                       (programState == STATE_RAMP || programState == STATE_STEP);
    Serial.println("Power failure signal cleared; normal operation resumed");
  }
  unsigned long now = millis();
  checkWebSocket();
  handleMenuNavigation();
  if (millis() - lastFullUpdate > 5000) {
    sendFullStateUpdate();
    lastFullUpdate = millis();
  }
  if (now - lastSerialCheck >= 100) {
    lastSerialCheck = now;
    checkSerialInput();
  }
  if (now - lastVoltMillis >= 1000) {
    lastVoltMillis = now;
    adcRaw = readADS1220Data();
    double mV = (adcRaw * (VREF / GAIN)) / FULL_SCALE * 1000.0;
    double filteredmV = applyFilter(mV);
    sensorTemp = mvToTemperature(sensorType, filteredmV);
    totalTemp = sensorTemp;
    if (sensorType >= 'A' && sensorType <= 'Z') {
      totalTemp += tempC;
    }
    Serial.print("Sensör Tipi: ");
    Serial.println(sensorType);
    Serial.print("Ham ADC: ");
    Serial.println(adcRaw);
    Serial.print("Filtrelenmiş mV: ");
    Serial.println(filteredmV, 4);
    Serial.print("Soğuk Bağlantı Sıcaklığı: ");
    Serial.print(tempC, 2);
    Serial.println(" °C");
    Serial.print("AVDD_AVSS ");
    Serial.println(adsvolt);
    temperature = totalTemp;
    if (!isnan(sensorTemp)) {
      Serial.print("Sensör Sıcaklığı: ");
      Serial.print(sensorTemp, 2);
      Serial.println(" °C");
      if (sensorType >= 'A' && sensorType <= 'Z') {
        Serial.print("Toplam Sıcaklık (CJ kompanzasyonlu): ");
        Serial.print(totalTemp, 2);
        Serial.println(" °C");
      }
    }
    Serial.println("-----------------------");
  }
  if ((sensorType >= 'A' && sensorType <= 'Z') && (now - lastTempMillis >= 10000)) {
    lastTempMillis = now;
    configureADS1220DiffAVDD_AVSS();
    startContinuousConversion();
    delay(100);
    adcRaw = readADS1220Data();
    adsvolt = (adcRaw * (VREF / 1.0)) / FULL_SCALE * 4.0;
    configureADS1220InternalTemp();
    startContinuousConversion();
    delay(100);
    int16_t temp14 = readADS1220Temp14Bit();
    tempC = convert14BitToTemp(temp14);
    if (tempC > 125.0) {
      Serial.println("UYARI: Yüksek soğuk bağlantı sıcaklığı!");
    }
    if (sensorType >= 'A' && sensorType <= 'Z') {
      configureADS1220DiffAIN1_AIN2();
    } else {
      switch (sensorType) {
        case '2': configureADS1220Pt2wire(); break;
        case '3': configureADS1220Pt3wire(); break;
        case '4': configureADS1220Pt4wire(); break;
        default: configureADS1220Pt2wire(); break;
      }
    }
    startContinuousConversion();
    delay(100);
  }
  if (wifiSettings.mode != 0 || forceAPMode == 1) {
    checkWiFiConnection();
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!platformRegistered) {
      registerWithPlatform();
    }
    if (platformRegistered && now - lastPlatformTelemetry >= platformTelemetryInterval) {
      lastPlatformTelemetry = now;
      sendPlatformTelemetry();
    }
    if (platformRegistered && now - lastPlatformCommandPoll >= platformCommandPollInterval) {
      lastPlatformCommandPoll = now;
      pollPlatformCommand();
    }
  } else {
    platformRegistered = false;
  }
  if (settings.alarmEnabled && temperature >= settings.alarmTemperature) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
  bool reading4 = digitalRead(BUTTON4_PIN);
  if (reading4 != button4PrevState) {
    lastDebounceTime4 = now;
  }
  if ((now - lastDebounceTime4) > debounceDelay) {
    if (reading4 != button4State) {
      button4State = reading4;
      if (button4State == LOW) {
        button4PressStartTime = now;
        button4LongPress = false;
      } else {
        if (!button4LongPress && (now - button4PressStartTime) < longPressTime) {
          toggleProgramState();
        }
        button4LongPress = false;
      }
    }
  }
  if (button4State == LOW && (now - button4PressStartTime) >= longPressTime) {
    button4LongPress = true;
  }
  button4PrevState = reading4;
  
  static unsigned long lastProgramCheck = 0;
  if (isRunning && (millis() - lastProgramCheck >= 100)) {
    lastProgramCheck = millis();
    runProgram();
  }
  
  if (autotuneRunning) {
    runAutotune();
  } else if (pidControlActive) {
    computePID();
  }
  if (now - previousMillis >= interval) {
    previousMillis = now;
    ss++;
    if (ss > 59) { ss = 0; mm++; }
    if (mm > 59) { mm = 0; hh++; }
    if (hh > 23) { hh = 0; }
    
    // Sıcaklık değiştiğinde güncelle
    if (!inMenu && fabs(temperature - temperatureGec) >= 0.1) {
      temperatureGec = temperature;
      displayTemperatureBig(temperature);
    }
    
    // Program bilgilerini güncelle (her saniye)
    if (!inMenu && !showingQRCode) {
      displayTime7Segment(hh, mm, ss);
    }
  }
}
