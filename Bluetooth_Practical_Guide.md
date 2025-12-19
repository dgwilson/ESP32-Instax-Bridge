# Practical guide to understanding BLE (Bluetooth Low Energy) protocol as seen in Wireshark. 

## A simple explanation focused on what you'll actually see in your INSTAX captures.                                                               
                                                                                                                                         
### BLE Connection Flow (The Big Picture)                                                                                                  
                                                                                                                                         
  1. ADVERTISING    → Printer broadcasts "I'm here!"                                                                                     
  2. SCANNING       → iPhone listens for broadcasts                                                                                      
  3. SCAN REQUEST   → iPhone asks "Tell me more?"                                                                                        
  4. SCAN RESPONSE  → Printer sends extra info (name, etc.)                                                                              
  5. CONNECTION     → iPhone connects to printer                                                                                         
  6. SERVICE DISCOVERY → iPhone asks "What can you do?"                                                                                  
  7. DATA EXCHANGE  → Actual printing happens                                                                                            
                                                                                                                                         
### Key Packet Types You'll See in Wireshark                                                                                               
                                                                                                                                         
  1. ADV_IND (Advertising Indication)                                                                                                    
                                                                                                                                         
  - Who sends it: Printer (broadcasting to everyone)                                                                                     
  - Purpose: "I'm available! Here's my main info"                                                                                        
  - Contains:                                                                                                                            
    - Primary service UUID (the main INSTAX service)                                                                                     
    - Manufacturer data (Fujifilm ID)                                                                                                    
    - Flags (discoverable, connectable)                                                                                                  
  - Frequency: Every 100-150ms                                                                                                           
  - Wireshark filter: btle.advertising_address                                                                                           
                                                                                                                                         
  Example from capture:                                                                                                             
  Advertising Address: fa:ab:bc:87:55:00                                                                                                 
  Advertising Data:                                                                                                                      
    - Flags: 0x05 (Limited Discoverable, BR/EDR not supported)                                                                           
    - 128-bit Service: 70954782-2d83-473d-9e5f-81e1d02d5273 (INSTAX)                                                                     
    - Manufacturer: 0x004C (Fujifilm)                                                                                                    
                                                                                                                                         
  2. SCAN_REQ (Scan Request)                                                                                                             
                                                                                                                                         
  - Who sends it: iPhone (to specific printer)                                                                                           
  - Purpose: "I see you, send me more details"                                                                                           
  - Contains: Scanner's address, printer's address                                                                                       
  - Triggered by: iPhone actively scanning for printers                                                                                  
                                                                                                                                         
  3. SCAN_RSP (Scan Response) ← This is packet 42249 from a specific capture!                                                                                    
                                                                                                                                         
  - Who sends it: Printer (response to scan request)                                                                                     
  - Purpose: "Here's additional info that didn't fit in ADV_IND"                                                                         
  - Contains:                                                                                                                            
    - Device name (WIDE-205555)                                                                                                          
    - Additional services (E0FF for Wide model)                                                                                          
  - Why separate? BLE advertising packets are limited to 31 bytes, so you split data across ADV_IND and SCAN_RSP                         
                                                                                                                                         
  Packet 42249:                                                                                                                     
  Scan Response Data:                                                                                                                    
    - UUID: 0000E0FF... (Wide service) [incomplete list]                                                                                 
    - Device Name: WIDE-205555                                                                                                           
                                                                                                                                         
  4. CONNECT_IND (Connection Request)                                                                                                    
                                                                                                                                         
  - Who sends it: iPhone                                                                                                                 
  - Purpose: "I want to connect to you"                                                                                                  
  - Contains: Connection parameters (timing, intervals)                                                                                  
  - Result: Advertising stops, connection established                                                                                    
                                                                                                                                         
  5. Data Packets (After Connection)                                                                                                     
                                                                                                                                         
  - Who sends: Both devices (bidirectional)                                                                                              
  - Purpose: Actual communication (read/write characteristics)                                                                           
  - Contains: GATT protocol data                                                                                                         
  - Encrypted: Often encrypted after pairing                                                                                             
                                                                                                                                         
### GATT Structure (How BLE Services Work)                                                                                                 
                                                                                                                                         
  Think of it like a filing cabinet:                                                                                                     
                                                                                                                                         
  SERVICE (the drawer)                                                                                                                   
    └── CHARACTERISTIC (the folder)                                                                                                      
        ├── VALUE (the document)                                                                                                         
        ├── PROPERTIES (read/write/notify)                                                                                               
        └── DESCRIPTORS (metadata)                                                                                                       
                                                                                                                                         
  Example - INSTAX Service:                                                                                                              
  Service: 70954782-2d83-473d-9e5f-81e1d02d5273                                                                                          
    ├── Write Characteristic: 70954783... (send commands)                                                                                
    │   └── Properties: Write Without Response                                                                                           
    └── Notify Characteristic: 70954784... (receive responses)                                                                           
        ├── Properties: Notify                                                                                                           
        └── Descriptor: Client Characteristic Config (CCCD)                                                                              

                                                                                                                                         
## Reading Wireshark - Practical Examples                                                                                                 
                                                                                                                                         
