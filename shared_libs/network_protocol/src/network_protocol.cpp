#include "network_protocol.hpp"

extern WiFiUDP Udp;

void sendPacket(void* structPtr, size_t size) {
  Udp.beginPacket("192.168.4.1", 1234);
  Udp.write((uint8_t*)structPtr, size);
  Udp.endPacket();
}

void sendPacketTo(const char* ip, void* structPtr, size_t size) {
  Udp.beginPacket(ip, 1234);
  Udp.write((uint8_t*)structPtr, size);
  Udp.endPacket();
}

PacketResult receivePacket() {
    PacketResult result;
    result.type = PACKET_NONE;

    int packetSize = Udp.parsePacket();
    if (packetSize > 0) {
        // SAFETY CHECK: Ensure the incoming packet isn't huge
        if (packetSize > sizeof(result.data)) {
            Udp.flush(); // Dump the bad packet
            return result; 
        }

        uint8_t buffer[PACKET_MAXLEN];
        int len = Udp.read(buffer, PACKET_MAXLEN);

        if (len > 0) {
            // Use the actual received length, but capped at the size of our union/data
            size_t copyLen = (len < sizeof(result.data)) ? len : sizeof(result.data);
            memcpy(&result.data, buffer, copyLen);
            
            result.type = buffer[0]; 
        }
    }
    return result;
}
