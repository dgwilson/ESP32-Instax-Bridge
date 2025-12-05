#!/usr/bin/env python3
"""
TI SmartRF Packet Sniffer PSD File Parser - Final Version
Correctly parses BLE packet captures from TI CC2540/CC2541 sniffers

Record Structure (271 bytes per record):
  Offset 0:    [1 byte]  Packet Info
  Offset 1-4:  [4 bytes] Packet Number (little-endian)
  Offset 5-12: [8 bytes] Timestamp (little-endian, in clock ticks)
  Offset 13-14:[2 bytes] Total length field (little-endian)
  Offset 15:   [1 byte]  BLE payload length
  Offset 16+:  [N bytes] BLE payload
  Remainder:   Padding (zeros)
"""

import struct
from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional, Tuple
import json

# Constants
RECORD_SIZE = 271
HEADER_SIZE = 16
BLE_ADV_ACCESS_ADDR = bytes([0xd6, 0xbe, 0x89, 0x8e])

@dataclass
class BLEPacket:
    """Parsed BLE packet"""
    record_num: int
    packet_num: int
    timestamp: int
    timestamp_us: float  # Converted to microseconds
    payload_len: int
    raw_payload: bytes
    
    # BLE fields
    access_address: str = ""
    pdu_type: int = 0
    pdu_type_name: str = ""
    pdu_length: int = 0
    tx_addr_type: str = ""  # Public or Random
    
    # Address fields (depends on PDU type)
    adv_address: str = ""
    scanner_address: str = ""
    initiator_address: str = ""
    target_address: str = ""
    
    # Advertising data
    device_name: str = ""
    manufacturer: str = ""
    manufacturer_data: str = ""
    tx_power: Optional[int] = None
    flags: List[str] = field(default_factory=list)
    service_uuids: List[str] = field(default_factory=list)
    ad_structures: List[Dict] = field(default_factory=list)
    
    # Status
    rssi: int = 0
    channel: int = 0
    crc_ok: bool = False
    
    # Connection parameters (for CONNECT_IND)
    conn_access_addr: str = ""
    conn_crc_init: str = ""
    conn_interval: float = 0.0
    conn_latency: int = 0
    conn_timeout: float = 0.0

def get_pdu_type_name(pdu_type: int) -> str:
    """Get human-readable PDU type name"""
    return {
        0x00: "ADV_IND",
        0x01: "ADV_DIRECT_IND", 
        0x02: "ADV_NONCONN_IND",
        0x03: "SCAN_REQ",
        0x04: "SCAN_RSP",
        0x05: "CONNECT_IND",
        0x06: "ADV_SCAN_IND",
    }.get(pdu_type, f"UNKNOWN_0x{pdu_type:02X}")

def get_ad_type_name(ad_type: int) -> str:
    """Get advertising data type name"""
    return {
        0x01: "Flags",
        0x02: "Incomplete 16-bit UUIDs",
        0x03: "Complete 16-bit UUIDs",
        0x06: "Incomplete 128-bit UUIDs",
        0x07: "Complete 128-bit UUIDs",
        0x08: "Shortened Local Name",
        0x09: "Complete Local Name",
        0x0A: "TX Power Level",
        0x16: "Service Data (16-bit)",
        0xFF: "Manufacturer Specific",
    }.get(ad_type, f"Type_0x{ad_type:02X}")

def get_company_name(company_id: int) -> str:
    """Get Bluetooth SIG company name"""
    return {
        0x004C: "Apple",
        0x04D8: "Fujifilm",
        0x0006: "Microsoft",
        0x0059: "Nordic Semiconductor",
        0x00E0: "Google",
        0x0075: "Samsung",
        0x000D: "Texas Instruments",
        0x0046: "Mediatek",
        0x02E0: "Fitbit",
    }.get(company_id, f"Company_0x{company_id:04X}")

def bytes_to_mac(b: bytes) -> str:
    """Convert 6 bytes to MAC address string (little-endian to standard)"""
    return ':'.join(f'{x:02X}' for x in reversed(b))

