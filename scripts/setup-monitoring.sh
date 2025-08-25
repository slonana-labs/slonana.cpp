#!/bin/bash

# Slonana Validator Production Monitoring Infrastructure Setup Script
# This script sets up comprehensive monitoring with Prometheus, Grafana, and Alertmanager

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
MONITORING_DIR="$PROJECT_ROOT/monitoring"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

show_help() {
    echo "Slonana Validator Production Monitoring Setup"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help           Show this help message"
    echo "  --setup-prometheus   Set up Prometheus configuration"
    echo "  --setup-grafana      Set up Grafana dashboards"
    echo "  --setup-alerting     Set up Alertmanager configuration"
    echo "  --setup-all          Set up complete monitoring stack"
    echo "  --docker             Create Docker Compose setup"
    echo "  --systemd            Create systemd service files"
    echo ""
    echo "Examples:"
    echo "  $0 --setup-all --docker"
    echo "  $0 --setup-prometheus --setup-grafana"
}

# Parse command line arguments
SETUP_PROMETHEUS=false
SETUP_GRAFANA=false
SETUP_ALERTING=false
SETUP_ALL=false
CREATE_DOCKER=false
CREATE_SYSTEMD=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        --setup-prometheus)
            SETUP_PROMETHEUS=true
            shift
            ;;
        --setup-grafana)
            SETUP_GRAFANA=true
            shift
            ;;
        --setup-alerting)
            SETUP_ALERTING=true
            shift
            ;;
        --setup-all)
            SETUP_ALL=true
            shift
            ;;
        --docker)
            CREATE_DOCKER=true
            shift
            ;;
        --systemd)
            CREATE_SYSTEMD=true
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Set all flags if --setup-all is specified
if [[ "$SETUP_ALL" == true ]]; then
    SETUP_PROMETHEUS=true
    SETUP_GRAFANA=true
    SETUP_ALERTING=true
fi

# Create monitoring directories
create_directories() {
    log_info "Creating monitoring directories..."
    
    mkdir -p "$MONITORING_DIR"/{prometheus,grafana,alertmanager,docker}
    mkdir -p "$MONITORING_DIR/grafana/dashboards"
    mkdir -p "$MONITORING_DIR/grafana/provisioning"/{dashboards,datasources}
    mkdir -p "$MONITORING_DIR/prometheus/rules"
    
    log_success "Monitoring directories created"
}

