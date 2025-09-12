#include "cluster/multi_master_coordinator.h"
#include "cluster/multi_master_manager.h"
#include "network/distributed_load_balancer.h"
#include "network/topology_manager.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using namespace slonana::cluster;
using namespace slonana::network;
using namespace slonana::common;

int main() {
  std::cout << "=== Phase 3: Multi-Master Network Setup Demo ===" << std::endl;

  // Create a minimal validator config
  ValidatorConfig config;

  try {
    // Initialize multi-master components
    auto multi_master_manager =
        std::make_shared<MultiMasterManager>("node1", config);
    auto topology_manager =
        std::make_shared<NetworkTopologyManager>("node1", config);
    auto load_balancer =
        std::make_shared<DistributedLoadBalancer>("lb1", config);
    auto coordinator =
        std::make_shared<MultiMasterCoordinator>("coord1", config);

    // Wire up components
    coordinator->set_multi_master_manager(multi_master_manager);
    coordinator->set_topology_manager(topology_manager);
    coordinator->set_load_balancer(load_balancer);

    load_balancer->set_multi_master_manager(multi_master_manager);
    load_balancer->set_topology_manager(topology_manager);

    std::cout << "✅ Multi-master components initialized successfully"
              << std::endl;

    // Start all components
    std::cout << "\n🚀 Starting multi-master network setup..." << std::endl;

    if (!multi_master_manager->start()) {
      std::cerr << "❌ Failed to start multi-master manager" << std::endl;
      return 1;
    }
    std::cout << "✅ Multi-master manager started" << std::endl;

    if (!topology_manager->start()) {
      std::cerr << "❌ Failed to start topology manager" << std::endl;
      return 1;
    }
    std::cout << "✅ Network topology manager started" << std::endl;

    if (!load_balancer->start()) {
      std::cerr << "❌ Failed to start load balancer" << std::endl;
      return 1;
    }
    std::cout << "✅ Distributed load balancer started" << std::endl;

    if (!coordinator->start()) {
      std::cerr << "❌ Failed to start coordinator" << std::endl;
      return 1;
    }
    std::cout << "✅ Multi-master coordinator started" << std::endl;

    // Demo Phase 3 functionality
    std::cout << "\n🎯 Demonstrating Phase 3 Multi-Master Features:"
              << std::endl;

    // 1. Register network nodes
    std::cout << "\n1. Setting up network topology..." << std::endl;

    NetworkNode node1;
    node1.node_id = "node1";
    node1.address = "192.168.1.10";
    node1.port = 8899;
    node1.region = "us-east-1";
    node1.datacenter = "dc1";
    node1.capabilities = {"rpc", "consensus"};
    node1.bandwidth_mbps = 1000;
    node1.latency_ms = 10;
    node1.is_active = true;

    NetworkNode node2;
    node2.node_id = "node2";
    node2.address = "192.168.1.11";
    node2.port = 8899;
    node2.region = "us-east-1";
    node2.datacenter = "dc1";
    node2.capabilities = {"rpc", "ledger"};
    node2.bandwidth_mbps = 1000;
    node2.latency_ms = 12;
    node2.is_active = true;

    NetworkNode node3;
    node3.node_id = "node3";
    node3.address = "192.168.2.10";
    node3.port = 8899;
    node3.region = "us-west-1";
    node3.datacenter = "dc2";
    node3.capabilities = {"rpc", "gossip"};
    node3.bandwidth_mbps = 1000;
    node3.latency_ms = 25;
    node3.is_active = true;

    topology_manager->register_node(node1);
    topology_manager->register_node(node2);
    topology_manager->register_node(node3);

    // 2. Create network partitions
    std::cout << "2. Creating network partitions..." << std::endl;

    topology_manager->create_partition("partition-east", {"node1", "node2"});
    topology_manager->create_partition("partition-west", {"node3"});

    // 3. Setup cross-region links
    std::cout << "3. Establishing cross-region links..." << std::endl;

    CrossRegionLink link;
    link.source_region = "us-east-1";
    link.target_region = "us-west-1";
    link.bridge_nodes = {"node1", "node3"};
    link.bandwidth_mbps = 1000;
    link.latency_ms = 25;
    link.reliability_score = 0.99;
    link.is_active = true;
    topology_manager->establish_cross_region_link(link);

    // 4. Register master nodes
    std::cout << "4. Setting up multi-master architecture..." << std::endl;

    MasterNode master1;
    master1.node_id = "node1";
    master1.address = "192.168.1.10";
    master1.port = 8899;
    master1.role = MasterRole::RPC_MASTER;
    master1.shard_id = 0;
    master1.region = "us-east-1";
    master1.load_score = 0;
    master1.is_healthy = true;

    MasterNode master2;
    master2.node_id = "node2";
    master2.address = "192.168.1.11";
    master2.port = 8899;
    master2.role = MasterRole::LEDGER_MASTER;
    master2.shard_id = 0;
    master2.region = "us-east-1";
    master2.load_score = 0;
    master2.is_healthy = true;

    MasterNode master3;
    master3.node_id = "node3";
    master3.address = "192.168.2.10";
    master3.port = 8899;
    master3.role = MasterRole::GOSSIP_MASTER;
    master3.shard_id = 0;
    master3.region = "us-west-1";
    master3.load_score = 0;
    master3.is_healthy = true;

    multi_master_manager->register_master(master1);
    multi_master_manager->register_master(master2);
    multi_master_manager->register_master(master3);

    // 5. Setup load balancing
    std::cout << "5. Configuring distributed load balancing..." << std::endl;

    BackendServer server1;
    server1.server_id = "node1";
    server1.address = "192.168.1.10";
    server1.port = 8899;
    server1.region = "us-east-1";
    server1.weight = 100;
    server1.current_connections = 0;
    server1.max_connections = 1000;
    server1.average_response_time = std::chrono::milliseconds(10);
    server1.health_score = 1.0;
    server1.is_active = true;
    server1.is_draining = false;

    BackendServer server2;
    server2.server_id = "node2";
    server2.address = "192.168.1.11";
    server2.port = 8899;
    server2.region = "us-east-1";
    server2.weight = 100;
    server2.current_connections = 0;
    server2.max_connections = 1000;
    server2.average_response_time = std::chrono::milliseconds(12);
    server2.health_score = 1.0;
    server2.is_active = true;
    server2.is_draining = false;

    BackendServer server3;
    server3.server_id = "node3";
    server3.address = "192.168.2.10";
    server3.port = 8899;
    server3.region = "us-west-1";
    server3.weight = 50; // Lower weight for cross-region
    server3.current_connections = 0;
    server3.max_connections = 1000;
    server3.average_response_time = std::chrono::milliseconds(25);
    server3.health_score = 1.0;
    server3.is_active = true;
    server3.is_draining = false;

    load_balancer->register_backend_server(server1);
    load_balancer->register_backend_server(server2);
    load_balancer->register_backend_server(server3);

    LoadBalancingRule rpc_rule;
    rpc_rule.rule_name = "rpc_service";
    rpc_rule.service_pattern = "rpc.*";
    rpc_rule.strategy = LoadBalancingStrategy::GEOGRAPHIC;
    rpc_rule.backend_servers = {"node1", "node2", "node3"};
    rpc_rule.health_check_interval_ms = 5000;
    rpc_rule.max_retries = 3;
    rpc_rule.enable_session_affinity = true;

    load_balancer->add_load_balancing_rule(rpc_rule);

    // 6. Coordinate global consensus
    std::cout << "6. Initiating global consensus..." << std::endl;

    coordinator->initiate_global_consensus();
    coordinator->setup_regional_coordination("us-east-1");
    coordinator->setup_regional_coordination("us-west-1");

    // 7. Demonstrate request routing
    std::cout << "\n7. Testing request routing..." << std::endl;

    ConnectionRequest req1;
    req1.request_id = "req1";
    req1.client_ip = "10.0.1.100";
    req1.service_name = "rpc_service";
    req1.target_region = "us-east-1";
    req1.timestamp = std::chrono::system_clock::now();

    auto response1 = load_balancer->route_request(req1);
    if (response1.success) {
      std::cout << "   ✅ Request routed to: " << response1.selected_server
                << " (" << response1.server_address << ":"
                << response1.server_port << ")" << std::endl;
    }

    ConnectionRequest req2;
    req2.request_id = "req2";
    req2.client_ip = "10.0.2.100";
    req2.service_name = "rpc_service";
    req2.target_region = "us-west-1";
    req2.timestamp = std::chrono::system_clock::now();

    auto response2 = load_balancer->route_request(req2);
    if (response2.success) {
      std::cout << "   ✅ Request routed to: " << response2.selected_server
                << " (" << response2.server_address << ":"
                << response2.server_port << ")" << std::endl;
    }

    // 8. Demonstrate master coordination
    std::cout << "\n8. Testing master coordination..." << std::endl;

    // Promote additional master
    coordinator->coordinate_master_promotion("node1",
                                             MasterRole::GLOBAL_MASTER);

    // Test failover simulation
    std::cout << "   Simulating master failover..." << std::endl;
    coordinator->coordinate_failover("node2");

    // 9. Show statistics
    std::cout << "\n📊 Multi-Master Network Statistics:" << std::endl;

    auto master_stats = multi_master_manager->get_statistics();
    std::cout << "   • Total Masters: " << master_stats.total_masters
              << std::endl;
    std::cout << "   • Healthy Masters: " << master_stats.healthy_masters
              << std::endl;

    auto network_metrics = topology_manager->get_network_metrics();
    std::cout << "   • Network Nodes: " << network_metrics.total_nodes
              << std::endl;
    std::cout << "   • Active Nodes: " << network_metrics.active_nodes
              << std::endl;
    std::cout << "   • Network Partitions: " << network_metrics.partitions_count
              << std::endl;

    auto lb_stats = load_balancer->get_statistics();
    std::cout << "   • Load Balancer Requests: " << lb_stats.total_requests
              << std::endl;
    std::cout << "   • Successful Requests: " << lb_stats.successful_requests
              << std::endl;
    std::cout << "   • Active Backend Servers: " << lb_stats.active_backends
              << std::endl;

    auto coord_stats = coordinator->get_statistics();
    std::cout << "   • Coordination Events: "
              << coord_stats.total_coordination_events << std::endl;
    std::cout << "   • Successful Failovers: "
              << coord_stats.successful_failovers << std::endl;

    // 10. Run for a few seconds to show activity
    std::cout << "\n⏱️  Running multi-master network for 5 seconds..."
              << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Show final status
    auto coordination_status = coordinator->get_coordination_status();
    std::cout << "\n🏁 Final Coordination Status:" << std::endl;
    for (const auto &status : coordination_status) {
      std::cout << "   • " << status.first << ": " << status.second
                << std::endl;
    }

    // Stop all components
    std::cout << "\n🛑 Stopping multi-master network..." << std::endl;
    coordinator->stop();
    load_balancer->stop();
    topology_manager->stop();
    multi_master_manager->stop();

    std::cout << "\n🎉 Phase 3 Multi-Master Network Setup Demo Completed "
                 "Successfully!"
              << std::endl;
    std::cout << "\n📋 Phase 3 Features Demonstrated:" << std::endl;
    std::cout << "   ✅ Multi-Master Architecture with Role Specialization"
              << std::endl;
    std::cout << "   ✅ Network Topology Management with Regional Partitioning"
              << std::endl;
    std::cout << "   ✅ Distributed Load Balancing with Geographic Awareness"
              << std::endl;
    std::cout << "   ✅ Cross-Master Coordination and Global Consensus"
              << std::endl;
    std::cout << "   ✅ Automatic Failover and Master Election" << std::endl;
    std::cout << "   ✅ Cross-Region Network Links and Synchronization"
              << std::endl;
    std::cout << "   ✅ Service Discovery and Request Routing" << std::endl;
    std::cout << "   ✅ Performance Monitoring and Statistics" << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "❌ Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}