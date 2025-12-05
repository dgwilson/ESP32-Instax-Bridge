#!/usr/bin/env python3
"""
TI SmartRF PSD to PCAP Converter
Converts Texas Instruments Packet Sniffer (.psd) files to PCAP format for Wireshark

Usage:
    python psd_to_pcap.py input.psd output.pcap
    
The output PCAP uses DLT_BLUETOOTH_LE_LL (251) link type for BLE packets.
"""

import struct
import sys
import os
from typing import List, Tuple

# PSD file constants
RECORD_SIZE = 271
HEADER_SIZE = 16
BLE_ADV_ACCESS_ADDR = bytes([0xd6, 0xbe, 0x89, 0x8e])

# PCAP constants
PCAP_MAGIC = 0xa1b2c3d4  # Standard pcap magic number
PCAP_VERSION_MAJOR = 2
PCAP_VERSION_MINOR = 4
PCAP_THISZONE = 0  # GMT
PCAP_SIGFIGS = 0
PCAP_SNAPLEN = 65535
DLT_BLUETOOTH_LE_LL = 251  # Bluetooth Low Energy Link Layer
DLT_USER0 = 147  # Alternative: User-defined for BTLE dissector


def parse_psd_packets(filepath: str) -> List[Tuple[int, int, bytes]]:
    """
    Parse PSD file and extract packets.
    Returns list of (timestamp_us, packet_num, payload_bytes)
    """
    with open(filepath, 'rb') as f:
        data = f.read()
    
    packets = []
    num_records = len(data) // RECORD_SIZE
    
    for rec_num in range(num_records):
        offset = rec_num * RECORD_SIZE
        
        # Parse header
        # [1] packet_info, [4] packet_num, [8] timestamp, [2] total_len, [1] payload_len
        packet_num = struct.unpack('<I', data[offset+1:offset+5])[0]
        timestamp = struct.unpack('<Q', data[offset+5:offset+13])[0]
        payload_len = data[offset+15]
        
        # Extract payload (BLE packet)
        payload = data[offset+16:offset+16+payload_len]
        
        if len(payload) >= 4:
            # Convert timestamp from clock ticks to microseconds
            # CC2540 uses 32MHz clock
            timestamp_us = timestamp / 32.0
            packets.append((timestamp_us, packet_num, payload))
    
    return packets


def write_pcap_header(f, link_type: int = DLT_BLUETOOTH_LE_LL):
    """Write PCAP global header"""
    # Global header: magic, version_major, version_minor, thiszone, sigfigs, snaplen, network
    header = struct.pack('<IHHIIII',
        PCAP_MAGIC,
        PCAP_VERSION_MAJOR,
        PCAP_VERSION_MINOR,
        PCAP_THISZONE,
        PCAP_SIGFIGS,
        PCAP_SNAPLEN,
        link_type
    )
    f.write(header)


def write_pcap_packet(f, timestamp_us: float, data: bytes):
    """Write a single packet to PCAP file"""
    # Convert microseconds to seconds and microseconds
    ts_sec = int(timestamp_us / 1_000_000)
    ts_usec = int(timestamp_us % 1_000_000)
    
    # Packet header: ts_sec, ts_usec, incl_len, orig_len
    pkt_header = struct.pack('<IIII',
        ts_sec,
        ts_usec,
        len(data),
        len(data)
    )
    f.write(pkt_header)
    f.write(data)


