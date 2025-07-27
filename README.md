# RIP (Routing Information Protocol) Assignment Instruction
Your RIP application (router process) should be run with a command line argument, which is a config file (details of the config file is discussed later in this document). You should run your RIP applications in separate shells. Router failure will be simulated by killing a router process. The syntax to run the RIP application:
RIP <config_file>

Output:
Your program should print its route vectors (i.e. routing tables) as soon as it gets/sends one from/to any of its neighbors. Feel free to show more debugging info on the console.

Compulsory Features:
- Implement Periodic, Expiration, and Garbage Collection Timers


Config File Format:
This is a three column text file meant to describe the  connectivity information among neighbors. Each line describes the IP address of this router and that of one of its neighbor's. The first column represents the interface ID, the second column of each line is the IP address of an interface of this router and the third column represents the IP address of a directly connected neighbor. A sample config file:
1  192.168.1.1   192.168.1.2
2  192.168.2.1.  192.168.2.254
3  192.168.9.2.  192.168.9.1
From the above mentioned sample config file, it can be assumed that there are three interfaces of this router and the IP addresses of those are 192.168.1.1, 192.168.2.1, 192.168.9.2. The third column represents the IP address of a neighboring router. So the first line of the config file says that there is a link between interface 1 (IP address 192.168.1.1) and an interface of a neighboring router whose IP address is 192.168.1.2. And so on..

# RIP (Routing Information Protocol) Implementation

A multithreaded C implementation of the RIP distance-vector routing protocol with support for periodic updates, expiration timers, and garbage collection using port-based communication.

## Overview

This implementation simulates a RIP router that maintains routing tables, exchanges routing information with neighbors using UDP ports, and handles route timeouts and garbage collection. Routers are identified by numeric IDs and communicate through specified port numbers.

## Features

- **Distance Vector Routing**: Implements routing table management with metric-based path selection
- **Split Horizon with Poison Reverse**: Prevents routing loops by advertising metric 16 to source neighbors
- **Timeout Management**: Routes expire after 180 seconds (6 * UPDATE_INTERVAL)
- **Garbage Collection**: Invalid routes are purged after 120 seconds (4 * UPDATE_INTERVAL) 
- **Triggered Updates**: Immediate route advertisements when topology changes occur
- **Multithreaded Architecture**: Separate threads for listening, updating, and timer management
- **Thread-Safe Operations**: Mutex protection for shared data structures

## Architecture

### Core Components

1. **Route Table Management**
   - Linked list structure storing destination, metric, next-hop, interface port
   - Individual timeout and garbage collection timers per route
   - Thread-safe access using `access_route_table` mutex

2. **Interface Management**
   - **Input Interfaces**: Ports for receiving RIP packets from neighbors
   - **Output Interfaces**: Port-ID pairs for sending packets to specific neighbors
   - UDP socket communication with port-based addressing

3. **Timer System**
   - **Periodic Updates**: Every 30 seconds (UPDATE = 5, but sleep(UPDATE) in code)
   - **Route Timeout**: 180 seconds (TIMEOUT = UPDATE*6 = 30 seconds)
   - **Garbage Collection**: 120 seconds (GARBAGE = UPDATE*4 = 20 seconds)
   - Individual pthread timer for each route entry

4. **Packet Processing**
   - Custom RIP-like packet format with Header and Body structures
   - Split horizon: advertises metric 16 to routes learned from specific neighbors
   - Packet validation and neighbor verification

### Threading Model

- **Main Thread**: Initialization, creates other threads, then calls exit_program()
- **Listener Threads**: One per input port (`listen_process`) for receiving packets
- **Update Thread**: Periodic route advertisements (`update_process`)
- **Timer Threads**: Individual `time_handler` threads for each route's timeout/garbage timers

## Configuration File Format

Three-line configuration with colon-separated key-value pairs:

```
id: <router_id>
inputs: <port1>,<port2>,<port3>,...
outputs: <port1>-<neighbor_id>,<port2>-<neighbor_id>,...
```

