#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "JSON";
const char* password = "javascript";

// ThingsBoard credentials
const char* thingsboardServer = "20.2.86.242";
const int mqttPort = 1883;
const char* clientID = "p8947gm633t705dji7pg";
const char* username = "w15t4sip0e8jvunn7v4h";
const char* mqttPassword = "oionj809iof0ctuj7mo6";

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Pin definitions
const int sensorPin = A0;
const int relayPin = D4;

// Thresholds
const int dryValue = 700;
const int wetValue = 500;

// Status
bool pumpState = false;
bool autoMode = true;
bool manualOverride = false;

unsigned long lastTelemetry = 0;
const long telemetryInterval = 1000;

void setup() {
  Serial.begin(115200);
  pinMode(sensorPin, INPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  client.setServer(thingsboardServer, mqttPort);
  client.setCallback(callback);
  reconnect();
}

void callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.print("Failed to parse RPC: ");
    Serial.println(error.c_str());
    return;
  }

  const char* method = doc["method"];
  if (strcmp(method, "setState") == 0) {
    autoMode = doc["params"];
    manualOverride = false;

    if (!autoMode) {
      pumpState = false;
      digitalWrite(relayPin, HIGH);
      Serial.println("Auto OFF, pump OFF");
    } else {
      Serial.println("Auto ON");
    }

    char responseTopic[50];
    snprintf(responseTopic, sizeof(responseTopic), "v1/devices/me/rpc/response/%s", topic + strlen(topic) - 1);

    StaticJsonDocument<64> responseDoc;
    responseDoc["auto_mode"] = autoMode;
    char response[64];
    serializeJson(responseDoc, response);
    client.publish(responseTopic, response);
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    if (client.connect(clientID, username, mqttPassword)) {
      Serial.println("Connected to ThingsBoard");
      client.subscribe("v1/devices/me/rpc/request/+");
    } else {
      Serial.print("MQTT Failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5s...");
      delay(5000);
    }
  }
}

void sendTelemetry() {
  StaticJsonDocument<128> doc;
  doc["moisture"] = analogRead(sensorPin);
  doc["pump_status"] = pumpState;
  doc["auto_mode"] = autoMode;

  char buffer[128];
  serializeJson(doc, buffer);
  client.publish("v1/devices/me/telemetry", buffer, true);  // QoS=1
}

int getSmoothedMoisture() {
  int sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += analogRead(sensorPin);
    delay(10);
  }
  return sum / 5;
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (millis() - lastTelemetry > telemetryInterval) {
    sendTelemetry();
    lastTelemetry = millis();
  }

  if (autoMode) {
    int sensorValue = getSmoothedMoisture();
    if (sensorValue > dryValue && !pumpState) {
      pumpState = true;
      digitalWrite(relayPin, LOW);
      Serial.println("Soil dry → pump ON");
    } else if (sensorValue < wetValue && pumpState) {
      pumpState = false;
      digitalWrite(relayPin, HIGH);
      Serial.println("Soil wet → pump OFF");
    }
  }
}
