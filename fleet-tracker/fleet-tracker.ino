#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <Ticker.h>

char ssid[32];
char password[32];

char server[46];
const char authMethod[] = "use-token-auth";
char token[36];
char deviceId[9];
char clientId[32];

const char publishTopic[] = "iot-2/evt/status/fmt/json";
const char responseTopic[] = "iotdm-1/response";
const char manageTopic[] = "iotdevice-1/mgmt/manage";
const char updateTopic[] = "iotdm-1/device/update";
const char rebootTopic[] = "iotdm-1/mgmt/initiate/device/reboot";

void callback(char* topic, byte* payload, unsigned int payloadLength);

WiFiClientSecure wifiClient;
PubSubClient client(server, 8883, callback, wifiClient);

unsigned int publishInterval = 30 * 1000;
long lastPublishMillis;

void setup() {
  Serial.begin(115200);
  Serial.println();

  blink_init();

  loadConfig();
  wifiConnect();
  mqttConnect();
  initManagedDevice();
}

void loop() {
  if (millis() - lastPublishMillis > publishInterval) {
    publishData();
    lastPublishMillis = millis();
  }

  if (!client.loop()) {
    mqttConnect();
    initManagedDevice();
  }
}

void loadConfig() {
  if (SPIFFS.begin()) {
    if (SPIFFS.exists("/config.json")) {
      File file = SPIFFS.open("/config.json", "r");
      if (file) {
        size_t size = file.size();
        std::unique_ptr<char[]> buf(new char[size]);
        file.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        Serial.print("JSON config: ");
        json.printTo(Serial);
        Serial.println();
        if (json.success()) {
          strcpy(ssid, json["ssid"]);
          strcpy(password, json["password"]);

          strcpy(server, json["org"]);
          strcat(server, ".messaging.internetofthings.ibmcloud.com");

          strcpy(token, json["token"]);

          sprintf(deviceId, "ft-%6x", ESP.getChipId());

          strcpy(clientId, "d:");
          strcat(clientId, json["org"]);
          strcat(clientId, ":fleet-tracker:");
          strcat(clientId, deviceId);
        } else {
          Serial.println("Failed to parse config file");
        }
      }
    }
    /*
    if (SPIFFS.exists("/wiotp.pkcs8")) {
      File file = SPIFFS.open("/wiotp.pkcs8", "r");
      if (file) {
        if (!wifiClient.loadPrivateKey(file)) {
          Serial.println("key not loaded");
        }
      }
    } else {
      Serial.println("Failed to open key file");
    }

    if (SPIFFS.exists("/wiotp.der")) {
      File file = SPIFFS.open("/wiotp.der", "r");
      if (file) {
        if (!wifiClient.loadCertificate(file)) {
          Serial.println("cert not loaded");
        }
      }
    } else {
      Serial.println("Failed to open cert file");
    }

    if (SPIFFS.exists("/messaging.der")) {
      File file = SPIFFS.open("/messaging.der", "r");
      if (file) {
        if (!wifiClient.loadCACert(file, file.size())) {
          Serial.println("messaging cert not loaded");
        }
      }
    } else {
      Serial.println("Failed to open messaging cert file");
    }
    /**/
  }
}

void wifiConnect() {
  blink_start();
  Serial.print("Connecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("WiFi connected, IP address: ");
  Serial.println(WiFi.localIP());
  blink_stop();
}

void mqttConnect() {
  if (!client.connected()) {
    blink_start();
    Serial.print("Connecting MQTT client to ");
    Serial.print(server);
    while (!client.connect(clientId, authMethod, token)) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("!");
    blink_stop();
  }
}

void initManagedDevice() {
  if (client.subscribe(responseTopic)) {
    Serial.println("subscribe to responses OK");
  } else {
    Serial.println("subscribe to responses FAILED");
  }

  if (client.subscribe(rebootTopic)) {
    Serial.println("subscribe to reboot OK");
  } else {
    Serial.println("subscribe to reboot FAILED");
  }

  if (client.subscribe(updateTopic)) {
    Serial.println("subscribe to update OK");
  } else {
    Serial.println("subscribe to update FAILED");
  }

  StaticJsonBuffer<300> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  JsonObject& d = root.createNestedObject("d");
  JsonObject& metadata = d.createNestedObject("metadata");
  metadata["publishInterval"] = publishInterval;
  JsonObject& supports = d.createNestedObject("supports");
  supports["deviceActions"] = true;

  char buff[300];
  root.printTo(buff, sizeof(buff));
  Serial.println("publishing device metadata:");
  Serial.println(buff);
  if (client.publish(manageTopic, buff)) {
    Serial.println("device Publish ok");
  } else {
    Serial.print("device Publish failed:");
  }
}

void publishData() {
  tick();
  
  StaticJsonBuffer<300> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  JsonObject& d = root.createNestedObject("d");
  d["counter"] = WiFi.RSSI();

  char payload[300];
  root.printTo(payload, sizeof(payload));

  Serial.print("Sending payload: ");
  Serial.println(payload);

  if (client.publish(publishTopic, payload)) {
    Serial.println("Publish OK");
  } else {
    Serial.println("Publish FAILED");
  }
  
  tick();
}

void callback(char* topic, byte * payload, unsigned int payloadLength) {
  Serial.print("callback invoked for topic: ");
  Serial.print(payloadLength);
  Serial.println(topic);

  if (strcmp(responseTopic, topic) == 0) {
    return; // just print of response for now
  }

  if (strcmp(rebootTopic, topic) == 0) {
    Serial.println("Rebooting...");
    ESP.restart();
  }

  if (strcmp(updateTopic, topic) == 0) {
    handleUpdate(payload);
  }
}

void handleUpdate(byte * payload) {
  StaticJsonBuffer<300> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject((char*) payload);
  if (!root.success()) {
    Serial.println("handleUpdate: payload parse FAILED");
    return;
  }
  Serial.println("handleUpdate payload:");
  root.prettyPrintTo(Serial);
  Serial.println();

  JsonObject& d = root["d"];
  JsonArray& fields = d["fields"];
  for (JsonArray::iterator it = fields.begin(); it != fields.end(); ++it) {
    JsonObject& field = *it;
    const char* fieldName = field["field"];
    if (strcmp(fieldName, "metadata") == 0) {
      JsonObject& fieldValue = field["value"];
      if (fieldValue.containsKey("publishInterval")) {
        publishInterval = fieldValue["publishInterval"];
        Serial.print("publishInterval:");
        Serial.println(publishInterval);
      }
    }
  }
}

Ticker ticker;

void tick() {
  int state = digitalRead(BUILTIN_LED);
  digitalWrite(BUILTIN_LED, !state);
}

void blink_init() {
  pinMode(BUILTIN_LED, OUTPUT);
}

void blink_start() {
  ticker.attach_ms(250, tick);
}

void blink_stop() {
  ticker.detach();
  digitalWrite(BUILTIN_LED, LOW);
}