# Setup Prometheus configuration
setup_prometheus() {
    if [[ "$SETUP_PROMETHEUS" == true ]]; then
        log_info "Setting up Prometheus configuration..."
        
        cat > "$MONITORING_DIR/prometheus/prometheus.yml" << 'EOF'
# Prometheus configuration for Slonana Validator monitoring

global:
  scrape_interval: 15s
  evaluation_interval: 15s
  external_labels:
    cluster: 'slonana-validators'
    environment: 'production'

# Alertmanager configuration
alerting:
  alertmanagers:
    - static_configs:
        - targets:
          - alertmanager:9093

# Load rules once and periodically evaluate them
rule_files:
  - "rules/*.yml"

# Scrape configurations
scrape_configs:
  # Slonana Validator metrics
  - job_name: 'slonana-validator'
    static_configs:
      - targets: ['localhost:9090']
    metrics_path: '/metrics'
    scrape_interval: 5s
    scrape_timeout: 5s

  # System metrics
  - job_name: 'node-exporter'
    static_configs:
      - targets: ['localhost:9100']

  # Prometheus itself
  - job_name: 'prometheus'
    static_configs:
      - targets: ['localhost:9090']

  # Additional validator instances (add as needed)
  - job_name: 'slonana-cluster'
    static_configs:
      - targets: 
          # Add other validator instances here
          # - 'validator1.example.com:9090'
          # - 'validator2.example.com:9090'
    metrics_path: '/metrics'
    scrape_interval: 10s
EOF

        # Create alerting rules
        cat > "$MONITORING_DIR/prometheus/rules/slonana-validator.yml" << 'EOF'
groups:
  - name: slonana-validator-alerts
    rules:
      # Validator is down
      - alert: ValidatorDown
        expr: up{job="slonana-validator"} == 0
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "Slonana validator is down"
          description: "Validator {{ $labels.instance }} has been down for more than 1 minute"

      # High memory usage
      - alert: HighMemoryUsage
        expr: (slonana_memory_usage_bytes / slonana_memory_total_bytes) * 100 > 85
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "High memory usage on validator"
          description: "Memory usage is above 85% for more than 5 minutes: {{ $value }}%"

      # High CPU usage
      - alert: HighCPUUsage
        expr: slonana_cpu_usage_percent > 90
        for: 10m
        labels:
          severity: warning
        annotations:
          summary: "High CPU usage on validator"
          description: "CPU usage is above 90% for more than 10 minutes: {{ $value }}%"

      # Validator behind in slots
      - alert: ValidatorBehind
        expr: slonana_validator_slot_distance > 50
        for: 2m
        labels:
          severity: critical
        annotations:
          summary: "Validator is behind in slots"
          description: "Validator is {{ $value }} slots behind the cluster"

      # Low vote success rate
      - alert: LowVoteSuccessRate
        expr: (slonana_validator_votes_successful / slonana_validator_votes_total) * 100 < 95
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "Low vote success rate"
          description: "Vote success rate is below 95%: {{ $value }}%"

      # Transaction processing errors
      - alert: HighTransactionErrors
        expr: rate(slonana_transaction_errors_total[5m]) > 10
        for: 2m
        labels:
          severity: warning
        annotations:
          summary: "High transaction error rate"
          description: "Transaction error rate is {{ $value }} errors/second"

      # Network connectivity issues
      - alert: LowPeerCount
        expr: slonana_network_peers_connected < 10
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "Low peer count"
          description: "Only {{ $value }} peers connected (minimum recommended: 10)"

      # Disk space running low
      - alert: LowDiskSpace
        expr: (slonana_disk_free_bytes / slonana_disk_total_bytes) * 100 < 20
        for: 1m
        labels:
          severity: warning
        annotations:
          summary: "Low disk space"
          description: "Disk space is below 20%: {{ $value }}% remaining"

      # Disk space critically low
      - alert: CriticalDiskSpace
        expr: (slonana_disk_free_bytes / slonana_disk_total_bytes) * 100 < 10
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "Critical disk space"
          description: "Disk space is critically low: {{ $value }}% remaining"
EOF

        log_success "Prometheus configuration created"
    fi
}