def parse_ad_structures(data: bytes) -> Tuple[List[Dict], Dict[str, Any]]:
    """Parse advertising data structures, return (structures, extracted_info)"""
    structures = []
    info = {}
    
    i = 0
    while i < len(data) - 1:
        length = data[i]
        if length == 0 or i + length >= len(data):
            break
            
        ad_type = data[i + 1]
        ad_data = data[i + 2:i + 1 + length]
        
        structure = {
            'type': ad_type,
            'type_name': get_ad_type_name(ad_type),
            'data_hex': ad_data.hex(),
            'length': length - 1
        }
        
        # Parse specific types
        if ad_type == 0x01 and ad_data:  # Flags
            flags = []
            f = ad_data[0]
            if f & 0x01: flags.append("LE Limited Discoverable")
            if f & 0x02: flags.append("LE General Discoverable")
            if f & 0x04: flags.append("BR/EDR Not Supported")
            if f & 0x08: flags.append("LE+BR/EDR Controller")
            if f & 0x10: flags.append("LE+BR/EDR Host")
            structure['flags'] = flags
            info['flags'] = flags
            
        elif ad_type in (0x08, 0x09) and ad_data:  # Local Name
            try:
                name = ad_data.decode('utf-8', errors='replace').rstrip('\x00')
                structure['name'] = name
                info['device_name'] = name
            except:
                pass
                
        elif ad_type == 0x0A and ad_data:  # TX Power
            tx = ad_data[0]
            tx = tx - 256 if tx > 127 else tx
            structure['tx_power'] = tx
            info['tx_power'] = tx
            
        elif ad_type == 0xFF and len(ad_data) >= 2:  # Manufacturer Specific
            company_id = struct.unpack('<H', ad_data[0:2])[0]
            structure['company_id'] = company_id
            structure['company_name'] = get_company_name(company_id)
            structure['mfg_data'] = ad_data[2:].hex()
            info['manufacturer'] = get_company_name(company_id)
            info['manufacturer_data'] = ad_data[2:].hex()
            
        elif ad_type in (0x02, 0x03):  # 16-bit UUIDs
            uuids = []
            for j in range(0, len(ad_data), 2):
                if j + 2 <= len(ad_data):
                    uuid = struct.unpack('<H', ad_data[j:j+2])[0]
                    uuids.append(f"0x{uuid:04X}")
            structure['uuids'] = uuids
            info.setdefault('service_uuids', []).extend(uuids)
        
        structures.append(structure)
        i += length + 1
    
    return structures, info

def parse_ble_packet(raw: bytes, record_num: int, packet_num: int, timestamp: int) -> BLEPacket:
    """Parse a BLE packet from raw payload bytes"""
    pkt = BLEPacket(
        record_num=record_num,
        packet_num=packet_num,
        timestamp=timestamp,
        timestamp_us=timestamp / 32.0,  # CC2540 uses 32MHz clock
        payload_len=len(raw),
        raw_payload=raw
    )
    
    if len(raw) < 6:
        return pkt
    
    # Access address
    pkt.access_address = raw[0:4].hex()
    is_advertising = (raw[0:4] == BLE_ADV_ACCESS_ADDR)
    
    if not is_advertising:
        # Data channel packet - limited parsing
        pkt.pdu_type_name = "DATA_CHANNEL"
        return pkt
    
    # PDU header
    pdu_header = raw[4:6]
    pkt.pdu_type = pdu_header[0] & 0x0F
    pkt.pdu_type_name = get_pdu_type_name(pkt.pdu_type)
    pkt.tx_addr_type = "Random" if (pdu_header[0] >> 6) & 0x01 else "Public"
    pkt.pdu_length = pdu_header[1] & 0x3F
    
    # Extract RSSI and channel from last 2 bytes (before any CRC)
    if len(raw) >= 2:
        rssi_raw = raw[-2]
        pkt.rssi = (rssi_raw - 256 if rssi_raw > 127 else rssi_raw) - 73
        status = raw[-1]
        pkt.channel = status & 0x3F
        pkt.crc_ok = bool(status & 0x80)
    
    # Parse based on PDU type
    if pkt.pdu_type in (0x00, 0x02, 0x06):  # ADV_IND, ADV_NONCONN_IND, ADV_SCAN_IND
        if len(raw) >= 12:
            pkt.adv_address = bytes_to_mac(raw[6:12])
            # Advertising data follows
            adv_data = raw[12:-2] if len(raw) > 14 else bytes()
            if adv_data:
                pkt.ad_structures, info = parse_ad_structures(adv_data)
                pkt.device_name = info.get('device_name', '')
                pkt.manufacturer = info.get('manufacturer', '')
                pkt.manufacturer_data = info.get('manufacturer_data', '')
                pkt.tx_power = info.get('tx_power')
                pkt.flags = info.get('flags', [])
                pkt.service_uuids = info.get('service_uuids', [])
                
    elif pkt.pdu_type == 0x04:  # SCAN_RSP
        if len(raw) >= 12:
            pkt.adv_address = bytes_to_mac(raw[6:12])
            # Scan response data
            adv_data = raw[12:-2] if len(raw) > 14 else bytes()
            if adv_data:
                pkt.ad_structures, info = parse_ad_structures(adv_data)
                pkt.device_name = info.get('device_name', '')
                pkt.manufacturer = info.get('manufacturer', '')
                pkt.manufacturer_data = info.get('manufacturer_data', '')
                
    elif pkt.pdu_type == 0x03:  # SCAN_REQ
        if len(raw) >= 18:
            pkt.scanner_address = bytes_to_mac(raw[6:12])
            pkt.adv_address = bytes_to_mac(raw[12:18])
            
    elif pkt.pdu_type == 0x05:  # CONNECT_IND
        if len(raw) >= 40:
            pkt.initiator_address = bytes_to_mac(raw[6:12])
            pkt.adv_address = bytes_to_mac(raw[12:18])
            # LL Data
            ll_data = raw[18:40]
            pkt.conn_access_addr = ll_data[0:4][::-1].hex()  # Reverse for display
            pkt.conn_crc_init = ll_data[4:7].hex()
            pkt.conn_interval = struct.unpack('<H', ll_data[10:12])[0] * 1.25
            pkt.conn_latency = struct.unpack('<H', ll_data[12:14])[0]
            pkt.conn_timeout = struct.unpack('<H', ll_data[14:16])[0] * 10
    
    return pkt

