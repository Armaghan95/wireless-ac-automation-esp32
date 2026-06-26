# Wireless-ac-automation-via-mesh-networking-esp32
This project implements a complete wireless AC automation and environmental monitoring system built around two ESP32 nodes communicating over a self-forming painlessMesh WiFi network. It was developed as a practical prototyping exercise covering embedded firmware, RF mesh networking, IR protocol transmission, PCB design, and embedded web server development — all integrated into a single end-to-end system.

The Controller Node runs on an ESP32 with a W5500 / ENC28J60 Ethernet module connected to an existing network switch via SPI. It uses FreeRTOS dual-core tasking — the Ethernet HTTP server runs on Core 0 while the mesh network handler runs on Core 1 — ensuring neither task blocks the other. It hosts a responsive browser-based control dashboard accessible from any device on the local network at a static IP, with no app, no cloud, and no internet dependency.
The dashboard provides full AC control including power, operating mode (Cool / Dry / Fan / Auto), fan speed (Auto / High / Medium / Low), vertical and horizontal swing, and set-point temperature adjustment between 16–30 °C. It also handles sensor node registration — each Sensor Node is identified by its unique ESP32 Chip ID, assigned a custom location label by the operator, and stored permanently in the Controller's NVS (non-volatile storage) so registrations survive power cycles.
Registered nodes are displayed on the dashboard with live temperature, humidity, and online/offline status. Status is determined by a 5-second heartbeat timeout — if no broadcast is received from a node within that window, it is automatically flagged OFFLINE with its last known readings retained on screen.

The Sensor Node runs on an ESP32-S3-WROOM with an external WiFi antenna for improved mesh range. It reads temperature and humidity from a DHT22 sensor and broadcasts this data over the mesh every 1–3 seconds. When it receives an AC settings packet from the Controller Node, it immediately fires the corresponding IR signal at the AC unit using an IRremoteESP8266-driven TSAL6100 IR LED with an AO3400A MOSFET driver circuit. The IR protocol used is Coolix / HVAC, which is configurable in firmware to match any AC brand supported by the IRremoteESP8266 library.

The custom two-layer PCB integrates the full Sensor Node in a single compact board: ESP32-S3-WROOM, IR LED driver, DHT22 header, CT sensor connector (for planned power monitoring), WS2812 RGB status LED, user button, SMA antenna connector, and a MEAN WELL IRM-02-3.3 AC-DC module accepting 85–264 VAC mains input and outputting 3.3 VDC — making the unit self-powered directly from a standard socket with no separate power adapter.

Planned features in active development:

Over-the-Air (OTA) firmware updates for both nodes
CT sensor integration for AC current and power consumption monitoring
Grafana / InfluxDB dashboard for centralised multi-room monitoring and historical data logging
HTTPS and basic authentication for the web control panel

Scalability: each Controller Node supports up to 2 Sensor Nodes, making the system straightforward to expand across multiple rooms from a single network cabinet.

Core technologies: ESP32 · ESP32-S3 · FreeRTOS · painlessMesh · IRremoteESP8266 · EthernetENC · DHT22 · NVS (Preferences) · Arduino IDE · PCB design · Mains power supply design

That covers everything a recruiter, engineer, or hiring manager would want to understand about the project at a glance. Want me to also write the LinkedIn Projects section entry separately from the post? That's a different, shorter format that stays permanently on your profile.
