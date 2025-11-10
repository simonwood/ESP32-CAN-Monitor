#pragma once

#include <WiFi.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

class SoftAPConfig
{
public:
    static const gpio_num_t CONFIG_PIN = GPIO_NUM_10;  // Same as button input from main
    static const char* AP_SSID;  // Will be set to "RCLS-XXXXXX" where X are MAC digits
    static const char* AP_PASSWORD;  // Will be set to "configure"
    static const char* PORTAL_HOSTNAME;  // For captive portal DNS

    // Configuration structure
    struct Config {
        char ssid[33];        // 32 chars + null terminator
        char password[65];    // 64 chars + null terminator
        // Add other config items here as needed
    };

    static bool checkConfigMode();  // Returns true if button pressed at boot
    static bool startConfigPortal(); // Start SoftAP and captive portal
    static bool loadConfig(Config& config);  // Load from NVS
    static bool saveConfig(const Config& config);  // Save to NVS

private:
    static AsyncWebServer server;
    static DNSServer dnsServer;
    static Preferences preferences;
    
    static void setupConfigPage();
    static void handleConfigSave(AsyncWebServerRequest* request);
    static String generateHTML();
    static void startDNSServer();
};