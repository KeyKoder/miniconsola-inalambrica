#ifndef CUSTOM_NETWORK_PROTOCOL_H
#define CUSTOM_NETWORK_PROTOCOL_H

#include <Arduino.h>
#include <stdint.h>  // Standard integer types
#include <stddef.h>  // Standard size types
#include <WiFiUdp.h> // For the Udp object recognition

#define PACKET_MAXLEN 255
#define PACKET_NONE 0x00
#define PACKET_RECONNECT_TYPE 0x01
#define PACKET_ASSIGN_TYPE 0x0F
#define PACKET_INTRO_TYPE 0x80
#define PACKET_INPUT_TYPE 0x81

// Force the compiler not to add empty padding bytes between variables.
// This ensures the struct is exactly the same size on both ends of the connection.
#pragma pack(push, 1)

// Controller -> Screen packets

struct C2S_IntroPacket {
  char type; // PACKET_INTRO_TYPE
};


struct C2S_InputPacket {
  char type; // PACKET_INPUT_TYPE
  uint8_t playerId;
  int x;
  int y;
  int z;
  int rotX;
  int rotY;
  int rotZ;
  int tilt;
  bool buttonPressed;
};


// Screen -> Controller packets

struct S2C_AssignPacket {
  char type; // PACKET_ASSIGN_TYPE
  uint8_t playerId;
};

struct S2C_ReconnectPacket {
  char type; // PACKET_RECONNECT_TYPE
};

// For receiving packets
struct PacketResult {
    char type;
    union {
        C2S_IntroPacket intro;
        C2S_InputPacket input;
        S2C_AssignPacket assign;
        S2C_ReconnectPacket reconnect;
    } data;
};

void sendPacket(void* structPtr, size_t size);
void sendPacketTo(const char* ip, void* structPtr, size_t size);
PacketResult receivePacket();

#pragma pack(pop)

#endif