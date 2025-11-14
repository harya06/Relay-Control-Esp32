#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WebSocketsClient.h>
#include <PCF8574.h>
#include <LiquidCrystal_I2C.h>

// ==================== ALAMAT I2C ====================
#define ADDR_PCF1 0x20
#define ADDR_PCF2 0x24
#define ADDR_LCD 0x27

// ==================== KONFIGURASI ====================
#define TOTAL_OUTPUTS 20
#define CONFIG_FILE "/config.json"
#define AP_SSID "ESP32-Control"
#define AP_PASSWORD "12345678"

// ==================== ENUMS ====================
enum IOType
{
  IO_ESP,
  IO_PCF1,
  IO_PCF2
};

enum CommMode
{
  MODE_MQTT = 0,
  MODE_WEBSOCKET = 1
};

// ==================== STRUCTS ====================
struct ChannelMap
{
  IOType type;
  uint8_t pin;
};

struct OutputChannel
{
  String name;
  bool state;
  unsigned long intervalOn;
  unsigned long intervalOff;
  unsigned long lastToggle;
  bool autoMode;
  int maxToggles;
  int currentToggles;
};

struct Config
{
  String wifiSSID;
  String wifiPassword;
  String serverIP;
  int serverPort;
  String serverPath;
  String serverToken;
  String webUsername;
  String webPassword;
  CommMode commMode;
};

// ==================== SYNC GROUP SYSTEM ====================
struct SyncGroup
{
  unsigned long intervalOn;
  unsigned long intervalOff;
  unsigned long lastToggle;
  bool currentState;
  int memberCount;
};

// ==================== GLOBAL OBJECTS ====================
SyncGroup syncGroups[5];           // Maksimal 5 grup berbeda
int outputGroupMap[TOTAL_OUTPUTS]; // Map output ke grup mana
int activeSyncGroups = 0;

ChannelMap chMap[TOTAL_OUTPUTS + 1];
OutputChannel outputs[TOTAL_OUTPUTS];
Config config;

PCF8574 pcf1(ADDR_PCF1);
PCF8574 pcf2(ADDR_PCF2);
LiquidCrystal_I2C lcd(ADDR_LCD, 16, 2);
WebServer server(80);

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebSocketsClient wsClient;

// ==================== GLOBAL VARIABLES ====================
bool wifiConnected = false;
bool remoteConnected = false;
bool modeSwitching = false;
bool lcdNeedsRedraw = true;
bool isRemoteReconnecting = false;

unsigned long lastRemotePublish = 0;
unsigned long lastRemoteReconnect = 0;
unsigned long remoteDisconnectTime = 0;
unsigned long lastLcdPageSwap = 0;

int lcdOutputPage = 0;

#define LCD_PAGES 5
#define LCD_PAGE_SWAP_MS 2000 
#define REMOTE_RECONNECT_TIMEOUT 15000

// ========================== PIN I/O ==========================
#define PIN_IO_ESP1 4
#define PIN_IO_ESP2 17
#define PIN_IO_ESP11 25
#define PIN_IO_ESP12 32

// ==================== FORWARD DECLARATIONS ====================
void updateLCD();
bool saveConfig();
void switchMode(CommMode newMode, bool saveToConfig = true);
void mqttPublish();
void wsPublish();
String getStatusJSON();
void processCommand(String command, String source);
void rebuildSyncGroups();
void processSyncGroups();

// ==================== CHANNEL MAPPING ====================
void initChannelMap()
{
  chMap[1] = {IO_ESP, PIN_IO_ESP1};
  chMap[2] = {IO_ESP, PIN_IO_ESP2};

  for (uint8_t i = 0; i < 8; i++)
  {
    chMap[i + 3] = {IO_PCF1, i};
  }

  chMap[11] = {IO_ESP, PIN_IO_ESP11};
  chMap[12] = {IO_ESP, PIN_IO_ESP12};

  for (uint8_t i = 0; i < 8; i++)
  {
    chMap[i + 13] = {IO_PCF2, i};
  }
}

// ==================== HARDWARE CONTROL ====================
void initHardwarePins()
{
  pinMode(PIN_IO_ESP1, OUTPUT);
  pinMode(PIN_IO_ESP2, OUTPUT);
  pinMode(PIN_IO_ESP11, OUTPUT);
  pinMode(PIN_IO_ESP12, OUTPUT);

  digitalWrite(PIN_IO_ESP1, LOW); 
  digitalWrite(PIN_IO_ESP2, LOW);
  digitalWrite(PIN_IO_ESP11, HIGH);
  digitalWrite(PIN_IO_ESP12, HIGH);

  // Init PCF8574 #1
  Serial.println("Initializing PCF1 pins...");
  for (int i = 0; i < 8; i++)
  {
    pcf1.pinMode(i, OUTPUT);
    delay(5);                   // Small delay
    pcf1.digitalWrite(i, HIGH); // HIGH = Relay OFF (inverted)
    delay(5);
    Serial.printf("  PCF1 P%d set to OUTPUT, HIGH\n", i);
  }

  // Init PCF8574 #2
  Serial.println("Initializing PCF2 pins...");
  for (int i = 0; i < 8; i++)
  {
    pcf2.pinMode(i, OUTPUT);
    delay(5);
    pcf2.digitalWrite(i, HIGH); // HIGH = Relay OFF (inverted)
    delay(5);
    Serial.printf("  PCF2 P%d set to OUTPUT, HIGH\n", i);
  }

  Serial.println("Hardware pins initialized");
}

