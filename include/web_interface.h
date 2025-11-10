#pragma once

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <map>
#include <vector>
#include "can_messages.h"  // Forward declaration of CANMessage type

class WebInterface
{
public:
    static bool initialize(const char* ssid, const char* password);
    static void setMessageMaps(
        const std::map<uint32_t, CANMessage>* latest,
        const std::map<uint32_t, CANMessage>* previous);
    static void recordChange(const CANMessage& current, const CANMessage* previous);
    static void setTransmitCallback(bool (*callback)(uint32_t id, uint8_t length, const uint8_t* data));

private:
    static AsyncWebServer server;
    static const std::map<uint32_t, CANMessage>* latestMessages;
    static const std::map<uint32_t, CANMessage>* previousMessages;
    static bool (*transmitCallback)(uint32_t id, uint8_t length, const uint8_t* data);

    static String formatByte(uint8_t byte, bool highlight);
    static String generateHtml();
    static String generateLatestRows();
    static String generateFilteredPage();
    static String generateIdListJson();
    static std::vector<uint32_t> parseIdList(const String& rawIds);
    static String generateFilteredRows(const std::vector<uint32_t>& ids);
    
    static const char* HTML_TEMPLATE;
    static const char* FILTERED_TEMPLATE;
};