###  Example 1: Finding the Connection Sequence                                                                                             
                                                                                                                                         
  Filter: btle.advertising_address == fa:ab:bc:87:55:00                                                                                  
                                                                                                                                         
  You'll see:                                                                                                                            
  ADV_IND → SCAN_REQ → SCAN_RSP → ADV_IND → SCAN_REQ → SCAN_RSP → CONNECT_IND                                                            
                                                                                                                                         
###  Example 2: Finding Service Discovery                                                                                                   
                                                                                                                                         
  Filter: btatt.opcode == 0x10 (Read By Group Type Request)                                                                              
                                                                                                                                         
  iPhone asking: "What services do you have?"                                                                                            
                                                                                                                                         
  Response (opcode 0x11):                                                                                                                
  Service 1: Device Information Service (standard)                                                                                       
  Service 2: 70954782... (INSTAX main service)                                                                                           
  Service 3: 0000E0FF... (Wide-specific service)                                                                                         
                                                                                                                                         
###  Example 3: Finding Print Data                                                                                                          
                                                                                                                                         
  Filter: btatt.opcode == 0x52 (Write Command)                                                                                           
                                                                                                                                         
  This is actual print data being sent:                                                                                                  
  Handle: 0x0023 (Write characteristic)                                                                                                  
  Value: 41 62 00 10 01 01 00 00... (INSTAX protocol packet)                                                                             
                                                                                                                                         
  Key Wireshark Columns to Watch                                                                                                         
                                                                                                                                         
  | Column              | Meaning                                                |                                                       
  |---------------------|--------------------------------------------------------|                                                       
  | PDU Type            | Packet type (ADV_IND, SCAN_RSP, etc.)                  |                                                       
  | Advertising Address | Who's broadcasting (printer MAC)                       |                                                       
  | ATT Opcode          | What GATT operation (read/write/notify)                |                                                       
  | Handle              | Which characteristic (0x0023 = write, 0x0025 = notify) |                                                       
  | Value               | Actual data bytes                                      |                                                       
                                                                                                                                         
###  Common Wireshark Filters for INSTAX Debugging                                                                                          
                                                                                                                                         
  - All advertising from your printer                                                                                                    
  btle.advertising_address == fa:ab:bc:87:55:00                                                                                          
                                                                                                                                         
  - Only scan responses                                                                                                                  
  btle.advertising_header.pdu_type == 0x04                                                                                               
                                                                                                                                         
  - Only connected data (after pairing)                                                                                                  
  btatt                                                                                                                                  
                                                                                                                                         
  - Only write commands (sending print data)                                                                                             
  btatt.opcode == 0x52                                                                                                                   
                                                                                                                                         
  - Only notifications (receiving printer responses)                                                                                     
  btatt.opcode == 0x1b                                                                                                                   
                                                                                                                                         
  - Find connection events                                                                                                               
  btle.advertising_header.pdu_type == 0x05                                                                                               
                                                                                                                                         
  The "Incomplete" Flag Explained Visually                                                                                               
                                                                                                                                         
  Advertising Packet (31 bytes max):                                                                                                     
  [Flags: 3 bytes]                                                                                                                       
  [Main Service UUID: 18 bytes]                                                                                                          
  [Manufacturer Data: 6 bytes]                                                                                                           
  = 27 bytes (fits!)                                                                                                                     
                                                                                                                                         
  Scan Response Packet (31 bytes max):                                                                                                   
  [Device Name "WIDE-205555": 13 bytes]                                                                                                  
  [Wide Service UUID: 18 bytes]                                                                                                          
  = 31 bytes total                                                                                                                       
                                                                                                                                         
  Since the Wide Service UUID is in the scan response but the Main Service UUID is in the advertising packet, the scan response marks its
   UUID list as "incomplete" because it's not showing ALL services in that one packet.                                                   
                                                                                                                                         
##  ATT Opcodes (For Reading Data Exchange)                                                                                                
                                                                                                                                         
  | Opcode | Name                       | What It Does                                 |                                                 
  |--------|----------------------------|----------------------------------------------|                                                 
  | 0x10   | Read By Group Type Request | "List your services"                         |                                                 
  | 0x08   | Read By Type Request       | "List characteristics in a service"          |                                                 
  | 0x0A   | Read Request               | "Give me the value"                          |                                                 
  | 0x12   | Write Request              | "Set this value (with ACK)"                  |                                                 
  | 0x52   | Write Command              | "Set this value (no ACK)" ← INSTAX uses this |                                                 
  | 0x1B   | Handle Value Notification  | "Here's data (no ACK)" ← INSTAX uses this    |                                                 
  | 0x1D   | Handle Value Indication    | "Here's data (send ACK)"                     |                                                 
                                                                                                                                         
  Your INSTAX Print Protocol in Wireshark                                                                                                
                                                                                                                                         
  When you send a print, you'll see this sequence:                                                                                       
                                                                                                                                         
  1. iPhone → Write Command (0x52): START command                                                                                        
  2. Printer → Notification (0x1B): ACK                                                                                                  
  3. iPhone → Write Command: Image chunk 1                                                                                               
  4. Printer → Notification: ACK                                                                                                         
  5. iPhone → Write Command: Image chunk 2                                                                                               
  ... (repeat for ~60 chunks)                                                                                                            
  6. iPhone → Write Command: END command                                                                                                 
  7. Printer → Notification: ACK                                                                                                         
  8. iPhone → Write Command: EXECUTE command                                                                                             
  9. Printer → Notification: "Printing..."                                                                                               
                                                                                                                                         