void setOutput(int channel, bool state)
{
  if (channel < 1 || channel > TOTAL_OUTPUTS)
  {
    Serial.printf("Error: Invalid channel %d\n", channel);
    return;
  }

  int outputIndex = channel - 1;

  if (outputs[outputIndex].state == state)
  {
    Serial.printf("CH%02d: Sudah di state %s, tidak ada perpindahan.\n", channel, state ? "ON" : "OFF");
    return;
  }

  if (outputs[outputIndex].maxToggles > 0)
  {
    if (outputs[outputIndex].currentToggles >= outputs[outputIndex].maxToggles)
    {
      Serial.printf("CH%02d: GAGAL! Batasan perpindahan (%d) telah tercapai.\n", channel, outputs[outputIndex].maxToggles);
      return;
    }
  }

  ChannelMap ch = chMap[channel];

  bool writeSuccess = false;

  switch (ch.type)
  {
  case IO_ESP:
  {
    if (ch.pin == PIN_IO_ESP1 || ch.pin == PIN_IO_ESP2)
        {
          digitalWrite(ch.pin, state ? HIGH : LOW);
          Serial.printf("CH%02d: ESP GPIO%02d = %s (Normal)\n", channel, ch.pin, state ? "HIGH" : "LOW");
          writeSuccess = true;
        }
        else if (ch.pin == PIN_IO_ESP11 || ch.pin == PIN_IO_ESP12)
        {
          digitalWrite(ch.pin, state ? LOW : HIGH);
          Serial.printf("CH%02d: ESP GPIO%02d = %s (Inverted)\n", channel, ch.pin, state ? "LOW" : "HIGH");
          writeSuccess = true;  
        }
        else
        {
          writeSuccess = false;
        }
        break;
  }

  case IO_PCF1:
  {
    // PCF8574 #1 - INVERTED logic untuk relay (LOW = ON)
    pcf1.digitalWrite(ch.pin, state ? LOW : HIGH);

    // Verify write
    uint8_t readback = pcf1.digitalRead(ch.pin);
    writeSuccess = (readback == (state ? LOW : HIGH));

    Serial.printf("CH%02d: PCF1(0x%02X) P%d = %s (inverted) [%s]\n",
                  channel, ADDR_PCF1, ch.pin,
                  state ? "LOW" : "HIGH",
                  writeSuccess ? "OK" : "FAIL");
    break;
  }

  case IO_PCF2:
  {
    // PCF8574 #2 - INVERTED logic untuk relay (LOW = ON)
    pcf2.digitalWrite(ch.pin, state ? LOW : HIGH);

    // Verify write
    uint8_t readback2 = pcf2.digitalRead(ch.pin);
    writeSuccess = (readback2 == (state ? LOW : HIGH));

    Serial.printf("CH%02d: PCF2(0x%02X) P%d = %s (inverted) [%s]\n",
                  channel, ADDR_PCF2, ch.pin,
                  state ? "LOW" : "HIGH",
                  writeSuccess ? "OK" : "FAIL");
    break;
  }
  }

  if (writeSuccess)
  {
    outputs[outputIndex].state = state;
    outputs[outputIndex].lastToggle = millis();
    outputs[outputIndex].currentToggles++;

    lcdNeedsRedraw = true;
    lcdOutputPage = 0;
    lastLcdPageSwap = millis();

    Serial.printf("CH%02d: Perpindahan ke %d. Meteran: %d / %d\n", 
                  channel, state, outputs[outputIndex].currentToggles, outputs[outputIndex].maxToggles);
  }
  else
  {
    Serial.printf("Failed to set CH%02d!\n", channel);
  }
}

void initOutputs()
{
  for (int i = 0; i < TOTAL_OUTPUTS; i++)
  {
    outputs[i].name = "Channel " + String(i + 1);
    outputs[i].state = false;
    outputs[i].intervalOn = 5000;
    outputs[i].intervalOff = 5000;
    outputs[i].lastToggle = 0;
    outputs[i].autoMode = false;
  }
}

// ==================== CONFIG MANAGEMENT ====================
bool loadConfig()
{
  // ======== FORCE WEBSOCKET MODE ========
  if (LittleFS.exists(CONFIG_FILE))
  {
    Serial.println("Found existing config.json");

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (file)
    {
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, file);
      file.close();

      // if (!error)
      // {
      //   int mode = doc["commMode"] | 1;

      //   if (mode == 0)
      //   {
      //     Serial.println("OLD CONFIG WITH MQTT MODE DETECTED!");
      //     Serial.println("Deleting old config...");
      //     LittleFS.remove(CONFIG_FILE);
      //   }
      // }
    }
  }
  // =====================================

  if (!LittleFS.exists(CONFIG_FILE))
  {
    Serial.println("Config file tidak ditemukan, menggunakan default");

    // Set default config
    config.wifiSSID = "";
    config.wifiPassword = "";
    config.serverIP = "";
    config.serverPort;
    config.serverPath = "";
    config.serverToken = "";
    config.webUsername = "admin";
    config.webPassword = "admin123";
    config.commMode = MODE_WEBSOCKET;

    Serial.println("   Default credentials set:");
    Serial.println("   Username: admin");
    Serial.println("   Password: admin123");
    Serial.println("   Mode: WebSocket (1)");

    // SAVE default config
    saveConfig();

    return false;
  }

  // File exists, try to load
  File file = LittleFS.open(CONFIG_FILE, "r");
  if (!file)
  {
    Serial.println("Failed to open config file");

    // Set default
    config.wifiSSID = "";
    config.wifiPassword = "";
    config.serverIP = "";
    config.serverPort;
    config.serverPath = "";
    config.serverToken = "";
    config.webUsername = "admin";
    config.webPassword = "admin123";
    config.commMode = MODE_WEBSOCKET;
    saveConfig();

    return false;
  }

  String fileContent = file.readString();
  file.close();

  Serial.println("Config file content:");
  Serial.println(fileContent);
  Serial.println("---");

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, fileContent);

  if (error)
  {
    Serial.println("JSON Parse Error: " + String(error.c_str()));
    Serial.println("Config file corrupt! Deleting and using default...");

    // Delete corrupt file
    LittleFS.remove(CONFIG_FILE);

    // Set default
    config.wifiSSID = "";
    config.wifiPassword = "";
    config.serverIP = "";
    config.serverPort;
    config.serverPath = "";
    config.serverToken = "";
    config.webUsername = "admin";
    config.webPassword = "admin123";
    config.commMode = MODE_WEBSOCKET;

    // Save default
    saveConfig();

    Serial.println("   Default config created");
    Serial.println("   Username: admin");
    Serial.println("   Password: admin123");
    Serial.println("   Mode: WebSocket");

    return false;
  }

  // Parse berhasil
  config.wifiSSID = doc["wifiSSID"] | "";
  config.wifiPassword = doc["wifiPassword"] | "";
  config.serverIP = doc["serverIP"] | "";
  config.serverPort = doc["serverPort"];
  config.serverPath = doc["serverPath"] | "/";
  config.serverToken = doc["serverToken"] | "";
  config.webUsername = doc["webUsername"] | "admin";
  config.webPassword = doc["webPassword"] | "admin123";

  if (doc.containsKey("commMode"))
  {
    config.commMode = (CommMode)(doc["commMode"].as<int>());
  }
  else
  {
    Serial.println("commMode not found in config, using WebSocket");
    config.commMode = MODE_WEBSOCKET;
  }

  // Trim whitespace
  config.wifiSSID.trim();
  config.wifiPassword.trim();
  config.serverIP.trim();
  config.serverPath.trim();
  config.serverToken.trim();
  config.webUsername.trim();
  config.webPassword.trim();

  Serial.println("   Config loaded successfully");
  Serial.println("   Login credentials:");
  Serial.println("   Username: '" + config.webUsername + "'");
  Serial.println("   Password: '" + config.webPassword + "'");
  Serial.printf("   Mode: %s (%d)\n", config.commMode == MODE_MQTT ? "MQTT" : "WebSocket", (int)config.commMode);

  return true;
}

