#!/bin/bash
cd "$(dirname "$0")"
docker-compose down
echo "Monitoring stack stopped!"