##  How to Read a GATT Packet in Wireshark                                                                                                 
                                                                                                                                         
  Example packet:                                                                                                                        
  Bluetooth Attribute Protocol                                                                                                           
    Opcode: Write Command (0x52)                                                                                                         
    Handle: 0x0023                                                                                                                       
    Value: 41620010010100000000000000000000d341                                                                                          
                                                                                                                                         
  Translation:                                                                                                                           
  - Opcode 0x52: Write without waiting for response                                                                                      
  - Handle 0x0023: The INSTAX write characteristic (70954783...)                                                                         
  - Value breakdown:                                                                                                                     
    - 41 62: INSTAX header (magic bytes)                                                                                                 
    - 00 10: Length (16 bytes)                                                                                                           
    - 01: Function (Query)                                                                                                               
    - 01: Operation (Device Info)                                                                                                        
    - 00 00...: Payload (empty for this query)                                                                                           
    - d3 41: Checksum                                                                                                                    
                                                                                                                                         
##  Practice Exercise                                                                                                                      
                                                                                                                                         
  Open your capture and try these:                                                                                                       
                                                                                                                                         
  1. Find the first advertising packet: Look for PDU Type = ADV_IND                                                                      
  2. Find the scan response: Look for PDU Type = SCAN_RSP (your packet 42249)                                                            
  3. Find the connection: Look for PDU Type = CONNECT_IND                                                                                
  4. Find service discovery: Filter btatt.opcode == 0x10                                                                                 
  5. Find the first print command: Filter btatt.value[0:2] == 41:62 (INSTAX header)                                                      
                                                                                                                                         
  This should give you a solid foundation to understand what you're seeing in Wireshark. 


## Practical Filtering

Will this filtering let me see the data packets or have I got something wrong?                                                         
                                                                                                                                         
  (btle.advertising_address == fa:ab:bc:87:55:00) || (btcommon.eir_ad.entry.device_name == "WIDE-205555")  && btatt                      
                                                                                                                                         
⏺ You've got the right idea, but there's a precedence issue with your filter that will cause problems. Let me explain what's happening   
  and give you better options.                                                                                                           
                                                                                                                                         
  What's Wrong With Your Filter                                                                                                          
                                                                                                                                         
  Your filter:                                                                                                                           
  (btle.advertising_address == fa:ab:bc:87:55:00) || (btcommon.eir_ad.entry.device_name == "WIDE-205555") && btatt                       
                                                                                                                                         
  Due to operator precedence (AND binds tighter than OR), Wireshark reads this as:                                                       
  (btle.advertising_address == fa:ab:bc:87:55:00)                                                                                        
  ||                                                                                                                                     
  ((btcommon.eir_ad.entry.device_name == "WIDE-205555") && btatt)                                                                        
                                                                                                                                         
  The problem:                                                                                                                           
  - btle.advertising_address only exists in advertising packets (before connection)                                                      
  - btatt only exists in data packets (after connection)                                                                                 
  - After connection, there's no "advertising_address" field anymore!                                                                    
                                                                                                                                         
  So your filter will show:                                                                                                              
  - ✅ All advertising packets from the printer                                                                                          
  - ❌ Won't show the connected data packets properly                                                                                    
                                                                                                                                         
  The Core Issue: BLE Address Fields Change                                                                                              
                                                                                                                                         
  Before connection (advertising):                                                                                                       
  - Field: btle.advertising_address                                                                                                      
  - Shows: fa:ab:bc:87:55:00                                                                                                             
                                                                                                                                         
  After connection (data exchange):                                                                                                      
  - Field: btle.advertising_address doesn't exist                                                                                        
  - Instead use: btle.slave_bd_addr or btle.master_bd_addr                                                                               
                                                                                                                                         
  Better Filter Options                                                                                                                  
                                                                                                                                         
  Option 1: Simple (shows everything from your device)                                                                                   
                                                                                                                                         
  btle.advertising_address == fa:ab:bc:87:55:00 || btle.slave_bd_addr == fa:ab:bc:87:55:00                                               
  This shows:                                                                                                                            
  - Advertising packets from the printer                                                                                                 
  - All connected traffic where printer is the slave (peripheral)                                                                        
                                                                                                                                         
  Option 2: With proper parentheses (your original intent)                                                                               
                                                                                                                                         
  (btle.advertising_address == fa:ab:bc:87:55:00 || btcommon.eir_ad.entry.device_name == "WIDE-205555") || btatt                         
  Warning: This shows ALL GATT traffic from ALL devices, not just your printer.                                                          
                                                                                                                                         
  Option 3: Most Accurate (complete conversation)                                                                                        
                                                                                                                                         
  btle.advertising_address == fa:ab:bc:87:55:00 || btle.slave_bd_addr == fa:ab:bc:87:55:00 || btle.master_bd_addr == fa:ab:bc:87:55:00   
  This captures:                                                                                                                         
  - Advertising from printer                                                                                                             
  - Data where printer is slave (peripheral) - most packets                                                                              
  - Data where printer is master (rare, but complete)                                                                                    
                                                                                                                                         
  Option 4: Only the conversation (after connection)                                                                                     
                                                                                                                                         
  btle.slave_bd_addr == fa:ab:bc:87:55:00 && btatt                                                                                       
  This shows:                                                                                                                            
  - Only GATT protocol packets                                                                                                           
  - Only from this specific printer                                                                                                      
  - Skips all the advertising noise                                                                                                      
                                                                                                                                         
  The BEST Method: Use Wireshark's Built-in Feature                                                                                      
                                                                                                                                         
  Instead of complex filters, use Wireshark's "Follow Stream" feature:                                                                   
                                                                                                                                         
  1. Find any data packet from your printer (filter: btatt)                                                                              
  2. Right-click on the packet                                                                                                           
  3. Select: Follow → Bluetooth ATT Stream                                                                                               
  4. Wireshark automatically shows only that conversation!                                                                               
                                                                                                                                         
  This is much easier and shows the complete print sequence.  