bool saveConfig()
{
  StaticJsonDocument<1024> doc;

  doc["wifiSSID"] = config.wifiSSID;
  doc["wifiPassword"] = config.wifiPassword;
  doc["serverIP"] = config.serverIP;
  doc["serverPort"] = config.serverPort;
  doc["serverPath"] = config.serverPath;
  doc["serverToken"] = config.serverToken;
  doc["webUsername"] = config.webUsername;
  doc["webPassword"] = config.webPassword;
  doc["commMode"] = (int)config.commMode;

  File file = LittleFS.open(CONFIG_FILE, "w");
  if (!file)
    return false;

  serializeJson(doc, file);
  file.close();

  Serial.println("Config saved");
  return true;
}

// ==================== LCD ====================
void updateLCD()
{
  lcd.clear();
  if (remoteConnected)
  {
    int startOutput = (lcdOutputPage * 4) + 1; 
    
    lcd.setCursor(0, 0);
    for (int i = 0; i < 2; i++) {
      int ch = startOutput + i;
      if (ch <= TOTAL_OUTPUTS) {
        char buf[8];
        sprintf(buf, "Q%d:%d, ", ch, outputs[ch-1].state ? 1 : 0);
        lcd.print(buf);
      }
    }
    
    lcd.setCursor(0, 1);
    for (int i = 2; i < 4; i++) {
      int ch = startOutput + i;
      if (ch <= TOTAL_OUTPUTS) {
        char buf[8]; 
        sprintf(buf, "Q%d:%d, ", ch, outputs[ch-1].state ? 1 : 0);
        lcd.print(buf);
      }
    }
  }
  else
  {
    lcd.setCursor(0, 0);
    if (wifiConnected)
    {
      lcd.print("IP:");
      lcd.print(WiFi.localIP().toString());
    }
    else
    {
      lcd.print("AP:");
      lcd.print(WiFi.softAPIP().toString());
    }
    lcd.setCursor(0, 1);
    String mode = (config.commMode == MODE_MQTT) ? "MQTT" : "WS";
    lcd.print(mode + ": ");
    lcd.print("NOT CONNECTED");
  }
}

// ==================== MODE SWITCHING ====================
void switchMode(CommMode newMode, bool saveToConfig)
{
  if (config.commMode == newMode && !modeSwitching)
  {
    Serial.println("Already in this mode");
    return;
  }

  modeSwitching = true;

  Serial.printf("\n Switching mode: %s → %s\n",
                config.commMode == MODE_MQTT ? "MQTT" : "WebSocket",
                newMode == MODE_MQTT ? "MQTT" : "WebSocket");

  // Disconnect current mode
  if (config.commMode == MODE_MQTT)
  {
    if (mqttClient.connected())
    {
      Serial.println("Disconnecting MQTT...");
      mqttClient.disconnect();
    }
  }
  else
  {
    if (wsClient.isConnected())
    {
      Serial.println("Disconnecting WebSocket...");
      wsClient.disconnect();
    }
  }

  remoteConnected = false;

  // Update config
  config.commMode = newMode;

  if (saveToConfig)
  {
    saveConfig();
  }

  // Setup new mode
  if (config.commMode == MODE_MQTT)
  {
    Serial.println("Setting up MQTT...");
    if (wifiConnected && config.serverIP.length() > 0)
    {
      mqttClient.setServer(config.serverIP.c_str(), config.serverPort);
    }
  }
  else
  {
    Serial.println("Setting up WebSocket...");
    if (wifiConnected && config.serverIP.length() > 0)
    {
      wsClient.begin(config.serverIP.c_str(), config.serverPort, config.serverPath.c_str());
      wsClient.setReconnectInterval(5000);
    }
  }

  updateLCD();
  modeSwitching = false;

  Serial.println("Mode switched successfully\n");
}

// ==================== JSON HELPERS ====================
String getStatusJSON()
{
  StaticJsonDocument<3072> doc;
  JsonArray arr = doc.createNestedArray("outputs");

  for (int i = 0; i < TOTAL_OUTPUTS; i++)
  {
    JsonObject obj = arr.createNestedObject();
    obj["id"] = i;
    obj["channel"] = i + 1;
    obj["name"] = outputs[i].name;
    obj["state"] = outputs[i].state;
    obj["intervalOn"] = outputs[i].intervalOn / 1000;
    obj["intervalOff"] = outputs[i].intervalOff / 1000;
    obj["autoMode"] = outputs[i].autoMode;
    obj["maxToggles"] = outputs[i].maxToggles;
    obj["currentToggles"] = outputs[i].currentToggles;

  }

  doc["wifiConnected"] = wifiConnected;
  doc["remoteConnected"] = remoteConnected;
  doc["commMode"] = (int)config.commMode;
  doc["modeName"] = config.commMode == MODE_WEBSOCKET ? "MQTT" : "WebSocket";
  doc["totalOutputs"] = TOTAL_OUTPUTS;

  String json;
  serializeJson(doc, json);
  return json;
}

// ==================== JSON HELPERS (REMOTE) ====================
String getRemoteStatusJSON()
{
  StaticJsonDocument<512> doc;

  for (int i = 0; i < TOTAL_OUTPUTS; i++)
  {
    String key = "O" + String(i + 1);
    doc[key] = outputs[i].state ? "1" : "0";
  }

  String json;
  serializeJson(doc, json);
  return json;
}

// ==================== COMMAND PROCESSOR ====================
void processCommand(String command, String source)
{
  StaticJsonDocument<512> doc;

  if (deserializeJson(doc, command) != DeserializationError::Ok)
  {
    Serial.println("Invalid JSON command");
    return;
  }

  String action = doc["action"].as<String>();

  Serial.printf("Command from %s: %s\n", source.c_str(), action.c_str());

  // Command: Switch Mode
  if (action == "switchMode" || action == "setMode")
  {
    String mode = doc["mode"].as<String>();

    if (mode == "MQTT" || mode == "mqtt" || doc["mode"] == 0)
    {
      switchMode(MODE_MQTT, true);
    }
    else if (mode == "WS" || mode == "websocket" || mode == "WebSocket" || doc["mode"] == 1)
    {
      switchMode(MODE_WEBSOCKET, true);
    }
  }

  // Command: Set Output
  else if (action == "setState" || action == "setOutput")
  {
    int channel = doc["channel"];
    bool state = doc["state"];

    if (channel >= 1 && channel <= TOTAL_OUTPUTS)
    {
      setOutput(channel, state);

      if (remoteConnected)
      {
        if (config.commMode == MODE_MQTT)
          mqttPublish();
        else
          wsPublish();
      }
    }
  }

  // Command: Get Status
  else if (action == "getStatus")
  {
    if (remoteConnected)
    {
      if (config.commMode == MODE_MQTT)
        mqttPublish();
      else
        wsPublish();
    }
  }

  // Command: Set Interval
  else if (action == "setInterval")
  {
    int id = doc["id"];
    if (id >= 0 && id < TOTAL_OUTPUTS)
    {
      outputs[id].intervalOn = doc["intervalOn"].as<unsigned long>() * 1000;
      outputs[id].intervalOff = doc["intervalOff"].as<unsigned long>() * 1000;
    }
  }

  // Command: Set Auto Mode
  else if (action == "setAutoMode")
  {
    int id = doc["id"];
    if (id >= 0 && id < TOTAL_OUTPUTS)
    {
      outputs[id].autoMode = doc["autoMode"];
      outputs[id].lastToggle = millis();
    }
  }

  // Command: Restart
  else if (action == "restart")
  {
    Serial.println("Restart command received");
    delay(1000);
    ESP.restart();
  }
}

