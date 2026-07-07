#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_wifi.h>

// ==================== CONFIGURATION ====================
const char *WIFI_SSID = "NetworkHB";
const char *WIFI_PASSWORD = "HB*77*BB";
const char *FIREBASE_URL = "https://bar-tracker-mvp-default-rtdb.asia-southeast1.firebasedatabase.app/venue_metrics/headcount.json";

const int MAX_CAPACITY = 300;       // Adjust to your bar's max capacity
const int TIMEOUT_MINUTES = 5;      // Remove MAC if not seen in X minutes
const int SCAN_INTERVAL_MS = 60000; // Send update every 60 seconds
// =========================================================

struct DeviceEntry
{
  uint8_t mac[6];
  unsigned long lastSeen;
};

#define MAX_DEVICES 500
DeviceEntry devices[MAX_DEVICES];
int deviceCount = 0;

unsigned long previousSendTime = 0;
bool wifiConnected = false;

// Callback for promiscuous mode - captures all WiFi frames
void snifferCallback(void *buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT)
    return;

  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
  if (pkt->payload[0] != 0x40)
    return; // Only probe requests

  uint8_t *mac = &pkt->payload[10];

  // Check if MAC already exists
  for (int i = 0; i < deviceCount; i++)
  {
    if (memcmp(devices[i].mac, mac, 6) == 0)
    {
      devices[i].lastSeen = millis();
      return;
    }
  }

  // Add new MAC if space available
  if (deviceCount < MAX_DEVICES)
  {
    memcpy(devices[deviceCount].mac, mac, 6);
    devices[deviceCount].lastSeen = millis();
    deviceCount++;
  }
}

void removeTimedOutDevices()
{
  unsigned long now = millis();
  unsigned long timeoutMs = TIMEOUT_MINUTES * 60UL * 1000UL;

  int writeIdx = 0;
  for (int i = 0; i < deviceCount; i++)
  {
    if (now - devices[i].lastSeen < timeoutMs)
    {
      if (writeIdx != i)
      {
        devices[writeIdx] = devices[i];
      }
      writeIdx++;
    }
  }
  deviceCount = writeIdx;
}

void sendToFirebase(int count)
{
  if (!wifiConnected)
    return;

  HTTPClient http;
  // We will write to the parent node 'venue_metrics'
  String url = "https://bar-tracker-mvp-default-rtdb.asia-southeast1.firebasedatabase.app/venue_metrics.json";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int percentage = min((count * 100) / 300, 100); // 300 is your max capacity

  // Create JSON payload
  String payload = "{\"headcount\":" + String(count) + ",\"percentage\":" + String(percentage) + "}";

  int httpResponseCode = http.PUT(payload);

  if (httpResponseCode > 0)
  {
    Serial.printf("Sent count=%d, pct=%d%% (HTTP %d)\n", count, percentage, httpResponseCode);
  }
  else
  {
    Serial.printf("Send failed (HTTP %d)\n", httpResponseCode);
  }
  http.end();
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Bar Tracker MVP Starting...");

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  wifiConnected = true;

  // Start promiscuous WiFi sniffing
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(snifferCallback);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  Serial.println("Promiscuous mode active. Scanning for probe requests...");
}

void loop()
{
  unsigned long now = millis();

  // Remove timed-out devices
  removeTimedOutDevices();

  // Send to Firebase at interval
  if (now - previousSendTime >= SCAN_INTERVAL_MS)
  {
    previousSendTime = now;

    int percentage = min((deviceCount * 100) / MAX_CAPACITY, 100);
    Serial.printf("Active devices: %d | Capacity: %d%%\n", deviceCount, percentage);

    sendToFirebase(deviceCount);
  }

  delay(100);
}