# Setup Grafana configuration
setup_grafana() {
    if [[ "$SETUP_GRAFANA" == true ]]; then
        log_info "Setting up Grafana configuration..."
        
        # Grafana configuration
        cat > "$MONITORING_DIR/grafana/grafana.ini" << 'EOF'
[server]
http_port = 3000
domain = localhost
root_url = http://localhost:3000/

[security]
admin_user = admin
admin_password = slonana123

[auth]
disable_login_form = false

[auth.anonymous]
enabled = false

[dashboards]
default_home_dashboard_path = /var/lib/grafana/dashboards/slonana-overview.json

[paths]
data = /var/lib/grafana
logs = /var/log/grafana
plugins = /var/lib/grafana/plugins
provisioning = /etc/grafana/provisioning
EOF

        # Datasource provisioning
        cat > "$MONITORING_DIR/grafana/provisioning/datasources/prometheus.yml" << 'EOF'
apiVersion: 1

datasources:
  - name: Prometheus
    type: prometheus
    access: proxy
    url: http://prometheus:9090
    isDefault: true
    editable: true
EOF

        # Dashboard provisioning
        cat > "$MONITORING_DIR/grafana/provisioning/dashboards/dashboards.yml" << 'EOF'
apiVersion: 1

providers:
  - name: 'slonana-dashboards'
    orgId: 1
    folder: 'Slonana'
    type: file
    disableDeletion: false
    updateIntervalSeconds: 10
    allowUiUpdates: true
    options:
      path: /var/lib/grafana/dashboards
EOF

        # Main validator dashboard
        cat > "$MONITORING_DIR/grafana/dashboards/slonana-overview.json" << 'EOF'
{
  "dashboard": {
    "id": null,
    "title": "Slonana Validator Overview",
    "tags": ["slonana", "validator"],
    "timezone": "browser",
    "refresh": "5s",
    "time": {
      "from": "now-1h",
      "to": "now"
    },
    "panels": [
      {
        "id": 1,
        "title": "Validator Status",
        "type": "stat",
        "targets": [
          {
            "expr": "up{job=\"slonana-validator\"}",
            "legendFormat": "Validator Up"
          }
        ],
        "fieldConfig": {
          "defaults": {
            "color": {
              "mode": "thresholds"
            },
            "thresholds": {
              "steps": [
                {"color": "red", "value": 0},
                {"color": "green", "value": 1}
              ]
            }
          }
        },
        "gridPos": {"h": 4, "w": 6, "x": 0, "y": 0}
      },
      {
        "id": 2,
        "title": "Current Slot",
        "type": "stat",
        "targets": [
          {
            "expr": "slonana_validator_current_slot",
            "legendFormat": "Current Slot"
          }
        ],
        "gridPos": {"h": 4, "w": 6, "x": 6, "y": 0}
      },
      {
        "id": 3,
        "title": "Slot Distance",
        "type": "stat",
        "targets": [
          {
            "expr": "slonana_validator_slot_distance",
            "legendFormat": "Slots Behind"
          }
        ],
        "fieldConfig": {
          "defaults": {
            "thresholds": {
              "steps": [
                {"color": "green", "value": 0},
                {"color": "yellow", "value": 10},
                {"color": "red", "value": 50}
              ]
            }
          }
        },
        "gridPos": {"h": 4, "w": 6, "x": 12, "y": 0}
      },
      {
        "id": 4,
        "title": "Vote Success Rate",
        "type": "stat",
        "targets": [
          {
            "expr": "(slonana_validator_votes_successful / slonana_validator_votes_total) * 100",
            "legendFormat": "Vote Success %"
          }
        ],
        "fieldConfig": {
          "defaults": {
            "unit": "percent",
            "thresholds": {
              "steps": [
                {"color": "red", "value": 0},
                {"color": "yellow", "value": 90},
                {"color": "green", "value": 95}
              ]
            }
          }
        },
        "gridPos": {"h": 4, "w": 6, "x": 18, "y": 0}
      },
      {
        "id": 5,
        "title": "Transaction Processing Rate",
        "type": "graph",
        "targets": [
          {
            "expr": "rate(slonana_transactions_processed_total[1m])",
            "legendFormat": "TPS"
          }
        ],
        "yAxes": [
          {
            "label": "Transactions/sec"
          }
        ],
        "gridPos": {"h": 8, "w": 12, "x": 0, "y": 4}
      },
      {
        "id": 6,
        "title": "System Resources",
        "type": "graph",
        "targets": [
          {
            "expr": "slonana_cpu_usage_percent",
            "legendFormat": "CPU %"
          },
          {
            "expr": "(slonana_memory_usage_bytes / slonana_memory_total_bytes) * 100",
            "legendFormat": "Memory %"
          }
        ],
        "yAxes": [
          {
            "label": "Percentage",
            "max": 100
          }
        ],
        "gridPos": {"h": 8, "w": 12, "x": 12, "y": 4}
      }
    ]
  }
}
EOF

        log_success "Grafana configuration created"
    fi
}

# Setup Alertmanager configuration
setup_alerting() {
    if [[ "$SETUP_ALERTING" == true ]]; then
        log_info "Setting up Alertmanager configuration..."
        
        cat > "$MONITORING_DIR/alertmanager/alertmanager.yml" << 'EOF'
global:
  smtp_smarthost: 'localhost:587'
  smtp_from: 'alerts@slonana.dev'

# The directory from which notification templates are read.
templates:
  - '/etc/alertmanager/templates/*.tmpl'

# The root route on which each incoming alert enters.
route:
  group_by: ['alertname']
  group_wait: 10s
  group_interval: 10s
  repeat_interval: 1h
  receiver: 'web.hook'
  routes:
    - match:
        severity: critical
      receiver: 'critical-alerts'
    - match:
        severity: warning
      receiver: 'warning-alerts'

receivers:
  - name: 'web.hook'
    webhook_configs:
      - url: 'http://localhost:5001/'

  - name: 'critical-alerts'
    email_configs:
      - to: 'admin@slonana.dev'
        subject: 'CRITICAL: Slonana Validator Alert'
        body: |
          {{ range .Alerts }}
          Alert: {{ .Annotations.summary }}
          Description: {{ .Annotations.description }}
          Instance: {{ .Labels.instance }}
          {{ end }}
    slack_configs:
      - api_url: 'YOUR_SLACK_WEBHOOK_URL'
        channel: '#alerts'
        title: 'Critical Validator Alert'
        text: '{{ range .Alerts }}{{ .Annotations.summary }}{{ end }}'

  - name: 'warning-alerts'
    email_configs:
      - to: 'monitoring@slonana.dev'
        subject: 'WARNING: Slonana Validator Alert'
        body: |
          {{ range .Alerts }}
          Alert: {{ .Annotations.summary }}
          Description: {{ .Annotations.description }}
          Instance: {{ .Labels.instance }}
          {{ end }}

inhibit_rules:
  - source_match:
      severity: 'critical'
    target_match:
      severity: 'warning'
    equal: ['alertname', 'dev', 'instance']
EOF

        log_success "Alertmanager configuration created"
    fi
}