// ==================== MQTT FUNCTIONS ====================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String message;
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  String topicStr = String(topic);
  Serial.println();
  Serial.printf(" MQTT Message Received\n");
  Serial.printf("   Topic: %s\n", topic);
  Serial.printf("   Data : %s\n", message.c_str());

  // ========== THINGSBOARD RPC ==========
  if (topicStr.startsWith("v1/devices/me/rpc/request/"))
  {
    Serial.println("🔧 Type: ThingsBoard RPC");
    
    // Extract request ID
    int lastSlash = topicStr.lastIndexOf('/');
    String requestId = topicStr.substring(lastSlash + 1);
    Serial.println("   Request ID: " + requestId);

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, message) != DeserializationError::Ok)
    {
      Serial.println(" JSON Parse Error");
      return;
    }

    String method = doc["method"].as<String>();
    Serial.println("   Method: " + method);

    StaticJsonDocument<256> response;
    bool success = false;

    if (method == "setValue")
    {
      JsonObject params = doc["params"];
      int channel = params["channel"] | 0;
      bool state = params["state"] | false;

      Serial.printf("   Action: Set CH%d to %s\n", channel, state ? "ON" : "OFF");

      if (channel >= 1 && channel <= TOTAL_OUTPUTS)
      {
        setOutput(channel, state);
        response["result"] = "OK";
        response["channel"] = channel;
        response["state"] = state;
        success = true;
      }
      else
      {
        response["error"] = "Invalid channel (1-20)";
      }
    }
    // ===== COMMAND: getValues =====
    else if (method == "getValues")
    {
      Serial.println("   Action: Get all channel status");
      
      JsonObject outputs_obj = response.createNestedObject("outputs");
      for (int i = 0; i < TOTAL_OUTPUTS; i++)
      {
        String key = "CH" + String(i + 1);
        outputs_obj[key] = outputs[i].state ? 1 : 0;
      }
      success = true;
    }
    // ===== COMMAND: setAutoMode =====
    else if (method == "setAutoMode")
    {
      JsonObject params = doc["params"];
      int channel = params["channel"] | 0;
      bool autoMode = params["enabled"] | false;
      int intervalOn = params["intervalOn"] | 5;
      int intervalOff = params["intervalOff"] | 5;

      Serial.printf("   Action: Set CH%d AutoMode=%s (ON:%ds OFF:%ds)\n", 
                    channel, autoMode ? "true" : "false", intervalOn, intervalOff);

      if (channel >= 1 && channel <= TOTAL_OUTPUTS)
      {
        int idx = channel - 1;
        outputs[idx].autoMode = autoMode;
        outputs[idx].intervalOn = intervalOn * 1000;
        outputs[idx].intervalOff = intervalOff * 1000;
        outputs[idx].lastToggle = millis();

        rebuildSyncGroups();

        response["result"] = "OK";
        response["channel"] = channel;
        response["autoMode"] = autoMode;
        success = true;
      }
      else
      {
        response["error"] = "Invalid channel (1-20)";
      }
    }
    else if (method == "restart")
    {
      Serial.println("   Action: Restarting ESP32...");
      
      response["result"] = "Restarting...";
      success = true;

      // Send response first
      String responseTopic = "v1/devices/me/rpc/response/" + requestId;
      String responseJson;
      serializeJson(response, responseJson);
      mqttClient.publish(responseTopic.c_str(), responseJson.c_str());

      delay(1000);
      ESP.restart();
      return;
    }
    else
    {
      Serial.println("   Unknown method!");
      response["error"] = "Unknown method: " + method;
    }

    // Send RPC response
    String responseTopic = "v1/devices/me/rpc/response/" + requestId;
    String responseJson;
    serializeJson(response, responseJson);
    
    mqttClient.publish(responseTopic.c_str(), responseJson.c_str());

    Serial.println(" Response sent:");
    Serial.println("   " + responseJson);

    // Publish updated telemetry if success
    if (success)
    {
      delay(100);
      mqttPublish();
    }
  }
  else
  {
    Serial.println(" Type: Standard MQTT Command");
    processCommand(message, "MQTT");
  }
}

void mqttPublish()
{
  if (!remoteConnected)
    return;

  String topic;
  String json;

  bool isThingsBoard = (config.serverToken.length() > 0);

  if (isThingsBoard)
  {
    StaticJsonDocument<1024> doc;
  
    for (int i = 0; i < TOTAL_OUTPUTS; i++)
    {
      String key = "Q" + String(i + 1);
      doc[key] = outputs[i].state ? 1 : 0;
    }

    serializeJson(doc, json);
    topic = "v1/devices/me/telemetry";
  }
  else
  {
    json = getRemoteStatusJSON();
    topic = config.serverPath;
  }

  // Publish
  bool published = mqttClient.publish(topic.c_str(), json.c_str());

  if (published)
  {
    Serial.println(" Published:");
    Serial.println("   Topic: " + topic);
    Serial.println("   Data : " + json);
  }
  else
  {
    Serial.println(" Publish failed!");
  }
}

void mqttReconnect()
{
  unsigned long now = millis();
  if (now - lastRemoteReconnect < 5000)
    return;

  lastRemoteReconnect = now;

  Serial.print("Connecting MQTT...");

  String clientId = "ESP32-" + String(random(0xffff), HEX);

  bool isThingsBoard = (config.serverToken.length() > 0);
  bool connected = false;

  if (isThingsBoard)
  {
    connected = mqttClient.connect(
      clientId.c_str(),
      config.serverToken.c_str(),
      NULL 
    );
  }
  else
  {
    connected = mqttClient.connect(clientId.c_str());
  }

  if (connected)
  {
    Serial.println(" MQTT CONNECTED!");
    remoteConnected = true;
    isRemoteReconnecting = false;

    if (isThingsBoard)
    {
      mqttClient.subscribe("v1/devices/me/rpc/request/+");
      mqttClient.subscribe("v1/devices/me/attributes");
      
      Serial.println(" Subscribed to:");
      Serial.println("   • v1/devices/me/rpc/request/+");
      Serial.println("   • v1/devices/me/attributes");
    }
    else
    {
      String controlTopic = config.serverPath + "/control";
      mqttClient.subscribe(controlTopic.c_str());
      
      Serial.println("Subscribed to:");
      Serial.printf("   • %s\n", controlTopic.c_str());
    }

    mqttPublish();
    
    lcdOutputPage = 0;
    updateLCD();
    lastLcdPageSwap = millis();
    lcdNeedsRedraw = true;
  }
  else
  {
    Serial.println(" CONNECTION FAILED!");
    Serial.printf("   Error Code: %d\n", mqttClient.state());
    Serial.println();
    Serial.println("    Error Reference:");
    Serial.println("   -4 = Connection timeout");
    Serial.println("   -3 = Connection lost");
    Serial.println("   -2 = Connect failed");
    Serial.println("   -1 = Disconnected");
    Serial.println("    1 = Bad protocol version");
    Serial.println("    2 = ID rejected");
    Serial.println("    3 = Server unavailable");
    Serial.println("    4 = Bad credentials");
    Serial.println("    5 = Unauthorized");
    Serial.println();
    Serial.println("    Troubleshooting:");
    
    if (isThingsBoard)
    {
      Serial.println("   ✓ Check Access Token is correct");
      Serial.println("   ✓ Check device exists in ThingsBoard");
      Serial.println("   ✓ Server should be: mqtt.thingsboard.cloud");
      Serial.println("   ✓ Port should be: 1883");
    }
    else
    {
      Serial.println("   ✓ Check MQTT broker is running");
      Serial.println("   ✓ Check broker allows anonymous connection");
      Serial.println("   ✓ Check server IP and port");
      Serial.println("   ✓ Try: test.mosquitto.org:1883");
    }
    
    Serial.println("========================================\n");

    remoteConnected = false;
    updateLCD();
    lastLcdPageSwap = millis();
    lcdNeedsRedraw = true;
  }
}

