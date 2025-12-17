#!/usr/bin/env python3
import sys

# Read hex dump and look for INSTAX packets
with open('iPhone_to_mini_Link_3_AppleCapture.pklg', 'rb') as f:
    data = f.read()

# Find all occurrences of INSTAX headers
packets = []
for i in range(len(data) - 20):
    # Look for "41 62" (from app) or "61 42" (from printer)
    if (data[i] == 0x41 and data[i+1] == 0x62) or (data[i] == 0x61 and data[i+1] == 0x42):
        # Extract potential packet
        length = (data[i+2] << 8) | data[i+3]
        if 7 <= length <= 100:  # Reasonable packet size
            packet = data[i:i+length+1]
            direction = "APP→PRINTER" if data[i] == 0x41 else "PRINTER→APP"
            packets.append((direction, packet))

# Print unique packets
seen = set()
for direction, packet in packets[:50]:  # First 50
    hex_str = ' '.join(f'{b:02x}' for b in packet[:min(20, len(packet))])
    if hex_str not in seen:
        seen.add(hex_str)
        print(f"{direction}: {hex_str}")
        
        # Decode if it's an info query/response
        if len(packet) >= 8:
            func = packet[4]
            op = packet[5]
            if func == 0x00 and op == 0x01 and direction == "APP→PRINTER":
                if len(packet) >= 8:
                    query_type = packet[7]
                    print(f"  → Info query type: 0x{query_type:02x}")
            elif func == 0x00 and op == 0x01 and direction == "PRINTER→APP":
                if len(packet) >= 10:
                    query_type = packet[7]
                    str_len = packet[8]
                    if len(packet) >= 9 + str_len:
                        data_str = packet[9:9+str_len].decode('ascii', errors='ignore')
                        print(f"  ← Response to query 0x{query_type:02x}: \"{data_str}\"")
        print()

