#include "painlessMesh.h"
#include <Arduino_JSON.h>
#include <SPI.h>
#include <EthernetENC.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ─────────────────────────────────────────────
//  MESH Configuration
// ─────────────────────────────────────────────
#define MESH_PREFIX     "AC_MESH"        // Must match sensor node
#define MESH_PASSWORD   "MESHpassword"
#define MESH_PORT       5555

// ─────────────────────────────────────────────
//  Ethernet Configuration
//  Assign a static IP that suits your network.
// ─────────────────────────────────────────────
byte mac[]         = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip       (192, 168, 0, 200);
IPAddress gateway  (192, 168, 0, 1);
IPAddress subnet   (255, 255, 255, 0);

// ─────────────────────────────────────────────
//  FreeRTOS Task Configuration
//  Ethernet runs on Core 0, Mesh on Core 1.
// ─────────────────────────────────────────────
#define ETHERNET_TASK_STACK 4096
#define TASK_PRIORITY       1

TaskHandle_t ethernetTaskHandle = NULL;
TaskHandle_t meshTaskHandle     = NULL;

EthernetServer server(80);

// ─────────────────────────────────────────────
//  Node Identity
// ─────────────────────────────────────────────
int nodeNumber = 1;  // Controller is always node 1

// ─────────────────────────────────────────────
//  AC State
// ─────────────────────────────────────────────
int    set_temp      = 25;           // °C  (16–30)
String ACmode        = "Cool";       // Cool | Dry | Fan | Auto
String ACPower       = "ON";         // ON | OFF
String ACVertical    = "OFF";        // ON | OFF
String ACHorizontal  = "OFF";        // ON | OFF
String ACFanspeed    = "High";       // Auto | High | Medium | Low

// ─────────────────────────────────────────────
//  Registered Sensor Nodes
//  Supports up to MAX_NODES sensor nodes.
// ─────────────────────────────────────────────
#define MAX_NODES 10
#define NODE_TIMEOUT_MS 5000   // 5 s heartbeat timeout

struct SensorNode {
  uint32_t chipID;
  String   location;
  double   temperature;
  double   humidity;
  unsigned long lastSeen;
  bool     registered;
};

SensorNode nodes[MAX_NODES];
int nodeCount = 0;

Preferences preferences;   // NVS storage for registered nodes

// ─────────────────────────────────────────────
//  Mesh Objects
// ─────────────────────────────────────────────
Scheduler    userScheduler;
painlessMesh mesh;

void sendMessage();
Task taskSendMessage(TASK_SECOND * 1, TASK_FOREVER, &sendMessage);

// ─────────────────────────────────────────────
//  NVS: Save & Load Registered Nodes
// ─────────────────────────────────────────────
void saveNodesToNVS() {
  preferences.begin("nodes", false);
  preferences.putInt("count", nodeCount);
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].registered) {
      preferences.putUInt(("id"  + String(i)).c_str(), nodes[i].chipID);
      preferences.putString(("loc" + String(i)).c_str(), nodes[i].location);
    }
  }
  preferences.end();
}

void loadNodesFromNVS() {
  preferences.begin("nodes", true);
  nodeCount = preferences.getInt("count", 0);
  for (int i = 0; i < nodeCount; i++) {
    nodes[i].chipID     = preferences.getUInt(("id"  + String(i)).c_str(), 0);
    nodes[i].location   = preferences.getString(("loc" + String(i)).c_str(), "Unknown");
    nodes[i].registered = true;
    nodes[i].lastSeen   = 0;
    nodes[i].temperature = 0;
    nodes[i].humidity    = 0;
  }
  preferences.end();
}

// ─────────────────────────────────────────────
//  Node Helpers
// ─────────────────────────────────────────────
int findNodeByID(uint32_t id) {
  for (int i = 0; i < nodeCount; i++)
    if (nodes[i].chipID == id) return i;
  return -1;
}

bool isNodeOnline(int idx) {
  return (millis() - nodes[idx].lastSeen) < NODE_TIMEOUT_MS;
}

// ─────────────────────────────────────────────
//  Broadcast AC Settings to All Sensor Nodes
// ─────────────────────────────────────────────
void broadcastACSettings() {
  JSONVar json;
  json["node"]         = nodeNumber;
  json["ACmode"]       = ACmode;
  json["ACPower"]      = ACPower;
  json["set_temp"]     = set_temp;
  json["ACFanspeed"]   = ACFanspeed;
  json["ACVertical"]   = ACVertical;
  json["ACHorizontal"] = ACHorizontal;
  mesh.sendBroadcast(JSON.stringify(json));
}

