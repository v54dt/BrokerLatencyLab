# Submit Order Analysis

## Sequence Diagram

```mermaid
sequenceDiagram
    participant SDK
    participant Endpoint
    participant Frontend
    participant Midend
    participant Backend
    participant Exchange

    SDK->>Endpoint:
    Endpoint->>Frontend:
    Frontend-->>SDK: Order State Response(ACK)
    Frontend->>Midend:
    Midend->>Exchange:
    Exchange->>Midend: Order Submitted
    Midend-->>SDK: Order State Response
```

## Round Trip Time

```mermaid
sequenceDiagram
    participant AWS Taipei
    participant Endpoint
    participant Frontend
    participant Midend
    participant Backend
    participant Exchange

    AWS Taipei->>Endpoint:
    Endpoint->>AWS Taipei:
    Note left of AWS Taipei: RTT=5ms

    AWS Taipei->>Midend:
    Midend->>AWS Taipei:
    Note left of AWS Taipei: RTT=30ms


    AWS Taipei->>Exchange:
    Exchange->>AWS Taipei:
    Note left of AWS Taipei: RTT=30~42ms
```

## Profiling

### Environment

#### Hardware

- CPU: AMD Ryzen 5 5900XT

#### Software

- GCC/G++: 13.3.0
- CMake: 3.28.3
- Build: Debug (no optimization, `-O0`)

### Methodology

#### Measurement Scope

- **Start**: Before `SubmitOrder()` call
- **End**: After `SubmitOrder()` returns
- **Includes**: Local CPU processing + network round-trip time

#### Tools

- `perf record` with DWARF call graph for CPU profiling (1000 Hz sampling)
- `strace` for syscall analysis
- `std::chrono::high_resolution_clock` for wall-clock timing

### Results

#### Processing Time

- **Average**: 1.1ms (local CPU processing, excluding network wait)

#### Network Timeline

| Time (ms) | Syscall    | Size      | Description               |
| --------- | ---------- | --------- | ------------------------- |
| 0.0       | sendmsg    | 520 bytes | Submit order request      |
| 0.1       | epoll_wait | -         | Waiting for ACK           |
| 6.2       | recvmsg    | 48 bytes  | gRPC HEADERS / HTTP/2 ACK |
| 8.1       | recvmsg    | 130 bytes | ACK from Frontend         |
| 33.0      | recvmsg    | 230 bytes | Response from Midend      |
| 34.0      | return     | -         | SubmitOrder() returns     |
