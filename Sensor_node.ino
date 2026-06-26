#include "DHT.h"
#include "painlessMesh.h"
#include <Arduino_JSON.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRutils.h>
#include <Preferences.h>

// ─────────────────────────────────────────────
//  MESH Configuration
// ─────────────────────────────────────────────
#define MESH_PREFIX     "AC_MESH"        // Change to your mesh name
#define MESH_PASSWORD   "MESHpassword"   // Change to your mesh password
#define MESH_PORT       5555

// ─────────────────────────────────────────────
//  Hardware Pin Definitions
// ─────────────────────────────────────────────
#define DHTPIN    4    // DHT22 data pin
#define DHTTYPE   DHT22
#define IR_LED_PIN 2   // IR transmitter pin

DHT dht(DHTPIN, DHTTYPE);
IRac ac(IR_LED_PIN);
Preferences preferences;

// ─────────────────────────────────────────────
//  Node Identity
//  Each sensor node has a unique nodeNumber.
//  Set this to 2, 3, 4 ... for each unit.
// ─────────────────────────────────────────────
int nodeNumber = 2;
String nodeLocation = "Room A";   // Updated by controller on registration

// ─────────────────────────────────────────────
//  AC State (received from Controller Node)
// ─────────────────────────────────────────────
double set_temp    = 25;
String ACmode      = "Cool";
String ACPower     = "ON";
String ACVertical  = "OFF";
String ACHorizontal= "OFF";
String ACFanspeed  = "High";
bool   acSwingV    = false;
bool   acSwingH    = false;

// ─────────────────────────────────────────────
//  Mesh & Scheduler
// ─────────────────────────────────────────────
Scheduler    userScheduler;
painlessMesh mesh;

void sendMessage();
String getReadings();

// Broadcast sensor data every 1–3 seconds
Task taskSendMessage(TASK_SECOND * 1, TASK_FOREVER, &sendMessage);

// ─────────────────────────────────────────────
//  AC Initialisation  (Coolix / HVAC protocol)
//  Replace decode_type_t::COOLIX with the
//  protocol that matches your AC brand.
// ─────────────────────────────────────────────
void setupAC() {
  ac.next.protocol  = decode_type_t::COOLIX;
  ac.next.model     = 1;
  ac.next.mode      = stdAc::opmode_t::kCool;
  ac.next.celsius   = true;
  ac.next.degrees   = 25;
  ac.next.fanspeed  = stdAc::fanspeed_t::kMedium;
  ac.next.swingv    = stdAc::swingv_t::kOff;
  ac.next.swingh    = stdAc::swingh_t::kOff;
  ac.next.light     = false;
  ac.next.beep      = false;
  ac.next.econo     = false;
  ac.next.filter    = false;
  ac.next.turbo     = false;
  ac.next.quiet     = false;
  ac.next.clean     = false;
  ac.next.sleep     = -1;
  ac.next.clock     = -1;
  ac.next.power     = false;
  Serial.println("AC IR Controller initialised");
}

// ─────────────────────────────────────────────
//  Apply AC Settings via IR
// ─────────────────────────────────────────────
void executeAC() {
  // Power
  ac.next.power = (ACPower == "ON");

  // Mode
  if      (ACmode == "Cool") ac.next.mode = stdAc::opmode_t::kCool;
  else if (ACmode == "Dry")  ac.next.mode = stdAc::opmode_t::kDry;
  else if (ACmode == "Fan")  ac.next.mode = stdAc::opmode_t::kFan;
  else if (ACmode == "Auto") ac.next.mode = stdAc::opmode_t::kAuto;

  // Fan speed
  if      (ACFanspeed == "Auto")   ac.next.fanspeed = stdAc::fanspeed_t::kAuto;
  else if (ACFanspeed == "Low")    ac.next.fanspeed = stdAc::fanspeed_t::kLow;
  else if (ACFanspeed == "Medium") ac.next.fanspeed = stdAc::fanspeed_t::kMedium;
  else if (ACFanspeed == "High")   ac.next.fanspeed = stdAc::fanspeed_t::kHigh;

  // Vertical swing
  ac.next.swingv = (ACVertical == "ON")
                   ? stdAc::swingv_t::kAuto
                   : stdAc::swingv_t::kOff;

  // Horizontal swing (uncomment swingh lines if your AC supports it)
  // ac.next.swingh = (ACHorizontal == "ON")
  //                  ? stdAc::swingh_t::kAuto
  //                  : stdAc::swingh_t::kOff;

  ac.next.degrees = set_temp;
  ac.sendAc();

  Serial.printf("AC → Power:%s Mode:%s Fan:%s Temp:%.0f VSwing:%s HSwing:%s\n",
    ACPower.c_str(), ACmode.c_str(), ACFanspeed.c_str(),
    set_temp, ACVertical.c_str(), ACHorizontal.c_str());
}

// ─────────────────────────────────────────────
//  Sensor Reading  (JSON payload → Controller)
// ─────────────────────────────────────────────
String getReadings() {
  JSONVar jsonReadings;
  jsonReadings["node"]     = nodeNumber;
  jsonReadings["temp"]     = dht.readTemperature();
  jsonReadings["hum"]      = dht.readHumidity();
  jsonReadings["location"] = nodeLocation;
  return JSON.stringify(jsonReadings);
}

void sendMessage() {
  mesh.sendBroadcast(getReadings());
}

// ─────────────────────────────────────────────
//  Mesh Callbacks
// ─────────────────────────────────────────────
void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Received from %u: %s\n", from, msg.c_str());
  JSONVar obj = JSON.parse(msg.c_str());

  // Only process messages from Controller Node (node 1)
  if ((int)obj["node"] == 1) {
    ACmode       = (const char*) obj["ACmode"];
    ACPower      = (const char*) obj["ACPower"];
    ACFanspeed   = (const char*) obj["ACFanspeed"];
    ACVertical   = (const char*) obj["ACVertical"];
    ACHorizontal = (const char*) obj["ACHorizontal"];
    set_temp     = (int)         obj["set_temp"];
    executeAC();
  }
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("New connection: %u\n", nodeId);
}

void changedConnectionCallback() {
  Serial.println("Mesh connections changed");
}

void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.printf("Time adjusted. Offset = %d\n", offset);
}

// ─────────────────────────────────────────────
//  Setup & Loop
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  dht.begin();
  setupAC();

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  userScheduler.addTask(taskSendMessage);
  taskSendMessage.enable();
}

void loop() {
  mesh.update();
}

// ─────────────────────────────────────────────
//  TODO (Planned Features)
//  - OTA firmware update (ArduinoOTA or ElegantOTA)
//  - CT sensor reading for AC current monitoring
//    (GPIO48/CT pin → ADC → RMS calculation)
// ─────────────────────────────────────────────
