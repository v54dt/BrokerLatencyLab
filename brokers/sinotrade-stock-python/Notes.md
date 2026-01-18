# Sinotrade SDK Notes

## Order Callback Behavior

Shioaji order callback only triggered when orders are submitted or filled. However, order placed during non-trading period will not trigger the order callback, which the order state is `PreSubmitted`. Users have to check the order state manually.

## Contract Cache

### Cache File Location
```
~/.shioaji/contracts-{version}.pkl
~/.shioaji/contracts-{version}.pkl.lock
```

### Cache Expiration Logic
The Shioaji SDK checks the contract cache during `login()`. The cache is considered expired when:
1. The date has changed (crossed midnight UTC)
2. Current time is after 14:45 (UTC+8) and the cache was created before 14:45

When expired, the SDK fetches fresh contract data from the server, which adds ~40-70 ms to processing time due to serialization of the 16.5 MB pickle file.

### Bypassing Contract Fetch

**When to use**: During non-trading hours (weekends, holidays), the Shioaji server may not respond to contract fetch requests, causing timeouts.

**Method 1: Update cache timestamp**
```bash
touch ~/.shioaji/contracts-*.pkl
```

**Method 2: Separate login and contract fetch**
```python
api.login(
    api_key=os.environ["API_KEY"],
    secret_key=os.environ["SECRET_KEY"],
    fetch_contract=False,
)
api.fetch_contracts(contracts_timeout=10000)  # Fetch separately with 10s timeout
```

If the cache is valid, `fetch_contracts()` will use the local cache without contacting the server.

## Error Tracking (Sentry)

> Source: `site-packages/shioaji/utils.py`

The Shioaji SDK sends error logs to Sentry by default. When an error occurs, the SDK uploads error messages to `sentry.io` (project ID: 1371618).

**To disable error tracking:**

Option 1: Environment variable
```bash
export LOG_SENTRY=False
```

Option 2: Login parameter
```python
api.login(..., error_tracking=False)
```