# Ah! I found the issue 

- your capture file doesn't contain any GATT/ATT data packets. It only has advertising-layer packets. Let me explain what's happening:                                                                                                              
                                                                                                                                         
  What's Actually in Your Capture                                                                                                        
                                                                                                                                         
⏺ Your capture contains only advertising-layer packets:                                                                                  
  - PDU Type 0x00 = ADV_IND (advertising)                                                                                                
  - PDU Type 0x04 = SCAN_RSP (your packet 42249)                                                                                         
  - PDU Type 0x03 = SCAN_REQ (scan requests)                                                                                             
  - PDU Type 0x05 = CONNECT_IND (connection request)                                                                                     
  - PDU Type 0x08-0x0F = Link Layer control packets                                                                                      
                                                                                                                                         
  What's missing: No GATT/ATT layer data (the actual print commands and data)                                                            
                                                                                                                                         
  Why There's No GATT Data                                                                                                               
                                                                                                                                         
  BLE sniffers (like the nRF Sniffer you're using) often lose sync after connection because:                                             
                                                                                                                                         
  1. Frequency hopping: After connecting, devices hop between 37 different channels rapidly                                              
  2. Encryption: Data is encrypted after pairing                                                                                         
  3. Sniffer limitations: nRF Sniffer needs to be explicitly "following" a connection                                                    
                                                                                                                                         
  So your capture shows:                                                                                                                 
  - ✅ Advertising (before connection)                                                                                                   
  - ✅ Connection request (CONNECT_IND)                                                                                                  
  - ❌ Missing: The actual data exchange (GATT/ATT packets)                                                                              
                                                                                                                                         
  The Right Filter for YOUR Capture                                                                                                      
                                                                                                                                         
  Since you only have advertising packets, use this simple filter:                                                                       
                                                                                                                                         
  btle.advertising_address == fa:ab:bc:87:55:00                                                                                          
                                                                                                                                         
  That's it! No need for btatt because there are no ATT packets in this file.                                                            
                                                                                                                                         
  To See Both Advertising AND Data (When You Have GATT Data)                                                                             
                                                                                                                                         
  If you had a capture with GATT data, you'd use different field names depending on the packet type:                                     
                                                                                                                                         
  btle.advertising_address == fa:ab:bc:87:55:00 || btle.access_address == 0x<connection_access_address>                                  
                                                                                                                                         
  But the connection access address changes per connection, so the easiest method is:                                                    
                                                                                                                                         
  For Captures With GATT Data - Use This:                                                                                                
                                                                                                                                         
  In Wireshark GUI:                                                                                                                      
  1. Find any packet after connection (not advertising)                                                                                  
  2. Right-click → Follow → Bluetooth Link Layer                                                                                         
  3. Wireshark automatically filters to that connection                                                                                  
                                                                                                                                         
  Or use this filter (shows everything from/to that device):                                                                             
  btle.advertising_address == fa:ab:bc:87:55:00                                                                                          
  Then after connection happens, switch to:                                                                                              
  Analyze → Follow → Bluetooth Link Layer                                                                                                
                                                                                                                                         
  Check If a Capture Has GATT Data                                                                                                       
                                                                                                                                         
  Quick test command:                                                                                                                    
  tshark -r <file.pcapng> -Y "btatt" -c 1                                                                                                
                                                                                                                                         
  If it returns nothing, there's no GATT data (like your current capture).                                                               
                                                                                                                                         
  Summary: Your Filter Problem                                                                                                           
                                                                                                                                         
  Your capture file contains:                                                                                                            
  - ✅ Advertising packets (ADV_IND, SCAN_RSP)                                                                                           
  - ✅ Connection request (CONNECT_IND)                                                                                                  
  - ❌ No GATT/ATT data (no print commands/data)                                                                                         
                                                                                                                                         
  Your filter:                                                                                                                           
  (btle.advertising_address == fa:ab:bc:87:55:00) || (btcommon.eir_ad.entry.device_name == "WIDE-205555") && btatt                       
                                                                                                                                         
  Problems:                                                                                                                              
  1. Precedence issue: AND binds tighter than OR (needs parentheses)                                                                     
  2. Pointless btatt filter: There's no GATT data in this file anyway!                                                                   
                                                                                                                                         
  The Right Filter for This Capture                                                                                                      
                                                                                                                                         
  Just use:                                                                                                                              
  btle.advertising_address == fa:ab:bc:87:55:00                                                                                          
                                                                                                                                         
  This shows all advertising-related packets from your printer (which is all you have).                                                  
                                                                                                                                         
#  If You Want to Capture GATT Data                                                                                                       
                                                                                                                                         
  ## To see actual print commands in Wireshark, you need to ensure your nRF Sniffer follows the connection:                                 
                                                                                                                                         
  1. In nRF Sniffer: Start capture                                                                                                       
  2. Find advertising packets from your printer                                                                                          
  3. Right-click → "Follow" (or click the "Follow" button)                                                                               
  4. THEN connect from iPhone                                                                                                            
  5. Sniffer will track the connection and capture GATT data                                                                             
                                                                                                                                         
  Without "following," the sniffer loses sync after connection due to frequency hopping.                                                 
                                                                                                                                         
  Filter for Captures WITH GATT Data                                                                                                     
                                                                                                                                         
  When you have GATT data, use:                                                                                                          
  btle.advertising_address == fa:ab:bc:87:55:00 || btatt                                                                                 
                                                                                                                                         
  Or simply use Wireshark's Follow → Bluetooth Link Layer feature after finding any connected packet.                                    
                                                                                                                                         
  Bottom line: Your current capture is advertising-only, so btatt filters won't do anything. For this file, just filter on the           
  advertising address.



## Wireshark doesn't have "Bluetooth" in the Follow menu! 
## The Follow submenu only has TCP, HTTP, UDP, etc. - no Bluetooth options.                                                                      
                                                                                                                                                                                                                                                                                                
  This is likely because:                                                                                                                                                                                                                                                                       
  1. Your Wireshark version might be older (Bluetooth Follow was added in v3.6+)                                                                                                                                                                                                                
  2. Or Bluetooth Follow isn't available for the packet type you clicked on                                                                                                                                                                                                                     

⏺ You have Wireshark 4.6.2 (latest version), so Bluetooth Follow should be available. The issue is you need to right-click on the correct protocol layer.                                                                                                                                       
                                                                                                                                                                                                                                                                                                
  The Problem                                                                                                                                                                                                                                                                                   
                                                                                                                                                                                                                                                                                                
  PacketLogger captures have multiple layers stacked:                                                                                                                                                                                                                                           
                                                                                                                                                                                                                                                                                                
  HCI (Host Controller Interface)                                                                                                                                                                                                                                                               
    ↓                                                                                                                                                                                                                                                                                           
  L2CAP (Logical Link Control)                                                                                                                                                                                                                                                                  
    ↓                                                                                                                                                                                                                                                                                           
  ATT (Attribute Protocol) ← You need to click HERE                                                                                                                                                                                                                                             
                                                                                                                                                                                                                                                                                                
  When you right-click on "Broadcast" or the HCI layer, Follow only shows TCP/UDP/HTTP options.                                                                                                                                                                                                 
                                                                                                                                                                                                                                                                                                
  How to Access Bluetooth Follow (Method 1)                                                                                                                                                                                                                                                     
                                                                                                                                                                                                                                                                                                
  1. Filter first: btatt                                                                                                                                                                                                                                                                        
  2. Click any packet to select it                                                                                                                                                                                                                                                              
  3. In the packet details pane (middle), expand down to the ATT layer:                                                                                                                                                                                                                         
    - Expand: "Bluetooth"                                                                                                                                                                                                                                                                       
    - Expand: "Bluetooth HCI ACL Packet"                                                                                                                                                                                                                                                        
    - Expand: "Bluetooth L2CAP Protocol"                                                                                                                                                                                                                                                        
    - Expand: "Bluetooth Attribute Protocol" ← Right-click HERE                                                                                                                                                                                                                                 
  4. Now select: Follow → Bluetooth ATT Stream                                                                                                                                                                                                                                                  
                                                                                                                                                                                                                                                                                                
  Easier Method: Use Filters Instead                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                
  Since Follow might be tricky, just use display filters to isolate conversations. This is actually more flexible!                                                                                                                                                                              
                                                                                                                                                                                                                                                                                                
  For PacketLogger Files - Best Filters                                                                                                                                                                                                                                                         
                                                                                                                                                                                                                                                                                                
  See all GATT operations:                                                                                                                                                                                                                                                                      
  btatt                                                                                                                                                                                                                                                                                         
                                                                                                                                                                                                                                                                                                
  See only write commands (iPhone → Printer):                                                                                                                                                                                                                                                   
  btatt.opcode == 0x52                                                                                                                                                                                                                                                                          
                                                                                                                                                                                                                                                                                                
  See only notifications (Printer → iPhone):                                                                                                                                                                                                                                                    
  btatt.opcode == 0x1b                                                                                                                                                                                                                                                                          
                                                                                                                                                                                                                                                                                                
  See specific characteristic (e.g., INSTAX write char):                                                                                                                                                                                                                                        
  btatt.handle == 0x0023                                                                                                                                                                                                                                                                        
                                                                                                                                                                                                                                                                                                
  See print data packets (INSTAX protocol):                                                                                                                                                                                                                                                     
  btatt.value[0:2] == 41:62                                                                                                                                                                                                                                                                     
                                                                                                                                                                                                                                                                                                
  Finding Your Print Conversation                                                                                                                                                                                                                                                               
                                                                                                                                                                                                                                                                                                
  Let me show you how to find the actual print data in your PacketLogger file:                                                                                                                                                                                                                  
                                                                                                                                                                                                                                                                                                
⏺ Bash(cd "/Users/dgwilson/Projects/ESP32-Instax-Bridge/Bluetooth Packet Capture" && /Applications/Wireshark.app/Contents/MacOS/tshark -r iPhone_to_mini_Link_3_With_Pr…)                                                                                                                       
  ⎿  34845      0x0010  41620007000055                                                                                                                                                                                                                                                          
     34851      0x0010  4162000800010152                                                                                                                                                                                                                                                        
     34853      0x0010  4162000800010251                                                                                                                                                                                                                                                        
     … +17 lines (ctrl+o to expand)                                                                                                                                                                                                                                                             
  ⎿  Shell cwd was reset to /Users/dgwilson/Desktop/Projects/Moments Project Suite                                                                                                                                                                                                              
                                                                                                                                                                                                                                                                                                
⏺ Perfect! Your PacketLogger file has INSTAX print data!                                                                                                                                                                                                                                        
                                                                                                                                                                                                                                                                                                
  I can see packets starting with 4162 (the INSTAX protocol header). Let me show you how to analyze this without needing Follow.                                                                                                                                                                
                                                                                                                                                                                                                                                                                                
  Practical Filters for Your INSTAX Capture                                                                                                                                                                                                                                                     
                                                                                                                                                                                                                                                                                                
  1. See All GATT Operations                                                                                                                                                                                                                                                                    
                                                                                                                                                                                                                                                                                                
  btatt                                                                                                                                                                                                                                                                                         
  Result: Shows all 1,139 GATT packets                                                                                                                                                                                                                                                          
                                                                                                                                                                                                                                                                                                
  2. See Only INSTAX Protocol Packets                                                                                                                                                                                                                                                           
                                                                                                                                                                                                                                                                                                
  btatt.value[0:2] == 41:62                                                                                                                                                                                                                                                                     
  Result: Shows only packets with INSTAX header (0x41 0x62)                                                                                                                                                                                                                                     
                                                                                                                                                                                                                                                                                                
  3. See Commands by Type                                                                                                                                                                                                                                                                       
                                                                                                                                                                                                                                                                                                
  Query commands (function 0x01):                                                                                                                                                                                                                                                               
  btatt.value[0:2] == 41:62 && btatt.value[4:1] == 01                                                                                                                                                                                                                                           
                                                                                                                                                                                                                                                                                                
  Print commands (function 0x02):                                                                                                                                                                                                                                                               
  btatt.value[0:2] == 41:62 && btatt.value[4:1] == 02                                                                                                                                                                                                                                           
                                                                                                                                                                                                                                                                                                
  Control commands (function 0x03):                                                                                                                                                                                                                                                             
  btatt.value[0:2] == 41:62 && btatt.value[4:1] == 03                                                                                                                                                                                                                                           
                                                                                                                                                                                                                                                                                                
  4. See Printer Responses                                                                                                                                                                                                                                                                      
                                                                                                                                                                                                                                                                                                
  btatt.opcode == 0x1b                                                                                                                                                                                                                                                                          
  Result: Shows all notifications from printer                                                                                                                                                                                                                                                  
                                                                                                                                                                                                                                                                                                
  5. See Write + Notify Together                                                                                                                                                                                                                                                                
                                                                                                                                                                                                                                                                                                
  Column filter (shows both directions):                                                                                                                                                                                                                                                        
  btatt && (btatt.opcode == 0x52 || btatt.opcode == 0x1b)                                                                                                                                                                                                                                       
                                                                                                                                                                                                                                                                                                
  View Specific Print Sequence                                                                                                                                                                                                                                                                  
                                                                                                                                                                                                                                                                                                
  Let me extract a complete print sequence for you:                                                                                                                                                                                                                                             
                                                                                                                                                                                                                                                                                                
⏺ Bash(cd "/Users/dgwilson/Projects/ESP32-Instax-Bridge/Bluetooth Packet Capture" && /Applications/Wireshark.app/Contents/MacOS/tshark -r iPhone_to_mini_Link_3_With_Pr…)                                                                                                                       
  ⎿  34845      0x52    41620007000055  259.690000000                                                                                                                                                                                                                                           
     34849      0x1b    61420010000000010002000000000049        259.750000000                                                                                                                                                                                                                   
     34851      0x52    4162000800010152        259.751000000                                                                                                                                                                                                                                   
     … +37 lines (ctrl+o to expand)                                                                                                                                                                                                                                                             
  ⎿  Shell cwd was reset to /Users/dgwilson/Desktop/Projects/Moments Project Suite                                                                                                                                                                                                              
                                                                                                                                                                                                                                                                                                
⏺ Perfect! I can see the complete INSTAX conversation with request/response pairs:                                                                                                                                                                                                              
                                                                                                                                                                                                                                                                                                
  Frame   Opcode  Direction          Packet                                                                                                                                                                                                                                                     
  34845   0x52    iPhone → Printer   4162000700... (Query)                                                                                                                                                                                                                                      
  34849   0x1b    Printer → iPhone   6142001000... (Response)                                                                                                                                                                                                                                   
  34851   0x52    iPhone → Printer   4162000800... (Next query)                                                                                                                                                                                                                                 
  ...                                                                                                                                                                                                                                                                                           
                                                                                                                                                                                                                                                                                                
  You Don't Need Follow!                                                                                                                                                                                                                                                                        
                                                                                                                                                                                                                                                                                                
  Just use these Wireshark columns for easy reading:                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                
  Set Up Custom Columns                                                                                                                                                                                                                                                                         
                                                                                                                                                                                                                                                                                                
  1. Right-click column header bar                                                                                                                                                                                                                                                              
  2. Column Preferences                                                                                                                                                                                                                                                                         
  3. Add these columns:                                                                                                                                                                                                                                                                         
                                                                                                                                                                                                                                                                                                
  | Title      | Type   | Field                              |                                                                                                                                                                                                                                  
  |------------|--------|------------------------------------|                                                                                                                                                                                                                                  
  | Direction  | Custom | `btatt.opcode == 0x52 && "→ WRITE" |                                                                                                                                                                                                                                  
  | ATT Opcode | Custom | btatt.opcode                       |                                                                                                                                                                                                                                  
  | Handle     | Custom | btatt.handle                       |                                                                                                                                                                                                                                  
  | Value      | Custom | btatt.value                        |                                                                                                                                                                                                                                  
                                                                                                                                                                                                                                                                                                
  Or simpler - just use this color filter:                                                                                                                                                                                                                                                      
                                                                                                                                                                                                                                                                                                
  Color Code the Conversation                                                                                                                                                                                                                                                                   
                                                                                                                                                                                                                                                                                                
  View → Coloring Rules → Add:                                                                                                                                                                                                                                                                  
                                                                                                                                                                                                                                                                                                
  iPhone → Printer (Write):                                                                                                                                                                                                                                                                     
  - Filter: btatt.opcode == 0x52                                                                                                                                                                                                                                                                
  - Color: Light blue background                                                                                                                                                                                                                                                                
                                                                                                                                                                                                                                                                                                
  Printer → iPhone (Notify):                                                                                                                                                                                                                                                                    
  - Filter: btatt.opcode == 0x1b                                                                                                                                                                                                                                                                
  - Color: Light green background                                                                                                                                                                                                                                                               
                                                                                                                                                                                                                                                                                                
  Now you can visually follow the conversation!                                                                                                                                                                                                                                                 
                                                                                                                                                                                                                                                                                                
  Bottom Line                                                                                                                                                                                                                                                                                   
                                                                                                                                                                                                                                                                                                
  Follow feature: Not needed for Bluetooth                                                                                                                                                                                                                                                      
  Instead use: Display filters + color coding + custom columns                                                                                                                                                                                                                                  
                                                                                                                                                                                                                                                                                                
  This is actually more powerful than Follow because you can filter by specific commands, operations, or data patterns.                                                                                                                                                                         
                                                                                                                                                                                                                                                                                                
  Want me to help you decode specific packets from your capture?   


# Perfect! Now let me give you a complete analysis of what we CAN see:                                                                                                                       
    📊 Capture Analysis Summary                                                                                                                                                           
  ✅ What Was Captured                                                                                                                                                                                                                                                                          
                                         
  Wide Simulator Advertising (210 packets):                                                                                                                                                                                                                                                     
                                                                                                                                                                                                                                                                                                
  Main Advertising Packet:                                                                                                                                                                                                                                                                      
  Device: fa:ab:bc:87:55:00                                                                                                                                                                                                                                                                     
  Service UUID: 70954782-2d83-473d-9e5f-81e1d02d5273 (INSTAX main service)                                                                                                                                                                                                                      
  Manufacturer Data: Present                                                                                                                                                                                                                                                                    
  Flags: 0x01                                                                                                                                                                                                                                                                                   
                                                                                                                                                                                                                                                                                                
  Scan Response Packet:                                                                                                                                                                                                                                                                         
  Device Name: WIDE-205555                                                                                                                                                                                                                                                                      
  Additional Service: 0000e0ff-3c17-d293-8e48-14fe2e4da212 (Wide service)                                                                                                                                                                                                                       
  Status: "incomplete" (correct - as we discussed earlier)                                                                                                                                                                                                                                      
                                                                                                                                                                                                                                                                                                
  Connection Timeline:                                                                                                                                                                                                                                                                          
  0.0s    - Capture starts                                                                                                                                                                                                                                                                      
  0.07s   - Wide simulator starts advertising                                                                                                                                                                                                                                                   
  11.57s  - Moments Print connects (CONNECT_IND to fa:ab:bc:87:55:00)                                                                                                                                                                                                                           
  11.57s+ - Connection lost (sniffer stopped tracking)                                                                                                                                                                                                                                          
                                                                                                                                                                                                                                                                                                
  ❌ What's Missing                                                                                                                                                                                                                                                                             
                                                                                                                                                                                                                                                                                                
  - No GATT/ATT data - 0 packets                                                                                                                                                                                                                                                                
  - No service discovery - Didn't capture iPhone querying services                                                                                                                                                                                                                              
  - No print protocol - Didn't capture actual print commands/data                                                                                                                                                                                                                               
  - No printer responses - No notifications captured                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                
  Why This Happened                                                                                                                                                                                                                                                                             
                                                                                                                                                                                                                                                                                                
  The nRF Sniffer wasn't "locked on" to your Wide simulator before Moments Print connected. After CONNECT_IND, the devices:                                                                                                                                                                     
  1. Hopped to data channel (one of 37 channels)                                                                                                                                                                                                                                                
  2. Started encrypted communication                                                                                                                                                                                                                                                            
  3. Sniffer stayed on advertising channels → missed everything                                                                                                                                                                                                                                 
                                                                                                                                                                                                                                                                                                
  🎯 How to Get the Print Data                                                                                                                                                                                                                                                                  
                                                                                                                                                                                                                                                                                                
  You have 3 options:                                                                                                                                                                                                                                                                           
                                                                                                                                                                                                                                                                                                
  Option 1: Mac PacketLogger (BEST - Most Reliable)                                                                                                                                                                                                                                             
                                                                                                                                                                                                                                                                                                
  Mac has built-in Bluetooth PacketLogger that captures everything:                                                                                                                                                                                                                             
                                                                                                                                                                                                                                                                                                
  # Enable PacketLogger                                                                                                                                                                                                                                                                         
  sudo log config --subsystem com.apple.bluetooth --mode level:debug                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                
  # Start capture                                                                                                                                                                                                                                                                               
  sudo log stream --predicate 'subsystem == "com.apple.bluetooth"' > bt_capture.log                                                                                                                                                                                                             
                                                                                                                                                                                                                                                                                                
  # Use Moments Print to send a print                                                                                                                                                                                                                                                           
                                                                                                                                                                                                                                                                                                
  # Stop capture (Ctrl+C)                                                                                                                                                                                                                                                                       
                                                                                                                                                                                                                                                                                                
  Then use the .pklg files from Console.app or convert the log.                                                                                                                                                                                                                                 
                                                                                                                                                                                                                                                                                                
  Option 2: ESP32 Serial Monitor (EASIEST)                                                                                                                                                                                                                                                      
                                                                                                                                                                                                                                                                                                
  Your ESP32 simulator already logs all protocol data! Just monitor it:                                                                                                                                                                                                                         
                                                                                                                                                                                                                                                                                                
⏺ Bash(ls -lh /Users/dgwilson/Projects/ESP32-Instax-Bridge/main/*.c | head -3)                                                                                                                                                                                                                  
  ⎿  -rw-r--r--  1 dgwilson  staff   121K 17 Dec 06:10 /Users/dgwilson/Projects/ESP32-Instax-Bridge/main/ble_peripheral.c                                                                                                                                                                       
     -rw-r--r--  1 dgwilson  staff    15K 22 Nov 10:32 /Users/dgwilson/Projects/ESP32-Instax-Bridge/main/ble_scanner.c                                                                                                                                                                          
     -rw-r--r--  1 dgwilson  staff    27K 24 Nov 12:20 /Users/dgwilson/Projects/ESP32-Instax-Bridge/main/console.c                                                                                                                                                                              
                                                                                                                                                                                                                                                                                                
⏺ Your ESP32 already logs the full protocol! Just connect via serial:                                                                                                                                                                                                                           
                                                                                                                                                                                                                                                                                                
  cd /Users/dgwilson/Projects/ESP32-Instax-Bridge                                                                                                                                                                                                                                               
  . ~/esp/esp-idf/export.sh                                                                                                                                                                                                                                                                     
  idf.py monitor                                                                                                                                                                                                                                                                                
                                                                                                                                                                                                                                                                                                
  Then send a print from Moments Print. You'll see:                                                                                                                                                                                                                                             
  📌 Write received [16 bytes]: 41 62 00 10 01 01 00 00...                                                                                                                                                                                                                                      
  → Decoded: Function=QUERY, Op=DEVICE_INFO                                                                                                                                                                                                                                                     
  📤 Sending response [20 bytes]: 61 42 00 14 01 01...                                                                                                                                                                                                                                          
                                                                                                                                                                                                                                                                                                
  This is better than packet captures because:                                                                                                                                                                                                                                                  
  - ✅ Decodes INSTAX protocol automatically                                                                                                                                                                                                                                                    
  - ✅ Shows request/response pairs clearly                                                                                                                                                                                                                                                     
  - ✅ No BLE capture complexity                                                                                                                                                                                                                                                                
  - ✅ Real-time feedback                                                                                                                                                                                                                                                                       
                                                                                                                                                                                                                                                                                                
  Option 3: Fix nRF Sniffer (Harder)                                                                                                                                                                                                                                                            
                                                                                                                                                                                                                                                                                                
  Use nRF Sniffer GUI to "Follow" the device before connecting:                                                                                                                                                                                                                                 
                                                                                                                                                                                                                                                                                                
  1. Start nRF Sniffer application                                                                                                                                                                                                                                                              
  2. See list of advertising devices                                                                                                                                                                                                                                                            
  3. Click "WIDE-205555" → "Follow"                                                                                                                                                                                                                                                             
  4. THEN use Moments Print to connect                                                                                                                                                                                                                                                          
  5. Sniffer will track the connection                                                                                                                                                                                                                                                          
                                                                                                                                                                                                                                                                                                
  Recommendation                                                                                                                                                                                                                                                                                
                                                                                                                                                                                                                                                                                                
  Use the ESP32 serial monitor - it's the easiest and most informative way to see what Moments Print is sending. Want me to help you set that up and analyze a print session?       
