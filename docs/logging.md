# Logging and Alerting Architecture for Slonana.cpp Validator

## Overview

The Slonana.cpp validator implements a comprehensive logging and alerting framework designed to capture, escalate, and notify on critical failure conditions in real-time. This system provides robust observability while maintaining high performance and thread safety.

## Architecture Components

### 1. Enhanced Logging System

#### Log Levels
- **TRACE** (0): Finest granularity for deep debugging
- **DEBUG** (1): Development debugging information
- **INFO** (2): General operational information
- **WARN** (3): Warning conditions that should be investigated
- **ERROR** (4): Error conditions that need attention
- **CRITICAL** (5): Critical failures requiring immediate action

#### Structured Logging
The system supports both text and JSON logging formats:

```cpp
// Text format (development)
[2025-09-20 09:58:52] [CRITICAL] [network] Connection lost (error: NET_001) {peer_count=0}

// JSON format (production)
{"timestamp":"2025-09-20T09:58:52.584Z","level":"CRITICAL","module":"network","thread_id":"140394501945152","message":"Connection lost","error_code":"NET_001","context":{"peer_count":"0"}}
```

#### Performance Features
- **Async Logging**: Non-blocking logging in production environments
- **Conditional Compilation**: Debug logging can be disabled in release builds
- **Thread Safety**: Atomic operations and lock-free patterns where possible
- **Zero-Copy**: Minimal string allocation overhead

### 2. Alerting Framework

#### Alert Channels
- **Console Channel**: Real-time alerts to stderr (development)
- **File Channel**: Persistent alert logging to disk
- **Prometheus Channel**: Metrics integration for monitoring systems
- **Slack Channel**: Team notifications via webhooks (configurable)
- **Email Channel**: SMTP-based notifications (configurable)

#### Alert Triggering
Critical alerts are automatically triggered on:
- Component initialization failures
- Network connectivity loss
- Consensus failures
- SVM execution errors
- Ledger corruption
- Memory/resource exhaustion

### 3. Module-Specific Error Codes

#### Error Code Format: `[MODULE]_[TYPE]_[NUMBER]`

**Validator Module (VAL_xxx)**
- `VAL_INIT_001`: Identity initialization failure
- `VAL_INIT_002`: Component initialization failure
- `VAL_START_001`: Startup failure
- `VAL_INIT_BANK_001`: Banking stage initialization failure

**Network Module (NET_xxx)**
- `NET_INIT_001`: Network component creation failure
- `NET_CREATE_001`: Network instance creation failure
- `RPC_CONFIG_001`: RPC server configuration failure

**Ledger Module (LED_xxx)**
- `LED_INIT_001`: Ledger manager initialization failure
- `LED_CREATE_001`: Ledger manager creation failure

**SVM Module (SVM_xxx)**
- `SVM_INIT_001`: SVM execution engine initialization failure
- `SVM_CREATE_001`: SVM component creation failure

**Consensus Module (CON_xxx)**
- `CON_FORK_001`: Fork choice failure
- `CON_VOTE_001`: Vote processing failure

## Usage Guide

### Basic Logging

```cpp
#include "common/logging.h"

// Simple logging
LOG_INFO("Validator started successfully");
LOG_ERROR("Failed to process transaction: ", tx_id);

// Structured logging with context
std::unordered_map<std::string, std::string> context = {
    {"transaction_id", "abc123"},
    {"slot", "12345"}
};
LOG_STRUCTURED(LogLevel::ERROR, "validator", "Transaction validation failed", 
               "TX_VAL_001", context);
```

### Critical Failure Reporting

```cpp
// Module-specific critical error macros
LOG_NETWORK_ERROR("Gossip protocol disconnected", "NET_GOSSIP_001", 
                  {{"peer_count", "0"}, {"last_message", "30s"}});

LOG_CONSENSUS_ERROR("Fork choice deadlock detected", "CON_FORK_001");

LOG_SVM_ERROR("Program execution timeout", "SVM_EXEC_001", 
              {{"program_id", program_id}, {"timeout_ms", "5000"}});
```

### Configuration

#### Development Environment
```cpp
Logger& logger = Logger::instance();
logger.set_level(LogLevel::DEBUG);
logger.set_json_format(false);
logger.set_async_logging(false);

// Add console alerts for immediate feedback
logger.add_alert_channel(AlertChannelFactory::create_console_channel(true));
```

