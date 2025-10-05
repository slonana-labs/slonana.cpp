# Slonana.cpp Examples

This directory contains example code demonstrating best practices for using Slonana.cpp components.

## Lock-Free Queue Examples

### queue_backpressure_example.cpp

Demonstrates proper handling of push failures in the lock-free request queue, including:

- Memory management (mandatory cleanup on push failure)
- Backpressure policies (drop, retry, rate limit, error response)
- Queue metrics monitoring for production deployment
- Capacity planning considerations

**Build and run:**
```bash
cd build
g++ -std=c++17 -I../include -I../src ../examples/queue_backpressure_example.cpp \
    -L. -lslonana_core -lpthread -lssl -lcrypto -lsodium \
    -o queue_backpressure_example
./queue_backpressure_example
```

**Key takeaways:**
1. Always delete allocated pointers if push fails to prevent memory leaks
2. Track push failures for monitoring and alerting
3. Implement appropriate backpressure policy for your use case
4. Monitor queue metrics regularly in production
5. Use capacity planning to avoid sustained backpressure

## Best Practices

### Lock-Free Queue Usage

When working with the lock-free request queue in `DistributedLoadBalancer`:

1. **Memory Management**
   - Allocate `ConnectionRequest*` on heap before pushing
   - If `push()` fails, immediately `delete` the pointer
   - Never increment allocation counter on push failure

2. **Backpressure Handling**
   - Choose appropriate policy: drop, retry, rate limit, or return error
   - Log all drops for monitoring
   - Track metrics to detect sustained backpressure

3. **Configuration**
   - Set queue capacity based on expected load
   - Default capacity is 1024 items
   - Use `get_queue_metrics()` to monitor utilization
   - Alert when utilization > 80% or push failures occur

4. **Production Monitoring**
   - Check `allocated_count`: current items in queue
   - Check `push_failure_count`: backpressure indicator
   - Check `utilization_percent`: capacity planning metric
   - Set up alerts for high utilization and failures

### Example Code Pattern

```cpp
// Configure capacity based on workload
DistributedLoadBalancer balancer("my_balancer", config, 2048);

// Monitor in production
auto metrics = balancer.get_queue_metrics();
if (metrics.utilization_percent > 80.0) {
    // Alert or scale up capacity
}
```

## See Also

- [Concurrency Safety Guidelines](../docs/concurrency.md)
- [Network Layer Documentation](../docs/)
