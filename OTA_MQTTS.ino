#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MQTT.h>
#include "FavoriotCA.h"
#include <Update.h>

#define CHUNK_SIZE 1024  // Adjust to a smaller value if needed


// WiFi and MQTT credentials
const char ssid[] = "Your WIFI SSID";
const char password[] = "Your WIFI password";

const char DeviceDeveloperId[] = "replace with your access token";
const char AccessToken[] = "replace with your device developer id";

const char firmwareUpdate[] = "/v2/firmware/update";
const char firmwareUpdateStatus[] = "/v2/firmware/update/status";

WiFiClientSecure net;
MQTTClient mqtt(8192);

// Variables for OTA
bool isUpdating = false;
size_t totalBytesWritten = 0;

void connectToWiFi() {
    Serial.print("Connecting to Wi-Fi '" + String(ssid) + "' ...");
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println(" connected!");
}

void setup() {
    Serial.begin(115200);

    connectToWiFi();
    connectToFavoriotMQTT();
}

void connectToFavoriotMQTT() {
    Serial.print("Connecting to Favoriot MQTT...");
    net.setCACert(FavoriotCA);

    mqtt.begin("mqtt.favoriot.com", 8883, net);
    mqtt.setKeepAlive(60);
    mqtt.setTimeout(60000); // Set keep-alive to 120 seconds
    mqtt.onMessageAdvanced(callback);

    String clientId = String(ssid) + "-" + String(random(1000, 9999));
    while (!mqtt.connect(clientId.c_str(), AccessToken, AccessToken)) {
        Serial.print(".");
        delay(500);
    }
    Serial.println(" connected!");

    mqtt.subscribe(String(AccessToken) + String(firmwareUpdate));
}

void callback(MQTTClient *client, char topic[], char payload[], int length) {
    Serial.print("Received topic: ");
    Serial.println(topic);

    // Define the topic for firmware updates
    String expectedTopic = String(AccessToken) + String(firmwareUpdate);

    if (strcmp(topic, expectedTopic.c_str()) == 0) {
        String message = String(payload).substring(0, length);

        if (message == "start") {
            Serial.println("Firmware update initialized.");
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Serial.println("Failed to initialize update.");
                 mqtt.publish((String(AccessToken) + String(firmwareUpdateStatus)).c_str(),
                             "{\"status\":\"failed\",\"device_developer_id\":\"" + String(DeviceDeveloperId) + "\"}");
                return;
            }
            isUpdating = true;
            totalBytesWritten = 0;
        } else if (message == "end") {
            Serial.println("Firmware update completed.");
            if (Update.end(true)) {
                Serial.println("Update successful. Rebooting...");
                mqtt.publish((String(AccessToken) + String(firmwareUpdateStatus)).c_str(),
                             "{\"status\":\"success\",\"device_developer_id\":\"" + String(DeviceDeveloperId) + "\"}");
                delay(1000);
                ESP.restart();
            } else {
                Serial.println("Update failed during finalization.");
                mqtt.publish((String(AccessToken) + String(firmwareUpdateStatus)).c_str(),
                             "{\"status\":\"failed\",\"device_developer_id\":\"" + String(DeviceDeveloperId) + "\"}");
                Update.abort();
            }
            isUpdating = false;
        } else {
            // Write the firmware chunk
            size_t written = Update.write((uint8_t *)payload, length);
            if (written != length) {
                Serial.println("Failed to write chunk to flash memory.");
                Update.abort();
                isUpdating = false;
                mqtt.publish((String(AccessToken) + String(firmwareUpdateStatus)).c_str(),
                             "{\"status\":\"failed\",\"device_developer_id\":\"" + String(DeviceDeveloperId) + "\"}");
                return;
            }

            totalBytesWritten += written;
            Serial.print("Written chunk of size: ");
            Serial.println(length);
        }
    }
}




void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        connectToWiFi();
    }

    if (!mqtt.connected()) {
        connectToFavoriotMQTT();
    }

    mqtt.loop();

    delay(10);
}
