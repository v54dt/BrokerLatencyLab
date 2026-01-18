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
- CPU: AMD Ryzen 5 5600XT

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

#### Syscalls

| Syscall | Description |
|---------|-------------|
| sendmsg | Send gRPC request over TLS |
| recvmsg | Receive gRPC response over TLS |
| epoll_wait | Wait for network events |

The SDK uses gRPC over TLS for communication. During `SubmitOrder()`:

1. **sendmsg**: Sends the order request (520 bytes, TLS encrypted)
2. **epoll_wait**: Waits for server response (~5-20 ms per wait)
3. **recvmsg**: Receives ACK and order state responses

#### Network Timeline

```
t=0.000 ms  sendmsg (order request)
t=0.1 ms    epoll_wait (waiting for ACK)
t=6.2 ms    recvmsg (ACK from Frontend)
t=8.1 ms    sendmsg (acknowledgement)
t=13.2 ms   recvmsg (order state from Midend)
t=33.0 ms   recvmsg (final response)
t=34.0 ms   return
```