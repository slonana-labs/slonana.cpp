#!/bin/bash
cd "$(dirname "$0")"
docker-compose up -d
echo "Monitoring stack started!"
echo "Grafana: http://localhost:3000 (admin/slonana123)"
echo "Prometheus: http://localhost:9090"
echo "Alertmanager: http://localhost:9093"
