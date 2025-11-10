#include <Arduino.h>
#include "driver/twai.h"
#include "can_messages.h"
#include "web_interface.h"
#include "softap_config.h"
#include <map>

// WiFi credentials will be loaded from NVS
SoftAPConfig::Config wifiConfig;

// TWAI (CAN) settings
const gpio_num_t TX_PIN = GPIO_NUM_3;  // GPIO4 for CAN TX
const gpio_num_t RX_PIN = GPIO_NUM_4;  // GPIO5 for CAN RX
const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();
const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
const twai_general_config_t g_config = 
{
    .mode = TWAI_MODE_NORMAL,
    .tx_io = TX_PIN,
    .rx_io = RX_PIN,
    .clkout_io = TWAI_IO_UNUSED,
    .bus_off_io = TWAI_IO_UNUSED,
    .tx_queue_len = 0,  // Size of TX queue (0 for RX only)
    .rx_queue_len = 32, // Size of RX queue
    .alerts_enabled = TWAI_ALERT_NONE,
    .clkout_divider = 0,
    .intr_flags = ESP_INTR_FLAG_LEVEL1
};

// Web server on port 80
AsyncWebServer server(80);

// Message storage
std::map<uint32_t, CANMessage> latestMessages;  // Latest state per CAN ID
std::map<uint32_t, CANMessage> previousMessages;  // Previous state per CAN ID
bool transmitCanMessage(uint32_t nId, uint8_t nBytes, const uint8_t* pData)
{
    if (nBytes > 8 || pData == nullptr)
    {
        Serial.println("Invalid CAN message parameters");
        return false;
    }

    twai_message_t message;
    message.identifier = nId;
    message.data_length_code = nBytes;
    message.rtr = 0;
    message.ss = 1;
    message.self = 0;
    message.dlc_non_comp = 0;
    message.extd = 0;  // Standard frame format (11-bit ID)
    
    // Copy data
    memcpy(message.data, pData, nBytes);
    
    // Transmit message
    esp_err_t result = twai_transmit(&message, pdMS_TO_TICKS(100));
    if (result != ESP_OK)
    {
        Serial.printf("Failed to transmit message, error: %d\n", result);
        return false;
    }
    
    return true;
}


void setup()
{
    // Initialize serial communication
    Serial.begin(115200);
    delay(1000); // Wait for serial to initialize
    Serial.println("TWAI (CAN) Receiver with Web Server");

    pinMode(GPIO_NUM_8, OUTPUT);
    pinMode(GPIO_NUM_9, INPUT);

    // Check for configuration mode (button pressed at boot)
    if (SoftAPConfig::checkConfigMode())
    {
        Serial.println("Entering configuration mode...");
        SoftAPConfig::startConfigPortal();
        // If we get here, something went wrong
        ESP.restart();
    }

    // Load WiFi configuration
    if (!SoftAPConfig::loadConfig(wifiConfig))
    {
        Serial.println("No WiFi configuration found! Please enter config mode.");
        while (1) 
        {
            digitalWrite(GPIO_NUM_8, !digitalRead(GPIO_NUM_8));
            delay(100); // Fast blink to indicate no config
        }
    }

#ifndef CAN_SENDER
    // Initialize web interface with loaded credentials
    if (!WebInterface::initialize(wifiConfig.ssid, wifiConfig.password))
    {
        Serial.println("Web interface initialization failed!");
        while (1);
    }
    WebInterface::setMessageMaps(&latestMessages, &previousMessages);
#endif

    // Install TWAI driver
    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println("Failed to install TWAI driver");
        while (1);
    }

    // Start TWAI driver
    if (twai_start() != ESP_OK) {
        Serial.println("Failed to start TWAI driver");
        while (1);
    }

    Serial.println("TWAI Initialized");

#ifndef CAN_SENDER
    // Web server is now initialized in WebInterface::initialize()
#endif
}

void IndicateMessage(const CANMessage& msg)
{
    if (msg.id == 0x124) 
    {
        if (msg.length == 2)
        {
            digitalWrite(GPIO_NUM_8, msg.data[1]); // Example: use 2nd byte to toggle LED
        }
    }
}

void CanRX()
{
    twai_message_t twai_msg;
    if (twai_receive(&twai_msg, pdMS_TO_TICKS(10)) == ESP_OK) 
    {
        // Convert TWAI message to our format
        CANMessage msg(twai_msg);

        IndicateMessage(msg);

        // Update latest/previous messages maps
        const CANMessage* previousMessage = nullptr;
        auto latestIt = latestMessages.find(msg.id);
        if (latestIt != latestMessages.end())
        {
            // Move current to previous
            previousMessages[msg.id] = latestIt->second;
            previousMessage = &latestIt->second;
        }
        else
        {
            previousMessages.erase(msg.id);
        }

        WebInterface::recordChange(msg, previousMessage);
        latestMessages[msg.id] = msg;

        // Debug output to serial
        /*
        Serial.printf("Received packet at %lu ms - ID: 0x%lX, Length: %d, Data: ",
                     msg.timestamp, msg.id, msg.length);
        for (int i = 0; i < msg.length; i++)
        {
            Serial.printf("%02X ", msg.data[i]);
        }
        Serial.println();
        */
    }
}

void CanTX()
{
    static uint32_t nNextSchedTX = 0;

    if (millis() > nNextSchedTX)
    {
        nNextSchedTX = millis() + 1000;

        static uint32_t nNextUpdateTime = 0;
        // Example CAN message to send
        static uint32_t exampleId = 0x123;
        static uint8_t exampleData[8] = {0x01, 0x02, 0xFF, 0x04, 0x05, 0x06, 0x07, 0x08};

        if (nNextUpdateTime < millis())
        {
            nNextUpdateTime = millis() + 5000; // Update every 5 second
            exampleData[1]++;
        }

        transmitCanMessage(exampleId, 8, exampleData);
    }
    static int nLastBtn = HIGH;
    if (digitalRead(GPIO_NUM_9) != nLastBtn)
    {
        // Send another CAN message when the button is changed#
        nLastBtn = digitalRead(GPIO_NUM_9);
        digitalWrite(GPIO_NUM_8, nLastBtn);
        static uint32_t buttonPressId = 0x124;
        uint8_t buttonData[2] = {0xAA, 0xBB};
        buttonData[1] = nLastBtn ? 0x01 : 0x00;
        transmitCanMessage(buttonPressId, 2, buttonData);
        Serial.println("Sent button press");
        delay(50); // Debounce delay
    }
}

void loop()
{
    #ifdef CAN_SENDER
        CanTX();
    #else
        // Continuously receive CAN messages    
        CanRX();
    #endif
}