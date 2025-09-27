# BrokerLatencyLab - Multi-Broker Trading Latency Measurement

BrokerLatencyLab is a containerized multi-broker trading system for measuring order latency across different securities brokers.

## Quick Start

1. **Configure brokers** in `config.toml`
2. **Generate Docker Compose** configuration
3. **Start services**

```bash
# Generate docker-compose.yml from config
python3 scripts/generate-compose.py > docker-compose.yml

# Start enabled brokers
docker compose up -d

# View logs
docker compose logs -f
```

## Configuration

Edit `config.toml` to configure brokers:

### Enable/Disable Brokers
```toml
[brokers.instance]
enabled = true    # Set to false to disable
replicas = 1      # Number of instances
```

### Broker Credentials
```toml
[[brokers.instance]]
user_id = "your_user_id"
password = "your_password"
account = "your_account"
cert_file = "/app/certs/cert.pfx"
cert_password = "pfx_password"
cert_host_path = "./certs/your_cert.pfx"
```

### Resource Limits
```toml
[brokers.instance.resources]
cpu_limit = "1.0"
memory_limit = "512M"
cpu_reservation = "0.5"
memory_reservation = "256M"
```

## Multiple Replicas

Run multiple instances of the same broker:

```toml
[brokers.instance]
replicas = 3
instances = [
  { name = "replica-1", user_id = "account1", password = "pass1", account = "acc1" },
  { name = "replica-2", user_id = "account2", password = "pass2", account = "acc2" },
  { name = "replica-3", user_id = "account3", password = "pass3", account = "acc3" }
]
```

This creates services: `instance-1`, `instance-2`, `instance-3`


## Commands

```bash
# Generate docker-compose.yml
python3 scripts/generate-compose.py > docker-compose.yml

# Start all enabled services
docker compose up -d

# Stop all services
docker compose down

# View logs
docker compose logs -f [service-name]

# Scale specific service
docker compose up -d --scale instance=3

# Restart specific service
docker compose restart instance
```

## Architecture

```
BrokerLatencyLab/
├── config.toml                    # Global configuration
├── docker-compose.yml            # Generated from config.toml
├── scripts/
│   └── generate-compose.py       # Config to Docker Compose generator
└── brokers/
    ├── ...
    └── ...
```

## Latency Measurement

The system measures round-trip latency:
1. **Submit Order** → Record timestamp
2. **Receive Confirmation** → Calculate latency
3. **Cancel Order** → Complete cycle
4. **Send Report** → API endpoint
5. **Repeat** → Every N seconds

## Monitoring

View real-time latency data and logs:

```bash
# Follow all logs
docker compose logs -f

# Monitor specific broker
docker compose logs -f instance

# Check service status
docker compose ps
```