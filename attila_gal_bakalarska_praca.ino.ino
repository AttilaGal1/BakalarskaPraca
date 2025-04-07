#include <WiFiS3.h>
#include <PubSubClient.h>
#include <Servo.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <MFRC522.h>

const char* ssid = "";  //Dopísať názov Wi-Fi siete
const char* pass = "";  //Dopísať heslo

const char* mqtt_server = ""; //Dopísať IP adresu localhostu
const int mqtt_port = 1883;
const char* mqtt_topic = "smartlock/status";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WiFiServer server(80);
Servo myServo;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);  

const int servoPin = 6;
bool isUnlocked = false;
unsigned long unlockTime = 0;
String lastUnlockedTime = "Never";

#define SS_PIN 10
#define RST_PIN 9
MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  myServo.attach(servoPin);
  myServo.write(0);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to WiFi...");
    WiFi.begin(ssid, pass);
    delay(3000);
  }

  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.begin();

  mqttClient.setServer(mqtt_server, mqtt_port);
  connectMQTT();

  timeClient.begin();
  timeClient.update();

  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID ready.");
}

void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();
  timeClient.update();

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    if (isAuthorizedUID(rfid.uid.uidByte)) {
      Serial.println("Authorized RFID tag detected.");
      unlocked("RFID");
    } else {
      Serial.print("Unauthorized UID: ");
      for (byte i = 0; i < rfid.uid.size; i++) {
        Serial.print(rfid.uid.uidByte[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  WiFiClient client = server.available();
  if (client) {
    String request = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        request += c;
        if (c == '\n') {
          if (request.indexOf("GET /unlock") >= 0) {
            unlocked("Web");
          }

          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println();
          client.println("<!DOCTYPE html><html><head><title>Smart Lock</title>");
          client.println("<style>");
          client.println("body { font-family: Arial, sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; background-color: #f2f2f2; }");
          client.println(".container { text-align: center; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 4px 12px rgba(0, 0, 0, 0.1); }");
          client.println("h1 { color: #333; }");
          client.println("p { font-size: 18px; color: #555; }");
          client.println("button { padding: 10px 20px; font-size: 16px; border: none; background-color: #4CAF50; color: white; border-radius: 5px; cursor: pointer; }");
          client.println("button:hover { background-color: #45a049; }");
          client.println("</style></head><body><div class='container'>");
          client.println("<h1>Smart Lock Control</h1>");
          client.println("<p>Status: " + String(isUnlocked ? "Unlocked" : "Locked") + "</p>");
          client.println("<p>Last Unlocked: " + lastUnlockedTime + "</p>");
          client.println("<form action=\"/unlock\" method=\"GET\">");
          client.println("<button type=\"submit\">Unlock</button>");
          client.println("</form></div></body></html>");
          break;
        }
      }
    }
    delay(1);
    client.stop();
  }

  if (isUnlocked && millis() - unlockTime >= 6000) {
    locked();
  }
}

void unlocked(String source) {
  myServo.write(90);
  isUnlocked = true;
  unlockTime = millis();

  lastUnlockedTime = timeClient.getFormattedTime();
  Serial.println("Unlocked at: " + lastUnlockedTime + " via " + source);

  String message = "unlocked at " + lastUnlockedTime + " via " + source;
  mqttClient.publish(mqtt_topic, message.c_str());
}

void locked() {
  myServo.write(0);
  isUnlocked = false;

  lastUnlockedTime = timeClient.getFormattedTime();
  Serial.println("Locked at: " + lastUnlockedTime);

  String message = "locked at " + lastUnlockedTime;
  mqttClient.publish(mqtt_topic, message.c_str());
}

bool isAuthorizedUID(byte *uid) {
  byte validUID[4] = {0xC1, 0x09, 0x8D, 0x1D};
  for (byte i = 0; i < 4; i++) {
    if (uid[i] != validUID[i]) return false;
  }
  return true;
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT... ");
    String clientId = "SmartLockClient-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected.");
      mqttClient.publish(mqtt_topic, "locked");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" trying again in 5 seconds...");
      delay(5000);
    }
  }
}