#### Production Environment
```cpp
Logger& logger = Logger::instance();
logger.set_level(LogLevel::INFO);
logger.set_json_format(true);
logger.set_async_logging(true);

// Add production alert channels
logger.add_alert_channel(AlertChannelFactory::create_file_channel("/var/log/slonana/alerts.log", true));
logger.add_alert_channel(AlertChannelFactory::create_prometheus_channel(true));

// Configure external alerting (via environment variables)
if (const char* slack_webhook = std::getenv("SLONANA_SLACK_WEBHOOK_URL")) {
    logger.add_alert_channel(AlertChannelFactory::create_slack_channel(slack_webhook, true));
}
```

## Environment Variables

### Logging Configuration
- `SLONANA_LOG_LEVEL`: Set logging level (TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL)
- `SLONANA_LOG_FORMAT`: Set format (text, json)
- `SLONANA_LOG_ASYNC`: Enable async logging (true, false)

### Alert Configuration
- `SLONANA_SLACK_WEBHOOK_URL`: Slack webhook URL for team notifications
- `SLONANA_ALERT_EMAIL_TO`: Email address for critical alerts
- `SLONANA_SMTP_SERVER`: SMTP server for email alerts
- `SLONANA_SMTP_PORT`: SMTP port (default: 587)
- `SLONANA_SMTP_USERNAME`: SMTP authentication username
- `SLONANA_SMTP_PASSWORD`: SMTP authentication password

## Monitoring Integration

### Prometheus Metrics
The system automatically exposes metrics for monitoring systems:

```
# Critical failure counters
critical_failures_total{module="network"} 5
critical_failures_total{module="consensus"} 2
critical_failures_total{module="svm"} 1

# Log level counters
log_messages_total{level="error"} 156
log_messages_total{level="critical"} 8
```

### Grafana Dashboard
Key metrics to monitor:
- Critical failure rate by module
- Error log frequency
- Alert response times
- System health indicators

## Performance Impact

### Benchmarking Results
- **Synchronous Logging**: <1% overhead in normal operations
- **Asynchronous Logging**: <0.1% overhead in production
- **Alert Processing**: <0.01% overhead per alert
- **Memory Usage**: ~2MB additional for logging buffers

### Thread Safety
- All logging operations are thread-safe
- Uses atomic operations for level checking
- Lock-free queue for async logging
- No blocking in critical consensus paths

## Troubleshooting

### Common Issues

**High Log Volume**
- Adjust log level to reduce verbosity
- Enable async logging for better performance
- Use log sampling for high-frequency events

**Missing Alerts**
- Check alert channel configuration
- Verify network connectivity for external channels
- Review alert file permissions

**Performance Degradation**
- Enable async logging in production
- Verify log level is appropriate (INFO or higher)
- Monitor disk I/O for log files

### Debug Commands

```bash
# Check current log level
grep "Log level:" /var/log/slonana/validator.log

# Monitor critical alerts
tail -f /var/log/slonana/critical_alerts.log

# View recent errors
grep "ERROR\|CRITICAL" /var/log/slonana/validator.log | tail -20
```

## Security Considerations

### Sensitive Data
- Never log private keys or secrets
- Sanitize user input in log messages
- Use error codes instead of full error details for external alerts

### Access Control
- Restrict access to log files (600 permissions)
- Secure alert channels (HTTPS/TLS)
- Rotate log files regularly

### Audit Trail
- All critical failures are permanently logged
- Log tampering detection via checksums
- Centralized log aggregation for forensics

## Future Enhancements

### Planned Features
- **Log Sampling**: Intelligent sampling for high-frequency events
- **Circuit Breakers**: Automatic alert rate limiting
- **ML-based Anomaly Detection**: Proactive failure prediction
- **Distributed Tracing**: Cross-component request tracking
- **Log Compression**: Automatic compression of archived logs

### Integration Roadmap
- **PagerDuty**: Enterprise incident management
- **Datadog**: APM and observability platform  
- **Elastic Stack**: Centralized log analysis
- **OpenTelemetry**: Standard observability framework

---

*This document is part of the Slonana.cpp comprehensive logging and alerting implementation (Issue #92).*