#pragma once

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <map>
#include "can_messages.h"  // Forward declaration of CANMessage type



class WebInterface
{
public:
    static bool initialize(const char* ssid, const char* password);
    static void setMessageMaps(
        const std::map<uint32_t, CANMessage>* recent,
        const std::map<uint32_t, CANMessage>* latest,
        const std::map<uint32_t, CANMessage>* previous);

private:
    static AsyncWebServer server;
    static const std::map<uint32_t, CANMessage>* recentMessages;
    static const std::map<uint32_t, CANMessage>* latestMessages;
    static const std::map<uint32_t, CANMessage>* previousMessages;
    
    static String formatByte(uint8_t byte, bool highlight);
    static String generateHtml();
    static String generateRecentRows();
    static String generateLatestRows();
    
    static const char* HTML_TEMPLATE;
};