# Create Docker Compose setup
create_docker_compose() {
    if [[ "$CREATE_DOCKER" == true ]]; then
        log_info "Creating Docker Compose monitoring stack..."
        
        cat > "$MONITORING_DIR/docker/docker-compose.yml" << 'EOF'
version: '3.8'

services:
  prometheus:
    image: prom/prometheus:latest
    container_name: prometheus
    ports:
      - "9090:9090"
    volumes:
      - ../prometheus/prometheus.yml:/etc/prometheus/prometheus.yml
      - ../prometheus/rules:/etc/prometheus/rules
      - prometheus_data:/prometheus
    command:
      - '--config.file=/etc/prometheus/prometheus.yml'
      - '--storage.tsdb.path=/prometheus'
      - '--web.console.libraries=/etc/prometheus/console_libraries'
      - '--web.console.templates=/etc/prometheus/consoles'
      - '--storage.tsdb.retention.time=200h'
      - '--web.enable-lifecycle'
    restart: unless-stopped
    networks:
      - monitoring

  grafana:
    image: grafana/grafana:latest
    container_name: grafana
    ports:
      - "3000:3000"
    volumes:
      - ../grafana/grafana.ini:/etc/grafana/grafana.ini
      - ../grafana/provisioning:/etc/grafana/provisioning
      - ../grafana/dashboards:/var/lib/grafana/dashboards
      - grafana_data:/var/lib/grafana
    environment:
      - GF_SECURITY_ADMIN_USER=admin
      - GF_SECURITY_ADMIN_PASSWORD=slonana123
      - GF_USERS_ALLOW_SIGN_UP=false
    restart: unless-stopped
    networks:
      - monitoring

  alertmanager:
    image: prom/alertmanager:latest
    container_name: alertmanager
    ports:
      - "9093:9093"
    volumes:
      - ../alertmanager/alertmanager.yml:/etc/alertmanager/alertmanager.yml
      - alertmanager_data:/alertmanager
    command:
      - '--config.file=/etc/alertmanager/alertmanager.yml'
      - '--storage.path=/alertmanager'
      - '--web.external-url=http://localhost:9093'
    restart: unless-stopped
    networks:
      - monitoring

  node-exporter:
    image: prom/node-exporter:latest
    container_name: node-exporter
    ports:
      - "9100:9100"
    volumes:
      - /proc:/host/proc:ro
      - /sys:/host/sys:ro
      - /:/rootfs:ro
    command:
      - '--path.procfs=/host/proc'
      - '--path.rootfs=/rootfs'
      - '--path.sysfs=/host/sys'
      - '--collector.filesystem.mount-points-exclude=^/(sys|proc|dev|host|etc)($$|/)'
    restart: unless-stopped
    networks:
      - monitoring

volumes:
  prometheus_data:
  grafana_data:
  alertmanager_data:

networks:
  monitoring:
    driver: bridge
EOF

        # Create convenience scripts
        cat > "$MONITORING_DIR/docker/start-monitoring.sh" << 'EOF'
#!/bin/bash
cd "$(dirname "$0")"
docker-compose up -d
echo "Monitoring stack started!"
echo "Grafana: http://localhost:3000 (admin/slonana123)"
echo "Prometheus: http://localhost:9090"
echo "Alertmanager: http://localhost:9093"
EOF

        cat > "$MONITORING_DIR/docker/stop-monitoring.sh" << 'EOF'
#!/bin/bash
cd "$(dirname "$0")"
docker-compose down
echo "Monitoring stack stopped!"
EOF

        chmod +x "$MONITORING_DIR/docker"/*.sh
        
        log_success "Docker Compose monitoring stack created"
    fi
}

# Create systemd service files
create_systemd_services() {
    if [[ "$CREATE_SYSTEMD" == true ]]; then
        log_info "Creating systemd service files..."
        
        mkdir -p "$MONITORING_DIR/systemd"
        
        # Prometheus service
        cat > "$MONITORING_DIR/systemd/prometheus.service" << 'EOF'
[Unit]
Description=Prometheus
Wants=network-online.target
After=network-online.target

[Service]
User=prometheus
Group=prometheus
Type=simple
ExecStart=/usr/local/bin/prometheus \
    --config.file /etc/prometheus/prometheus.yml \
    --storage.tsdb.path /var/lib/prometheus/ \
    --web.console.libraries=/etc/prometheus/console_libraries \
    --web.console.templates=/etc/prometheus/consoles \
    --web.listen-address=0.0.0.0:9090 \
    --web.external-url=

[Install]
WantedBy=multi-user.target
EOF

        # Grafana service (usually comes with package)
        cat > "$MONITORING_DIR/systemd/grafana-server.service" << 'EOF'
[Unit]
Description=Grafana instance
Documentation=http://docs.grafana.org
Wants=network-online.target
After=network-online.target
After=postgresql.service mariadb.service mysql.service

[Service]
EnvironmentFile=/etc/default/grafana-server
User=grafana
Group=grafana
Type=simple
Restart=on-failure
WorkingDirectory=/usr/share/grafana
RuntimeDirectory=grafana
RuntimeDirectoryMode=0750
ExecStart=/usr/sbin/grafana-server \
                            --config=${CONF_FILE} \
                            --pidfile=${PID_FILE_DIR}/grafana-server.pid \
                            --packaging=deb \
                            cfg:default.paths.logs=${LOG_DIR} \
                            cfg:default.paths.data=${DATA_DIR} \
                            cfg:default.paths.plugins=${PLUGINS_DIR} \
                            cfg:default.paths.provisioning=${PROVISIONING_CFG_DIR}

[Install]
WantedBy=multi-user.target
EOF

        log_success "Systemd service files created"
    fi
}

# Show summary
show_summary() {
    log_info "Monitoring Infrastructure Setup Summary:"
    echo "=========================================="
    
    if [[ "$SETUP_PROMETHEUS" == true ]]; then
        echo "✅ Prometheus configuration: $MONITORING_DIR/prometheus/"
        echo "   - Configuration: prometheus.yml"
        echo "   - Alerting rules: rules/slonana-validator.yml"
    fi
    
    if [[ "$SETUP_GRAFANA" == true ]]; then
        echo "✅ Grafana configuration: $MONITORING_DIR/grafana/"
        echo "   - Dashboards: dashboards/"
        echo "   - Provisioning: provisioning/"
    fi
    
    if [[ "$SETUP_ALERTING" == true ]]; then
        echo "✅ Alertmanager configuration: $MONITORING_DIR/alertmanager/"
        echo "   - Configuration: alertmanager.yml"
    fi
    
    if [[ "$CREATE_DOCKER" == true ]]; then
        echo "✅ Docker Compose stack: $MONITORING_DIR/docker/"
        echo "   - Start: ./docker/start-monitoring.sh"
        echo "   - Stop: ./docker/stop-monitoring.sh"
    fi
    
    if [[ "$CREATE_SYSTEMD" == true ]]; then
        echo "✅ Systemd services: $MONITORING_DIR/systemd/"
    fi
    
    echo ""
    echo "Next Steps:"
    echo "1. Review and customize configurations"
    echo "2. Update email/Slack webhook URLs in alertmanager.yml"
    echo "3. Start monitoring stack with Docker or systemd"
    echo "4. Configure validator to export metrics on port 9090"
    echo "=========================================="
}

# Main execution
main() {
    log_info "Setting up Slonana Validator production monitoring infrastructure..."
    
    create_directories
    setup_prometheus
    setup_grafana
    setup_alerting
    create_docker_compose
    create_systemd_services
    
    log_success "Production monitoring infrastructure setup completed!"
    show_summary
}

# Execute main function
main "$@"