#pragma once

#include <Arduino.h>
#include <map>
#include "driver/twai.h"

//#define CAN_SENDER

// Data structures to store CAN messages
struct CANMessage
{
    uint32_t timestamp;
    uint32_t id;
    uint8_t length;
    uint8_t data[8];
    
    // Constructor to convert from TWAI message
    CANMessage(const twai_message_t& msg)
    {
        timestamp = millis();
        id = msg.identifier;
        length = msg.data_length_code;
        memcpy(data, msg.data, length);
    }
    
    CANMessage() {} // Default constructor
};