def convert_psd_to_pcap(input_path: str, output_path: str, link_type: int = DLT_BLUETOOTH_LE_LL):
    """Convert PSD file to PCAP format"""
    print(f"Reading: {input_path}")
    packets = parse_psd_packets(input_path)
    print(f"Parsed {len(packets)} packets")
    
    # Get base timestamp from first packet
    if packets:
        base_time = packets[0][0]
    else:
        base_time = 0
    
    print(f"Writing: {output_path}")
    with open(output_path, 'wb') as f:
        write_pcap_header(f, link_type)
        
        for timestamp_us, pkt_num, payload in packets:
            # Use relative timestamp from start of capture
            rel_time = timestamp_us - base_time
            write_pcap_packet(f, rel_time, payload)
    
    file_size = os.path.getsize(output_path)
    print(f"Done! Output size: {file_size:,} bytes")
    print(f"\nTo open in Wireshark:")
    print(f"  wireshark {output_path}")
    print(f"\nUseful Wireshark filters for INSTAX:")
    print(f"  btle.advertising_address == fa:ab:bc:18:89:42")
    print(f"  btcommon.eir_ad.entry.device_name contains \"INSTAX\"")


def convert_psd_to_pcap_with_ppi(input_path: str, output_path: str):
    """
    Convert PSD to PCAP with PPI (Per-Packet Information) headers.
    This format includes RSSI and channel information.
    """
    DLT_PPI = 192
    
    print(f"Reading: {input_path}")
    
    with open(input_path, 'rb') as f:
        data = f.read()
    
    packets = []
    num_records = len(data) // RECORD_SIZE
    
    for rec_num in range(num_records):
        offset = rec_num * RECORD_SIZE
        
        packet_num = struct.unpack('<I', data[offset+1:offset+5])[0]
        timestamp = struct.unpack('<Q', data[offset+5:offset+13])[0]
        payload_len = data[offset+15]
        payload = data[offset+16:offset+16+payload_len]
        
        if len(payload) >= 4:
            # Extract RSSI and channel from last 2 bytes
            if len(payload) >= 2:
                rssi_raw = payload[-2]
                rssi = (rssi_raw - 256 if rssi_raw > 127 else rssi_raw) - 73
                channel = payload[-1] & 0x3F
            else:
                rssi, channel = -100, 0
            
            timestamp_us = timestamp / 32.0
            packets.append((timestamp_us, payload, rssi, channel))
    
    print(f"Parsed {len(packets)} packets")
    
    if packets:
        base_time = packets[0][0]
    else:
        base_time = 0
    
    print(f"Writing: {output_path}")
    with open(output_path, 'wb') as f:
        write_pcap_header(f, DLT_PPI)
        
        for timestamp_us, payload, rssi, channel in packets:
            rel_time = timestamp_us - base_time
            
            # Build PPI header
            # PPI header: version (1), flags (1), header_len (2), dlt (4)
            ppi_version = 0
            ppi_flags = 0
            ppi_dlt = DLT_BLUETOOTH_LE_LL
            
            # PPI field header for 802.11 common (we'll use it for BLE info)
            # Field type (2), field length (2), field data
            # Using a simple PPI structure
            ppi_header_len = 8  # Minimum PPI header
            
            ppi_header = struct.pack('<BBHI',
                ppi_version,
                ppi_flags,
                ppi_header_len,
                ppi_dlt
            )
            
            # Combine PPI header with BLE payload
            full_packet = ppi_header + payload
            
            write_pcap_packet(f, rel_time, full_packet)
    
    file_size = os.path.getsize(output_path)
    print(f"Done! Output size: {file_size:,} bytes")


def main():
    if len(sys.argv) < 2:
        print("TI SmartRF PSD to PCAP Converter")
        print("=" * 40)
        print(f"\nUsage: {sys.argv[0]} <input.psd> [output.pcap]")
        print("\nOptions:")
        print("  If output is not specified, uses input filename with .pcap extension")
        print("\nExample:")
        print(f"  {sys.argv[0]} capture.psd capture.pcap")
        sys.exit(1)
    
    input_path = sys.argv[1]
    
    if len(sys.argv) >= 3:
        output_path = sys.argv[2]
    else:
        # Generate output filename
        base = os.path.splitext(input_path)[0]
        output_path = base + '.pcap'
    
    if not os.path.exists(input_path):
        print(f"Error: Input file not found: {input_path}")
        sys.exit(1)
    
    convert_psd_to_pcap(input_path, output_path)


if __name__ == '__main__':
    main()