// ==================== WEBSOCKET FUNCTIONS ====================
void wsEvent(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    Serial.println("WS Disconnected");
    remoteConnected = false;

    if (wifiConnected && !isRemoteReconnecting) 
    {
      Serial.printf("WS: Memulai 15 detik percobaan reconnect...\n");
      isRemoteReconnecting = true;
      remoteDisconnectTime = millis();
    }

    updateLCD(); 
    lastLcdPageSwap = millis();
    lcdNeedsRedraw = true;
    break;

  case WStype_CONNECTED:
    Serial.printf("WS Connected to: %s\n", payload);
    remoteConnected = true;
    isRemoteReconnecting = false;

    wsPublish();

    lcdOutputPage = 0;
    updateLCD(); 
    lastLcdPageSwap = millis();
    lcdNeedsRedraw = true;
    break;

  case WStype_TEXT:
  {
    String message = String((char *)payload);
    Serial.printf("WS Received: %s\n", message.c_str());

    // Process command
    processCommand(message, "WebSocket");
    break;
  }

  case WStype_ERROR:
    Serial.printf("WS Error: %s\n", payload);
    break;

  default:
    break;
  }
}

void wsPublish()
{
  if (!remoteConnected)
    return;

  String json = getRemoteStatusJSON();

  wsClient.sendTXT(json);
  Serial.println("WS Published");
}

// ==================== WEB SERVER HANDLERS ====================
void serveFile(String path, String contentType)
{
  if (LittleFS.exists(path))
  {
    File file = LittleFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
  }
  else
  {
    server.send(404, "text/plain", "File Not Found");
  }
}

void handleRoot() { serveFile("/login.html", "text/html"); }
void handleDashboard() { serveFile("/index.html", "text/html"); }
void handleConfigPage() { serveFile("/config.html", "text/html"); }
void handleCSS() { serveFile("/style.css", "text/css"); }
void handleJS() { serveFile("/script.js", "application/javascript"); }

