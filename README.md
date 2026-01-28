# BrokerLatencyLab

A multi-broker trading latency measurement and analysis for Taiwan securities markets.

## Brokers

| Broker                 | Market | Implementation | Status      |
| ---------------------- | ------ | -------------- | ----------- |
| Sinotrade              | Stock  | Python         | Done        |
| Concords               | Stock  | C++, Python    | Done        |
| Fubon Neo              | Stock  | C++            | In Progress |
| Fubon Neo(Fugle)       | Stock  | Python         | In Progress |
| Masterlink Nova(Fugle) | Stock  | Python         | In Progress |

## Architecture

```
┌─────────┐     ┌─────────────────┐     ┌──────────┐
│   SDK   │◀───▶│  Broker Server  │◀───▶│ Exchange │
└─────────┘     └─────────────────┘     └──────────┘
```

## Reports

- [Sinotrade PlaceOrder Profiling](brokers/sinotrade-stock-python/profiling/PlaceOrder.md)
- [Concords SubmitOrder Profiling](brokers/concords-stock-cpp/profiling/SubmitOrder.md)