### Example Configuration
```
id: 1
inputs: 5001,5002,5003
outputs: 6001-2,6002-3,6003-4
```

This means:
- Router ID: 1
- Listens on ports: 5001, 5002, 5003
- Sends to: port 6001 (neighbor ID 2), port 6002 (neighbor ID 3), port 6003 (neighbor ID 4)

## Usage

### Compilation
```bash
gcc -o rip rip.c -lpthread
```

### Execution
```bash
./rip <config_file>
```

### Example Multi-Router Setup
```bash
# Terminal 1 (Router 1)
./rip router1.conf

# Terminal 2 (Router 2)  
./rip router2.conf

# Terminal 3 (Router 3)
./rip router3.conf
```

## Implementation Details

### Data Structures

```c
struct Route_Table {
    struct Route_Table *next;
    int address;                // Destination router ID
    uint32_t metric;           // Hop count (max 15, 16 = infinity)
    int next_hop;              // Next hop router ID
    struct AllTimer timeout;   // 180-second expiration timer
    struct AllTimer garbage;   // 120-second cleanup timer
    int interfc;               // Interface port number
};

struct Interface {
    int port;                  // Port number
    int sockfd;               // Socket file descriptor
    struct Neighbour *dest;   // Discovered neighbor info
    bool got_ne;              // Neighbor discovered flag
    pthread_t listener;       // Listener thread
    pthread_mutex_t send_socket; // Send synchronization
};
```

### Key Algorithms

1. **Route Update Logic**:
   - Same next-hop + same metric: Reset timeout timer
   - Same next-hop + different metric: Update metric, reset timer
   - Different next-hop + better metric: Update route info
   - Metric ≥ 16: Set to infinity, trigger update, start garbage collection

2. **Split Horizon Implementation**:
   ```c
   if (node->next_hop != nexthop) re.metric = node->metric;
   else re.metric = 16;  // Poison reverse
   ```

3. **Timer Management**:
   - Each route has independent timeout and garbage collection timers
   - Timers run in separate threads with mutex-protected state
   - Route removal occurs during garbage collection phase

### Packet Format

```c
struct Header {
    byte command;      // Always 1 (Response)
    byte version;      // Always 1 (RIPv1)
    short int id;      // Sender router ID
};

struct Body {
    short int addrfamily; // Always 2 (IP)
    short int zero;       // Padding
    uint32_t destination; // Destination router ID
    uint32_t zero1, zero2; // Padding
    uint32_t metric;      // Hop count
};
```

### Concurrency Control

- **Route Table Lock**: `access_route_table` mutex protects routing table operations
- **Timer Locks**: Individual `change_time` mutexes for each timer
- **Socket Locks**: Per-interface `send_socket` mutexes for UDP transmission
- **Graceful Shutdown**: `pthread_join()` ensures proper thread cleanup

## Output

The program prints routing tables periodically showing:
- Source interface
- Destination router ID  
- Metric (hop count)
- Next hop router ID
- Timeout remaining
- Garbage collection timer

Example output:
```
1 no. Router
Src    Dest  Metric  NextHop  Timeout  Garbage
5001   2     1       2        180      0
5002   3     2       3        175      0
```

## Protocol Behavior

- **Metric Limit**: Maximum 15 hops, 16 represents infinity
- **Update Interval**: 30 seconds for periodic advertisements
- **Route Timeout**: 180 seconds without updates
- **Garbage Collection**: 120 seconds after timeout
- **Triggered Updates**: Sent immediately when routes change
- **Loop Prevention**: Split horizon with poison reverse

## Error Handling

- Packet size validation
- Socket operation error checking  
- Thread creation/join error handling
- Graceful cleanup on program termination
- Memory deallocation for dynamic structures

## Dependencies

- POSIX threads (pthread)
- Unix socket API (sys/socket.h)
- Standard C libraries
- Linux/Unix environment