void handleLogin()
{
  if (server.method() != HTTP_POST)
  {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  StaticJsonDocument<200> doc;
  deserializeJson(doc, server.arg("plain"));

  String username = doc["username"];
  String password = doc["password"];

  StaticJsonDocument<100> response;
  if (username == config.webUsername && password == config.webPassword)
  {
    response["success"] = true;
    response["token"] = "authorized";
  }
  else
  {
    response["success"] = false;
    response["message"] = "Invalid credentials";
  }

  String json;
  serializeJson(response, json);
  server.send(200, "application/json", json);
}

void handleGetStatus()
{
  server.send(200, "application/json", getStatusJSON());
}

// Handler untuk switch mode via web
void handleSetMode()
{
  if (server.method() != HTTP_POST)
  {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  StaticJsonDocument<200> doc;
  deserializeJson(doc, server.arg("plain"));

  int mode = doc["mode"];

  if (mode == 0 || mode == 1)
  {
    switchMode((CommMode)mode, true);

    StaticJsonDocument<100> response;
    response["success"] = true;
    response["mode"] = mode;
    response["modeName"] = mode == 0 ? "MQTT" : "WebSocket";

    String json;
    serializeJson(response, json);
    server.send(200, "application/json", json);
  }
  else
  {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid mode\"}");
  }
}

void handleSetOutput()
{
  if (server.method() != HTTP_POST)
  {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  StaticJsonDocument<512> doc;
  deserializeJson(doc, server.arg("plain"));

  String action = doc["action"];

  if (action == "setState")
  {
    int id = doc["id"];
    bool state = doc["state"];

    if (id >= 0 && id < TOTAL_OUTPUTS)
    {
      setOutput(id + 1, state);

      if (remoteConnected)
      {
        if (config.commMode == MODE_MQTT)
          mqttPublish();
        else
          wsPublish();
      }

      server.send(200, "application/json", "{\"success\":true}");
    }
    else
    {
      server.send(400, "application/json", "{\"success\":false}");
    }
  }
  else if (action == "setAutoMode")
  {
    int id = doc["id"];
    if (id >= 0 && id < TOTAL_OUTPUTS)
    {
      outputs[id].autoMode = doc["autoMode"];
      outputs[id].lastToggle = millis();

      // Rebuild sync groups
      rebuildSyncGroups();

      server.send(200, "application/json", "{\"success\":true}");
    }
    else
    {
      server.send(400, "application/json", "{\"success\":false}");
    }
  }
  else if (action == "setInterval")
  {
    int id = doc["id"];
    if (id >= 0 && id < TOTAL_OUTPUTS)
    {
      outputs[id].intervalOn = doc["intervalOn"].as<unsigned long>() * 1000;
      outputs[id].intervalOff = doc["intervalOff"].as<unsigned long>() * 1000;

      // Rebuild sync groups jika output dalam auto mode
      if (outputs[id].autoMode)
      {
        rebuildSyncGroups();
      }

      server.send(200, "application/json", "{\"success\":true}");
    }
    else
    {
      server.send(400, "application/json", "{\"success\":false}");
    }
  }
  else if (action == "setName")
  {
    int id = doc["id"];
    String name = doc["name"].as<String>();

    if (id >= 0 && id < TOTAL_OUTPUTS)
    {
      outputs[id].name = name;
      server.send(200, "application/json", "{\"success\":true}");
    }
    else
    {
      server.send(400, "application/json", "{\"success\":false}");
    }
  }
  else if (action == "rebuildGroups")
  {
    rebuildSyncGroups();
    server.send(200, "application/json", "{\"success\":true}");
  }
  else if (action == "setToggleLimit")
  {
    int id = doc["id"];
    if (id >= 0 && id < TOTAL_OUTPUTS)
    {
      outputs[id].maxToggles = doc["limit"].as<int>();
      outputs[id].currentToggles = 0; // Otomatis reset meteran saat set baru
      Serial.printf("CH%02d: Batasan perpindahan diatur ke %d. Meteran direset.\n", id + 1, outputs[id].maxToggles);
      server.send(200, "application/json", "{\"success\":true}");
    }
    else { server.send(400, "application/json", "{\"success\":false}"); }
  }
  else if (action == "resetToggleCounter")
  {
    int id = doc["id"];
    if (id >= 0 && id < TOTAL_OUTPUTS)
    {
      outputs[id].currentToggles = 0;
      Serial.printf("CH%02d: Meteran perpindahan direset.\n", id + 1);
      server.send(200, "application/json", "{\"success\":true}");
    }
    else { server.send(400, "application/json", "{\"success\":false}"); }
  }
  else
  {
    server.send(400, "application/json", "{\"success\":false}");
  }
}

void handleGetConfig()
{
  StaticJsonDocument<1024> doc;
  doc["wifiSSID"] = config.wifiSSID;
  doc["serverIP"] = config.serverIP;
  doc["serverPort"] = config.serverPort;
  doc["serverPath"] = config.serverPath;
  doc["serverToken"] = config.serverToken;
  doc["webUsername"] = config.webUsername;
  doc["commMode"] = (int)config.commMode;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleSaveConfig()
{
  if (server.method() != HTTP_POST)
  {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  StaticJsonDocument<1024> doc;
  deserializeJson(doc, server.arg("plain"));

  config.wifiSSID = doc["wifiSSID"].as<String>();
  config.wifiPassword = doc["wifiPassword"].as<String>();
  config.serverIP = doc["serverIP"].as<String>();
  config.serverPort = doc["serverPort"] | 1883;
  config.serverPath = doc["serverPath"].as<String>();
  config.serverToken = doc["serverToken"].as<String>();
  config.webUsername = doc["webUsername"].as<String>();

  CommMode newMode = (CommMode)(doc["commMode"] | config.commMode);

  if (doc.containsKey("webPassword") && doc["webPassword"].as<String>().length() > 0)
  {
    config.webPassword = doc["webPassword"].as<String>();
  }

  // Switch mode jika berbeda (saveToConfig=false karena kita save manual)
  if (newMode != config.commMode)
  {
    config.commMode = newMode;
    saveConfig();
    switchMode(newMode, false);
  }
  else
  {
    saveConfig();
  }

  StaticJsonDocument<100> response;
  response["success"] = true;
  response["message"] = "Config saved. Restarting...";

  String json;
  serializeJson(response, json);
  server.send(200, "application/json", json);

  delay(1000);
  ESP.restart();
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  Serial.println("404 Not Found: " + server.uri());
  server.send(404, "text/plain", message);
}

// ==================== SERIAL COMMANDS ====================
void handleSerialCommand()
{
  if (!Serial.available())
    return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.startsWith("CH"))
  {
    int spacePos = cmd.indexOf(' ');
    if (spacePos > 0)
    {
      int channel = cmd.substring(2, spacePos).toInt();
      String state = cmd.substring(spacePos + 1);

      if (channel >= 1 && channel <= 20)
      {
        bool newState = (state == "ON" || state == "1");
        setOutput(channel, newState);

        if (remoteConnected)
        {
          if (config.commMode == MODE_MQTT)
            mqttPublish();
          else
            wsPublish();
        }
      }
    }
  }
  else if (cmd.startsWith("SETMODE"))
  {
    int spacePos = cmd.indexOf(' ');
    if (spacePos > 0)
    {
      String mode = cmd.substring(spacePos + 1);

      if (mode == "MQTT" || mode == "0")
      {
        config.commMode = MODE_MQTT;
        Serial.println("Mode set to: MQTT");
      }
      else if (mode == "WS" || mode == "WEBSOCKET" || mode == "1")
      {
        config.commMode = MODE_WEBSOCKET;
        Serial.println("Mode set to: WebSocket");
      }

      saveConfig();
      Serial.println("Config saved");
      Serial.println("Please restart ESP32");
    }
  }
  else if (cmd.startsWith("MODE"))
  {
    int spacePos = cmd.indexOf(' ');
    if (spacePos > 0)
    {
      String mode = cmd.substring(spacePos + 1);

      if (mode == "MQTT" || mode == "0")
      {
        switchMode(MODE_MQTT, true);
      }
      else if (mode == "WS" || mode == "WEBSOCKET" || mode == "1")
      {
        switchMode(MODE_WEBSOCKET, true);
      }
      else
      {
        Serial.println("Usage: MODE MQTT atau MODE WS");
      }
    }
    else
    {
      Serial.printf("Current mode: %s\n", config.commMode == MODE_MQTT ? "MQTT" : "WebSocket");
    }
  }
  else if (cmd == "STATUS")
  {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║      SYSTEM STATUS                  ║");
    Serial.println("╠═════════════════════════════=═══════╣");
    Serial.printf("║ Mode: %-28s ║\n", config.commMode == MODE_MQTT ? "MQTT" : "WebSocket");
    Serial.printf("║ WiFi: %-28s ║\n", wifiConnected ? "Connected" : "AP Mode");
    Serial.printf("║ Remote: %-26s ║\n", remoteConnected ? "Connected" : "Disconnected");
    Serial.printf("║ Server: %-26s ║\n", config.serverIP.c_str());
    Serial.printf("║ Port: %-28d ║\n", config.serverPort);
    Serial.println("╠════════════════════════════════════╣");
    for (int i = 0; i < TOTAL_OUTPUTS; i++)
    {
      Serial.printf("║ CH%02d %-15s [%s] ║\n",
                    i + 1, outputs[i].name.c_str(), outputs[i].state ? "ON " : "OFF");
    }
    Serial.println("╚════════════════════════════════════╝\n");
  }
  else if (cmd == "TEST")
  {
    Serial.println("\nTesting all channels...");
    for (int i = 1; i <= TOTAL_OUTPUTS; i++)
    {
      Serial.printf("CH%02d... ", i);
      setOutput(i, true);
      delay(300);
      setOutput(i, false);
      delay(100);
      Serial.println("OK");
    }
    Serial.println("Complete\n");
  }
  else if (cmd == "SCAN")
  {
    Serial.println("\nI2C Scan...");
    byte count = 0;
    for (byte i = 1; i < 127; i++)
    {
      Wire.beginTransmission(i);
      if (Wire.endTransmission() == 0)
      {
        Serial.printf("0x%02X", i);
        if (i == ADDR_PCF1)
          Serial.print(" (PCF1)");
        if (i == ADDR_PCF2)
          Serial.print(" (PCF2)");
        if (i == ADDR_LCD)
          Serial.print(" (LCD)");
        Serial.println();
        count++;
      }
    }
    Serial.printf("Found %d device(s)\n\n", count);
  }
  else if (cmd == "CRED")
  {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║      CURRENT CREDENTIALS           ║");
    Serial.println("╠════════════════════════════════════╣");
    Serial.printf("║ Username: %-24s ║\n", config.webUsername.c_str());
    Serial.printf("║ Password: %-24s ║\n", config.webPassword.c_str());
    Serial.println("╚════════════════════════════════════╝\n");
  }
  else if (cmd == "RESETCRED")
  {
    Serial.println("Resetting credentials to default...");
    config.webUsername = "admin";
    config.webPassword = "admin123";
    saveConfig();
    Serial.println("   Credentials reset to:");
    Serial.println("   Username: admin");
    Serial.println("   Password: admin123");
    Serial.println("Please restart ESP32 or just try login again.\n");
  }
  else if (cmd == "HELP")
  {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║        COMMANDS                    ║");
    Serial.println("╠════════════════════════════════════╣");
    Serial.println("║ CH[1-20] ON/OFF - Toggle output    ║");
    Serial.println("║ MODE [MQTT/WS]  - Switch mode      ║");
    Serial.println("║ STATUS          - Show status      ║");
    Serial.println("║ TEST            - Test outputs     ║");
    Serial.println("║ SCAN            - I2C scan         ║");
    Serial.println("║ CRED            - Show credentials ║");
    Serial.println("║ RESETCRED       - Reset to default ║");
    Serial.println("║ HELP            - This help        ║");
    Serial.println("╚════════════════════════════════════╝\n");
  }
}

// ==================== SYNC GROUP MANAGEMENT ====================
void rebuildSyncGroups()
{
  // Reset all groups
  activeSyncGroups = 0;
  for (int i = 0; i < 5; i++)
  {
    syncGroups[i].memberCount = 0;
  }
  for (int i = 0; i < TOTAL_OUTPUTS; i++)
  {
    outputGroupMap[i] = -1; // No group
  }

  // Build groups based on auto mode outputs with same intervals
  for (int i = 0; i < TOTAL_OUTPUTS; i++)
  {
    if (!outputs[i].autoMode)
      continue;

    // Cari grup yang cocok
    int groupIdx = -1;
    for (int g = 0; g < activeSyncGroups; g++)
    {
      if (syncGroups[g].intervalOn == outputs[i].intervalOn &&
          syncGroups[g].intervalOff == outputs[i].intervalOff)
      {
        groupIdx = g;
        break;
      }
    }

    // Jika belum ada grup, buat baru
    if (groupIdx == -1 && activeSyncGroups < 5)
    {
      groupIdx = activeSyncGroups;
      syncGroups[groupIdx].intervalOn = outputs[i].intervalOn;
      syncGroups[groupIdx].intervalOff = outputs[i].intervalOff;
      syncGroups[groupIdx].lastToggle = millis();
      syncGroups[groupIdx].currentState = outputs[i].state;
      syncGroups[groupIdx].memberCount = 0;
      activeSyncGroups++;

      Serial.printf("New Sync Group %d: ON=%lums OFF=%lums\n",
                    groupIdx, syncGroups[groupIdx].intervalOn, syncGroups[groupIdx].intervalOff);
    }

    // Assign output ke grup
    if (groupIdx >= 0)
    {
      outputGroupMap[i] = groupIdx;
      syncGroups[groupIdx].memberCount++;
      Serial.printf("  └─ CH%02d assigned to Group %d\n", i + 1, groupIdx);
    }
  }

  Serial.printf("Sync Groups rebuilt: %d active groups\n", activeSyncGroups);
}

void processSyncGroups()
{
  unsigned long currentMillis = millis();

  for (int g = 0; g < activeSyncGroups; g++)
  {
    if (syncGroups[g].memberCount == 0)
      continue;

    unsigned long interval = syncGroups[g].currentState
                                 ? syncGroups[g].intervalOn
                                 : syncGroups[g].intervalOff;

    if (currentMillis - syncGroups[g].lastToggle >= interval)
    {
      // Toggle state
      syncGroups[g].currentState = !syncGroups[g].currentState;
      syncGroups[g].lastToggle = currentMillis;

      Serial.printf("\nGROUP %d TOGGLE → %s (Members: %d)\n",
                    g, syncGroups[g].currentState ? "ON" : "OFF", syncGroups[g].memberCount);

      // Apply ke semua member
      int toggledCount = 0;
      for (int i = 0; i < TOTAL_OUTPUTS; i++)
      {
        if (outputGroupMap[i] == g)
        {
          setOutput(i + 1, syncGroups[g].currentState);
          toggledCount++;
        }
      }

      Serial.printf("Toggled %d outputs in Group %d\n\n", toggledCount, g);

      // Publish status update
      if (remoteConnected)
      {
        if (config.commMode == MODE_MQTT)
          mqttPublish();
        else
          wsPublish();
      }
    }
  }
}

// ==================== SETUP ====================
void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║   ESP32 - 20 Channel Control (v3.1)       ║");
  Serial.println("║   Dynamic Mode Switching                   ║");
  Serial.println("╚════════════════════════════════════════════╝\n");

  Wire.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ESP32 Control");
  lcd.setCursor(0, 1);
  lcd.print("v3.1 Booting...");

  // Init LittleFS
  Serial.println("Mounting LittleFS...");
  if (!LittleFS.begin(true))
  {
    Serial.println("✗ LittleFS Mount Failed!");
    lcd.setCursor(0, 1);
    lcd.print("FS Error!");
    while (1)
      delay(1000);
  }
  Serial.println("LittleFS Mounted OK");

  // ============ FORCE DELETE CONFIG.JSON ============
  Serial.println("\nChecking for old config...");
  if (LittleFS.exists(CONFIG_FILE))
  {
    Serial.println("Old config.json found!");

    // Baca dulu untuk cek mode
    File checkFile = LittleFS.open(CONFIG_FILE, "r");
    if (checkFile)
    {
      String content = checkFile.readString();
      checkFile.close();

      Serial.println("Current config content:");
      Serial.println(content);

      // Cek apakah ada commMode
      if (content.indexOf("\"commMode\":0") >= 0)
      {
        Serial.println("MQTT mode detected in config!");
        Serial.println("Deleting old config.json...");

        if (LittleFS.remove(CONFIG_FILE))
        {
          Serial.println("Config deleted successfully");
          Serial.println("Will create new config with WebSocket mode");
        }
        else
        {
          Serial.println("Failed to delete config");
        }
      }
    }
  }
  // ==================================================

  // List files
  Serial.println("\nFiles in LittleFS:");
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  int fileCount = 0;

  while (file)
  {
    Serial.print("  ");
    Serial.print(file.name());
    Serial.print(" (");
    Serial.print(file.size());
    Serial.println(" bytes)");
    fileCount++;
    file = root.openNextFile();
  }

  if (fileCount == 0)
  {
    Serial.println("NO FILES FOUND!");
  }
  Serial.println();

  // Load config (sekarang akan create baru dengan default WebSocket)
  loadConfig();

  Serial.printf("   Final mode: %s\n\n", config.commMode == MODE_MQTT ? "MQTT" : "WebSocket");

  Serial.println("\nInitializing PCF8574...");

  if (pcf1.begin(ADDR_PCF1))
  {
    Serial.printf("PCF1 initialized at 0x%02X\n", ADDR_PCF1);
  }
  else
  {
    Serial.printf("PCF1 NOT FOUND at 0x%02X!\n", ADDR_PCF1);
  }

  if (pcf2.begin(ADDR_PCF2))
  {
    Serial.printf("PCF2 initialized at 0x%02X\n", ADDR_PCF2);
  }
  else
  {
    Serial.printf("PCF2 NOT FOUND at 0x%02X!\n", ADDR_PCF2);
  }

  initChannelMap();
  initHardwarePins();
  initOutputs();

  // WiFi
  WiFi.mode(WIFI_STA);

  if (config.wifiSSID.length() > 0)
  {
    Serial.println("Connecting WiFi: " + config.wifiSSID);
    WiFi.begin(config.wifiSSID.c_str(), config.wifiPassword.c_str());

    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20)
    {
      delay(500);
      Serial.print(".");
      timeout++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      wifiConnected = true;
      Serial.println("\nWiFi OK");
      Serial.println("IP: " + WiFi.localIP().toString());
    }
  }

  if (!wifiConnected)
  {
    Serial.println("AP Mode");
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.println("IP: " + WiFi.softAPIP().toString());
  }

  // Setup mode sesuai config
  Serial.println("\nSetting up communication mode...");
  if (config.commMode == MODE_MQTT)
  {
    Serial.println("Initial Mode: MQTT");
    if (wifiConnected && config.serverIP.length() > 0)
    {
      mqttClient.setServer(config.serverIP.c_str(), config.serverPort);
      mqttClient.setCallback(mqttCallback);
    }
  }
  else
  {
    Serial.println("Initial Mode: WebSocket"); // INI YANG SEHARUSNYA MUNCUL
    if (wifiConnected && config.serverIP.length() > 0)
    {
      wsClient.begin(config.serverIP.c_str(), config.serverPort, config.serverPath.c_str());
      wsClient.onEvent(wsEvent);
      wsClient.setReconnectInterval(5000);
    }
  }

  // Web Server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/dashboard", HTTP_GET, handleDashboard);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/style.css", HTTP_GET, handleCSS);
  server.on("/script.js", HTTP_GET, handleJS);
  server.on("/api/login", HTTP_POST, handleLogin);
  server.on("/api/status", HTTP_GET, handleGetStatus);
  server.on("/api/output", HTTP_POST, handleSetOutput);
  server.on("/api/setmode", HTTP_POST, handleSetMode);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handleSaveConfig);

  server.onNotFound(handleNotFound);
  Serial.println("404 handler registered");

  server.begin();
  Serial.println("HTTP Server started");

  updateLCD();

  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║          READY!                            ║");
  Serial.println("╠════════════════════════════════════════════╣");
  Serial.println("║ Web: http://" + (wifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "               ║");
  Serial.println("║ Login: " + config.webUsername + " / " + config.webPassword + "              ║");
  Serial.println("║                                            ║");
  Serial.println("║ Serial: TYPE 'HELP' for commands          ║");
  Serial.println("╚════════════════════════════════════════════╝\n");
}

// ==================== LOOP ====================
void loop()
{
  handleSerialCommand();
  server.handleClient();

  unsigned long currentMillis = millis();

  // Handle MQTT atau WebSocket
  if (config.commMode == MODE_MQTT)
  {
    if (mqttClient.connected())
    {
      mqttClient.loop();
      isRemoteReconnecting = false;
      
      if (currentMillis - lastRemotePublish >= 5000)
      {
          mqttPublish();
          lastRemotePublish = currentMillis;
      }
    }
    else
    {
      if (!wifiConnected || config.serverIP.length() == 0)
      {
          isRemoteReconnecting = false;
      }
      else if (!isRemoteReconnecting)
      {
        Serial.println("MQTT Disconnected. Memulai 15 detik percobaan reconnect...");
        isRemoteReconnecting = true;
        remoteDisconnectTime = millis();
      }
      else
      {
        if (currentMillis - remoteDisconnectTime >= REMOTE_RECONNECT_TIMEOUT)
        {
          Serial.println("\nMQTT: Gagal reconnect selama 15 detik.");
          Serial.println("Menghentikan percobaan STA dan pindah ke AP Mode.");
          
          isRemoteReconnecting = false;
          mqttClient.disconnect(); 
          
          WiFi.disconnect();
          WiFi.mode(WIFI_AP);
          WiFi.softAP(AP_SSID, AP_PASSWORD);
          wifiConnected = false;
          
          Serial.println("AP Mode Aktif. IP: " + WiFi.softAPIP().toString());
          lcdNeedsRedraw = true; 
        }
        else
        {
          Serial.print(".");
          mqttReconnect(); 
        }
      }
    }
  }
  else
  {
    wsClient.loop(); 

    if (remoteConnected)
    {
      isRemoteReconnecting = false; 
      
      if (currentMillis - lastRemotePublish >= 5000)
      {
        wsPublish();
        lastRemotePublish = currentMillis;
      }
    }
    else
    {
      if (!wifiConnected || config.serverIP.length() == 0)
      {
        isRemoteReconnecting = false; 
      }
      else if (isRemoteReconnecting)
      {
        if (currentMillis - remoteDisconnectTime >= REMOTE_RECONNECT_TIMEOUT)
        {
          Serial.println("\nWS: Gagal reconnect selama 15 detik.");
          Serial.println("Menghentikan percobaan STA dan pindah ke AP Mode.");
          
          isRemoteReconnecting = false;
          wsClient.disconnect();
          
          WiFi.disconnect();
          WiFi.mode(WIFI_AP);
          WiFi.softAP(AP_SSID, AP_PASSWORD);
          wifiConnected = false;
          
          Serial.println("AP Mode Aktif. IP: " + WiFi.softAPIP().toString());
          lcdNeedsRedraw = true; 
        }
        else
        {
          Serial.print(".");
        }
      }
    }
  }

  // Process Sync Groups
  processSyncGroups();

  // Update LCD
  if (remoteConnected && (currentMillis - lastLcdPageSwap >= LCD_PAGE_SWAP_MS))
  {
    lastLcdPageSwap = currentMillis;

    lcdOutputPage++;
    if (lcdOutputPage >= LCD_PAGES) {
        lcdOutputPage = 0;
    }
    lcdNeedsRedraw = true;
  }

  if (lcdNeedsRedraw) 
  {
    lcdNeedsRedraw = false;
    updateLCD(); 
  }
}