// ─────────────────────────────────────────────
//  HTML Web Dashboard
// ─────────────────────────────────────────────
String buildHTML(String availableNodes, String registeredNodes) {
  String html = R"rawhtml(
HTTP/1.1 200 OK
Content-Type: text/html
Connection: close

<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AC Controller</title>
  <style>
    body { font-family: Arial, sans-serif; background:#f4f6fb; margin:0; padding:20px; text-align:center; }
    h1   { margin-bottom:20px; }
    .grid { display:grid; grid-template-columns:1fr 1fr; gap:15px; max-width:900px; margin:auto; }
    .section { background:white; padding:15px; border-radius:10px; box-shadow:0 2px 6px rgba(0,0,0,.1); min-height:140px; }
    .section h2 { margin-top:0; font-size:18px; color:#333; }
    .btn { background:slateblue; color:white; border:none; padding:10px 14px; margin:5px;
           border-radius:6px; cursor:pointer; font-size:14px; min-width:120px; }
    .btn:hover  { background:darkslateblue; }
    .danger     { background:#e74c3c; }
    .danger:hover { background:#c0392b; }
    .success    { background:#27ae60; }
    .success:hover { background:#1e8449; }
    .status-online  { color:green; font-weight:bold; }
    .status-offline { color:red;   font-weight:bold; }
    .node-card { background:#eaffea; border:1px solid #b2dfb2; border-radius:8px; padding:12px; margin:8px auto; max-width:600px; }
    .node-card.offline { background:#fff0f0; border-color:#f5c6c6; }
    input[type=text] { padding:8px; border-radius:6px; border:1px solid #ccc; margin-right:8px; }
    .ac-status { background:white; border-radius:10px; padding:15px; max-width:900px; margin:20px auto; box-shadow:0 2px 6px rgba(0,0,0,.1); }
  </style>
</head>
<body>
  <h1>Aircon Control</h1>
  <div class="grid">
    <div class="section">
      <h2>Power</h2>
      <a href="/power/on"><button class="btn success">AC ON</button></a>
      <a href="/power/off"><button class="btn danger">AC OFF</button></a>
    </div>
    <div class="section">
      <h2>Mode</h2>
      <a href="/CoolMode"><button class="btn">Cool</button></a>
      <a href="/DryMode"><button class="btn">Dry</button></a>
      <a href="/FanMode"><button class="btn">Fan</button></a>
      <a href="/AutoMode"><button class="btn">Auto</button></a>
    </div>
    <div class="section">
      <h2>Fan Speed</h2>
      <a href="/Fanspeed/Auto"><button class="btn">Auto</button></a>
      <a href="/Fanspeed/High"><button class="btn">High</button></a>
      <a href="/Fanspeed/Medium"><button class="btn">Medium</button></a>
      <a href="/Fanspeed/Low"><button class="btn">Low</button></a>
    </div>
    <div class="section">
      <h2>Vertical Swing</h2>
      <a href="/VerticalSwing/on"><button class="btn">ON</button></a>
      <a href="/VerticalSwing/off"><button class="btn">OFF</button></a>
    </div>
    <div class="section">
      <h2>Horizontal Swing</h2>
      <a href="/HorizontalSwing/on"><button class="btn">ON</button></a>
      <a href="/HorizontalSwing/off"><button class="btn">OFF</button></a>
    </div>
    <div class="section">
      <h2>Temperature</h2>
      <a href="/Temperatureup"><button class="btn">UP</button></a>
      <a href="/Temperaturedown"><button class="btn">DOWN</button></a>
    </div>
  </div>
)rawhtml";

  html += "<h2>Available Nodes</h2>" + availableNodes;
  html += "<h2>Registered Nodes</h2>" + registeredNodes;

  html += "<div class='ac-status'><h2>AC Status</h2><p>";
  html += "Set Temp: " + String(set_temp) + " &deg;C, ";
  html += "Mode: " + ACmode + ", ";
  html += "Power: " + ACPower + ", ";
  html += "Fan: " + ACFanspeed + ", ";
  html += "Vertical Swing: " + ACVertical + ", ";
  html += "Horizontal Swing: " + ACHorizontal;
  html += "</p></div></body></html>";
  return html;
}

// ─────────────────────────────────────────────
//  Ethernet / HTTP Task  (Core 0)
// ─────────────────────────────────────────────
void ethernetTask(void *parameter) {
  Serial.println("Ethernet Task — Core " + String(xPortGetCoreID()));

  SPI.begin();
  Ethernet.init(5);  // CS pin for ENC28J60
  Ethernet.begin(mac, ip, gateway, gateway, subnet);

  Serial.print("Ethernet IP: ");
  Serial.println(Ethernet.localIP());
  server.begin();
  Serial.println("HTTP Server started on port 80");

  while (1) {
    EthernetClient client = server.accept();
    if (client) {
      String request = "";
      unsigned long timeout = millis() + 2000;

      while (client.connected() && millis() < timeout) {
        if (client.available()) {
          char c = client.read();
          request += c;
          if (request.endsWith("\r\n\r\n")) break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
      }

      if (request.length() > 0) {
        // ── AC Control Endpoints ──────────────────
        if      (request.indexOf("GET /power/on")           >= 0) ACPower = "ON";
        else if (request.indexOf("GET /power/off")          >= 0) ACPower = "OFF";
        else if (request.indexOf("GET /CoolMode")           >= 0) ACmode  = "Cool";
        else if (request.indexOf("GET /DryMode")            >= 0) ACmode  = "Dry";
        else if (request.indexOf("GET /FanMode")            >= 0) ACmode  = "Fan";
        else if (request.indexOf("GET /AutoMode")           >= 0) ACmode  = "Auto";
        else if (request.indexOf("GET /Fanspeed/Auto")      >= 0) ACFanspeed = "Auto";
        else if (request.indexOf("GET /Fanspeed/High")      >= 0) ACFanspeed = "High";
        else if (request.indexOf("GET /Fanspeed/Medium")    >= 0) ACFanspeed = "Medium";
        else if (request.indexOf("GET /Fanspeed/Low")       >= 0) ACFanspeed = "Low";
        else if (request.indexOf("GET /VerticalSwing/on")   >= 0) ACVertical  = "ON";
        else if (request.indexOf("GET /VerticalSwing/off")  >= 0) ACVertical  = "OFF";
        else if (request.indexOf("GET /HorizontalSwing/on") >= 0) ACHorizontal = "ON";
        else if (request.indexOf("GET /HorizontalSwing/off")>= 0) ACHorizontal = "OFF";
        else if (request.indexOf("GET /Temperatureup")      >= 0) { set_temp++; if (set_temp > 30) set_temp = 30; }
        else if (request.indexOf("GET /Temperaturedown")    >= 0) { set_temp--; if (set_temp < 16) set_temp = 16; }

        // ── Node Registration ─────────────────────
        // URL: /register?id=<chipID>&loc=<location>
        else if (request.indexOf("GET /register") >= 0) {
          int idPos  = request.indexOf("id=");
          int locPos = request.indexOf("loc=");
          if (idPos >= 0 && locPos >= 0) {
            uint32_t chipID = strtoul(request.substring(idPos + 3).c_str(), NULL, 10);
            String loc = request.substring(locPos + 4);
            loc = loc.substring(0, loc.indexOf(' '));
            loc.replace("%20", " ");

            int idx = findNodeByID(chipID);
            if (idx >= 0) {
              nodes[idx].location   = loc;
              nodes[idx].registered = true;
            } else if (nodeCount < MAX_NODES) {
              nodes[nodeCount].chipID     = chipID;
              nodes[nodeCount].location   = loc;
              nodes[nodeCount].registered = true;
              nodes[nodeCount].lastSeen   = 0;
              nodeCount++;
            }
            saveNodesToNVS();
          }
        }

        // ── Node Unregister ───────────────────────
        // URL: /unregister?id=<chipID>
        else if (request.indexOf("GET /unregister") >= 0) {
          int idPos = request.indexOf("id=");
          if (idPos >= 0) {
            uint32_t chipID = strtoul(request.substring(idPos + 3).c_str(), NULL, 10);
            int idx = findNodeByID(chipID);
            if (idx >= 0) {
              // Shift array
              for (int i = idx; i < nodeCount - 1; i++) nodes[i] = nodes[i + 1];
              nodeCount--;
              saveNodesToNVS();
            }
          }
        }

        // Broadcast updated AC settings to all sensor nodes
        broadcastACSettings();

        // ── Build Available Nodes HTML ────────────
        // Shows nodes that are seen on mesh but not yet registered
        String availHTML = "";
        // (Discovered via receivedCallback — add UI row per unregistered node)

        // ── Build Registered Nodes HTML ───────────
        String regHTML = "";
        for (int i = 0; i < nodeCount; i++) {
          if (!nodes[i].registered) continue;
          bool online = isNodeOnline(i);
          String cardClass = online ? "node-card" : "node-card offline";
          String statusStr = online
            ? "<span class='status-online'>ONLINE</span>"
            : "<span class='status-offline'>OFFLINE</span>";

          regHTML += "<div class='" + cardClass + "'>";
          regHTML += "<b>" + nodes[i].location + "</b> &nbsp; ID: " + String(nodes[i].chipID);
          regHTML += " &nbsp; Status: " + statusStr;
          if (nodes[i].lastSeen > 0) {
            regHTML += " &nbsp; Temp: " + String(nodes[i].temperature, 2) + " &deg;C";
            regHTML += " &nbsp; Humidity: " + String(nodes[i].humidity, 2) + " %";
          }
          regHTML += " &nbsp; <a href='/unregister?id=" + String(nodes[i].chipID) + "'>";
          regHTML += "<button class='btn danger' style='min-width:80px;font-size:12px;'>Unregister</button></a>";
          regHTML += "</div>";
        }

        client.print(buildHTML(availHTML, regHTML));
      }

      vTaskDelay(10 / portTICK_PERIOD_MS);
      client.stop();
    }

    // Ethernet maintenance every 30 s
    static unsigned long lastMaintain = 0;
    if (millis() - lastMaintain > 30000) {
      Ethernet.maintain();
      lastMaintain = millis();
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ─────────────────────────────────────────────
//  Mesh Task  (Core 1)
// ─────────────────────────────────────────────
void meshTask(void *parameter) {
  Serial.println("Mesh Task — Core " + String(xPortGetCoreID()));

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);

  userScheduler.addTask(taskSendMessage);
  taskSendMessage.enable();

  while (1) {
    mesh.update();
    userScheduler.execute();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void sendMessage() {
  broadcastACSettings();
  taskSendMessage.setInterval(random(TASK_SECOND * 1, TASK_SECOND * 3));
}

// ─────────────────────────────────────────────
//  Mesh Callbacks
// ─────────────────────────────────────────────
void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Received from %u: %s\n", from, msg.c_str());
  JSONVar obj = JSON.parse(msg.c_str());
  if (JSON.typeof(obj) == "undefined") return;

  uint32_t chipID = (uint32_t)(int) obj["node"];  // sensor nodes send their nodeNumber
  // In production replace with actual chip ID field from sensor node payload

  double temp = (double) obj["temp"];
  double hum  = (double) obj["hum"];

  int idx = findNodeByID(from);  // use mesh node ID as key
  if (idx >= 0) {
    nodes[idx].temperature = temp;
    nodes[idx].humidity    = hum;
    nodes[idx].lastSeen    = millis();
  }
  // If not registered, node appears under "Available Nodes" on next page load
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("New mesh connection: %u\n", nodeId);
}

void changedConnectionCallback() {
  Serial.println("Mesh connections changed");
}

// ─────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  loadNodesFromNVS();
  Serial.printf("Loaded %d registered nodes from storage\n", nodeCount);

  // Ethernet task → Core 0
  xTaskCreatePinnedToCore(ethernetTask, "EthernetTask",
    ETHERNET_TASK_STACK, NULL, TASK_PRIORITY, &ethernetTaskHandle, 0);

  // Mesh task → Core 1
  xTaskCreatePinnedToCore(meshTask, "MeshTask",
    ETHERNET_TASK_STACK, NULL, TASK_PRIORITY, &meshTaskHandle, 1);

  Serial.println("System running — Ethernet: Core 0 | Mesh: Core 1");
}

void loop() {
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// ─────────────────────────────────────────────
//  TODO (Planned Features)
//  - OTA firmware updates (ElegantOTA / ArduinoOTA)
//  - Grafana / InfluxDB data push via HTTP POST
//  - CT sensor current reading per node
// ─────────────────────────────────────────────