def parse_psd_file(filepath: str) -> List[BLEPacket]:
    """Parse TI PSD file and return list of BLE packets"""
    with open(filepath, 'rb') as f:
        data = f.read()
    
    packets = []
    num_records = len(data) // RECORD_SIZE
    
    for rec_num in range(num_records):
        offset = rec_num * RECORD_SIZE
        
        # Parse header
        packet_info = data[offset]
        packet_num = struct.unpack('<I', data[offset+1:offset+5])[0]
        timestamp = struct.unpack('<Q', data[offset+5:offset+13])[0]
        total_len = struct.unpack('<H', data[offset+13:offset+15])[0]
        payload_len = data[offset+15]
        
        # Extract payload
        payload = data[offset+16:offset+16+payload_len]
        
        if len(payload) >= 4:
            pkt = parse_ble_packet(payload, rec_num + 1, packet_num, timestamp)
            packets.append(pkt)
    
    return packets

def analyze_and_report(filepath: str) -> Tuple[List[BLEPacket], List[BLEPacket]]:
    """Analyze PSD file and generate report"""
    print("=" * 80)
    print("TI SmartRF Packet Sniffer - BLE Capture Analysis")
    print("=" * 80)
    print(f"\nFile: {filepath}")
    
    packets = parse_psd_file(filepath)
    print(f"Total packets: {len(packets)}")
    
    # Collect device statistics
    devices = {}
    pdu_counts = {}
    
    for pkt in packets:
        # Count PDU types
        pdu_counts[pkt.pdu_type_name] = pdu_counts.get(pkt.pdu_type_name, 0) + 1
        
        # Track devices
        addr = pkt.adv_address or pkt.scanner_address or pkt.initiator_address
        if addr:
            if addr not in devices:
                devices[addr] = {
                    'name': pkt.device_name,
                    'manufacturer': pkt.manufacturer,
                    'count': 0,
                    'rssi_sum': 0,
                    'pdu_types': set(),
                    'addr_type': pkt.tx_addr_type
                }
            devices[addr]['count'] += 1
            devices[addr]['rssi_sum'] += pkt.rssi
            devices[addr]['pdu_types'].add(pkt.pdu_type_name)
            if pkt.device_name and not devices[addr]['name']:
                devices[addr]['name'] = pkt.device_name
            if pkt.manufacturer and not devices[addr]['manufacturer']:
                devices[addr]['manufacturer'] = pkt.manufacturer
    
    # Print PDU type summary
    print(f"\n{'PDU Type Distribution':=^80}")
    for pdu, count in sorted(pdu_counts.items(), key=lambda x: -x[1]):
        bar = '█' * min(50, count // 10)
        print(f"  {pdu:25s} {count:5d} {bar}")
    
    # Print device list
    print(f"\n{'Discovered BLE Devices':=^80}")
    print(f"{'Address':<20} {'Name':<22} {'Manufacturer':<15} {'Pkts':>5} {'RSSI':>6}")
    print("-" * 80)
    
    for addr, info in sorted(devices.items(), key=lambda x: -x[1]['count']):
        avg_rssi = info['rssi_sum'] / info['count'] if info['count'] else 0
        name = (info['name'] or '-')[:21]
        mfg = (info['manufacturer'] or '-')[:14]
        marker = " ◀ INSTAX" if 'INSTAX' in (info['name'] or '').upper() else ""
        print(f"{addr:<20} {name:<22} {mfg:<15} {info['count']:>5} {avg_rssi:>5.0f}{marker}")
    
    # Find INSTAX packets
    instax_addresses = set()
    for addr, info in devices.items():
        if info['name'] and 'INSTAX' in info['name'].upper():
            instax_addresses.add(addr)
    
    instax_packets = [p for p in packets if 
                     p.adv_address in instax_addresses or
                     p.scanner_address in instax_addresses or
                     p.initiator_address in instax_addresses or
                     p.target_address in instax_addresses]
    
    # INSTAX analysis
    print(f"\n{'INSTAX PRINTER ANALYSIS':=^80}")
    
    if not instax_packets:
        print("\n⚠ No INSTAX printer found in capture")
        return packets, []
    
    print(f"\n✓ Found INSTAX printer!")
    for addr in instax_addresses:
        info = devices.get(addr, {})
        print(f"\n  Device Name: {info.get('name', 'Unknown')}")
        print(f"  MAC Address: {addr}")
        print(f"  Address Type: {info.get('addr_type', 'Unknown')}")
        print(f"  Total Packets: {info.get('count', 0)}")
        print(f"  PDU Types: {', '.join(info.get('pdu_types', []))}")
    
    # Show INSTAX communication timeline
    print(f"\n{'INSTAX Communication Timeline':=^80}")
    print(f"{'#':>4} {'Time(ms)':>10} {'PDU Type':<20} {'Direction':<20} {'Info'}")
    print("-" * 80)
    
    base_time = instax_packets[0].timestamp_us if instax_packets else 0
    
    for pkt in instax_packets[:50]:
        rel_time = (pkt.timestamp_us - base_time) / 1000  # Convert to ms
        
        # Determine direction
        if pkt.pdu_type == 0x03:  # SCAN_REQ
            direction = f"{pkt.scanner_address[:8]}→INSTAX"
            info = "Scan Request"
        elif pkt.pdu_type == 0x05:  # CONNECT_IND
            direction = f"{pkt.initiator_address[:8]}→INSTAX"
            info = f"Connect (interval={pkt.conn_interval:.1f}ms)"
        elif pkt.pdu_type == 0x04:  # SCAN_RSP
            direction = "INSTAX→Phone"
            info = pkt.device_name or "Scan Response"
        elif pkt.pdu_type in (0x00, 0x02, 0x06):  # Advertising
            direction = "INSTAX→Broadcast"
            info = pkt.device_name or "Advertising"
        else:
            direction = "?"
            info = ""
        
        print(f"{pkt.packet_num:>4} {rel_time:>10.2f} {pkt.pdu_type_name:<20} {direction:<20} {info}")
    
    if len(instax_packets) > 50:
        print(f"... and {len(instax_packets) - 50} more packets")
    
    # Show advertising data details
    print(f"\n{'INSTAX Advertising Data Structures':=^80}")
    shown = set()
    for pkt in instax_packets:
        if pkt.ad_structures and pkt.adv_address not in shown:
            shown.add(pkt.adv_address)
            print(f"\nPacket #{pkt.packet_num} ({pkt.pdu_type_name}):")
            for ad in pkt.ad_structures:
                print(f"  [{ad['type_name']}]")
                if 'name' in ad:
                    print(f"    Name: {ad['name']}")
                elif 'company_name' in ad:
                    print(f"    Company: {ad['company_name']}")
                    print(f"    Data: {ad.get('mfg_data', '')[:40]}...")
                elif 'flags' in ad:
                    print(f"    Flags: {', '.join(ad['flags'])}")
                elif 'uuids' in ad:
                    print(f"    UUIDs: {', '.join(ad['uuids'])}")
                else:
                    print(f"    Data: {ad['data_hex'][:40]}...")
    
    # Connection events
    conn_events = [p for p in instax_packets if p.pdu_type == 0x05]
    if conn_events:
        print(f"\n{'Connection Events':=^80}")
        for conn in conn_events:
            print(f"\n  Packet #{conn.packet_num}")
            print(f"    Initiator (Phone): {conn.initiator_address}")
            print(f"    Target (INSTAX): {conn.adv_address}")
            print(f"    Data Access Address: {conn.conn_access_addr}")
            print(f"    CRC Init: {conn.conn_crc_init}")
            print(f"    Connection Interval: {conn.conn_interval:.2f} ms")
            print(f"    Slave Latency: {conn.conn_latency}")
            print(f"    Supervision Timeout: {conn.conn_timeout:.0f} ms")
    
    return packets, instax_packets

def export_results(packets: List[BLEPacket], instax_packets: List[BLEPacket], output_path: str):
    """Export analysis results to files"""
    
    # Export INSTAX packets as JSON
    json_data = []
    for pkt in instax_packets:
        json_data.append({
            'packet_num': pkt.packet_num,
            'timestamp_us': pkt.timestamp_us,
            'pdu_type': pkt.pdu_type_name,
            'adv_address': pkt.adv_address,
            'scanner_address': pkt.scanner_address,
            'initiator_address': pkt.initiator_address,
            'device_name': pkt.device_name,
            'manufacturer': pkt.manufacturer,
            'manufacturer_data': pkt.manufacturer_data,
            'rssi': pkt.rssi,
            'channel': pkt.channel,
            'raw_hex': pkt.raw_payload.hex(),
            'ad_structures': pkt.ad_structures,
            'conn_interval': pkt.conn_interval,
            'conn_latency': pkt.conn_latency,
            'conn_timeout': pkt.conn_timeout
        })
    
    json_file = output_path.replace('.txt', '.json')
    with open(json_file, 'w') as f:
        json.dump(json_data, f, indent=2)
    print(f"\nJSON export: {json_file}")
    
    # Export text summary
    with open(output_path, 'w') as f:
        f.write("INSTAX BLE Packet Capture - Detailed Export\n")
        f.write("=" * 70 + "\n\n")
        f.write(f"Total packets: {len(packets)}\n")
        f.write(f"INSTAX packets: {len(instax_packets)}\n\n")
        
        for pkt in instax_packets:
            f.write(f"Packet #{pkt.packet_num}\n")
            f.write(f"  Time: {pkt.timestamp_us:.2f} µs\n")
            f.write(f"  PDU: {pkt.pdu_type_name}\n")
            f.write(f"  Address: {pkt.adv_address or pkt.scanner_address or pkt.initiator_address}\n")
            if pkt.device_name:
                f.write(f"  Name: {pkt.device_name}\n")
            f.write(f"  RSSI: {pkt.rssi} dBm\n")
            f.write(f"  Raw: {pkt.raw_payload.hex()}\n")
            f.write("\n")
    
    print(f"Text export: {output_path}")

if __name__ == '__main__':
    input_file = '/mnt/user-data/uploads/INSTAX_Square_App_Connection.psd'
    output_file = '/mnt/user-data/outputs/instax_analysis.txt'
    
    all_packets, instax_packets = analyze_and_report(input_file)
    
    if instax_packets:
        export_results(all_packets, instax_packets, output_file)
        
        print(f"\n{'SUMMARY':=^80}")
        print(f"✓ Successfully analyzed INSTAX Bluetooth capture")
        print(f"  Total BLE packets: {len(all_packets)}")
        print(f"  INSTAX packets: {len(instax_packets)}")
        print(f"\n  Output files saved to /mnt/user-data/outputs/")
