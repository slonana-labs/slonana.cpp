#!/bin/bash
set -e

# Slonana Validator Debian package build script

PACKAGE_NAME="slonana-validator"
VERSION="1.0.0"
ARCHITECTURE="amd64"
BUILD_DIR="$(pwd)/build"
PACKAGE_DIR="$(pwd)/packaging/debian"
TEMP_DIR="/tmp/${PACKAGE_NAME}_${VERSION}_${ARCHITECTURE}"

echo "Building Debian package for ${PACKAGE_NAME} ${VERSION}"

# Clean up any previous build
rm -rf "${TEMP_DIR}"
mkdir -p "${TEMP_DIR}/DEBIAN"
mkdir -p "${TEMP_DIR}/usr/bin"
mkdir -p "${TEMP_DIR}/etc/slonana"
mkdir -p "${TEMP_DIR}/usr/lib/systemd/system"
mkdir -p "${TEMP_DIR}/var/lib/slonana"
mkdir -p "${TEMP_DIR}/var/log/slonana"
mkdir -p "${TEMP_DIR}/usr/share/doc/${PACKAGE_NAME}"

# Copy control file
cp "${PACKAGE_DIR}/control" "${TEMP_DIR}/DEBIAN/"

# Create package installation scripts
cat > "${TEMP_DIR}/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e

# Create slonana user if it doesn't exist
if ! id "slonana" &>/dev/null; then
    useradd --system --home /var/lib/slonana --shell /bin/bash --group slonana
fi

# Set proper permissions
chown -R slonana:slonana /var/lib/slonana
chown -R slonana:slonana /var/log/slonana
chmod 755 /usr/bin/slonana_validator
chmod 755 /usr/bin/slonana-genesis
chmod 644 /etc/slonana/validator.toml

# Enable systemd service
systemctl daemon-reload
systemctl enable slonana-validator.service

echo "Slonana validator installed successfully!"
echo "Configuration: /etc/slonana/validator.toml"
echo "Start service: sudo systemctl start slonana-validator"
echo "View logs: sudo journalctl -u slonana-validator -f"

exit 0
EOF

cat > "${TEMP_DIR}/DEBIAN/prerm" << 'EOF'
#!/bin/bash
set -e

# Stop and disable service
systemctl stop slonana-validator.service || true
systemctl disable slonana-validator.service || true

exit 0
EOF

cat > "${TEMP_DIR}/DEBIAN/postrm" << 'EOF'
#!/bin/bash
set -e

# Remove systemd service
systemctl daemon-reload

# Note: We don't remove the slonana user or data directory
# to preserve user data across package updates

exit 0
EOF

# Make scripts executable
chmod 755 "${TEMP_DIR}/DEBIAN/postinst"
chmod 755 "${TEMP_DIR}/DEBIAN/prerm"
chmod 755 "${TEMP_DIR}/DEBIAN/postrm"

# Copy binaries
if [ ! -f "${BUILD_DIR}/slonana_validator" ]; then
    echo "Error: slonana_validator binary not found. Run 'make' first."
    exit 1
fi

cp "${BUILD_DIR}/slonana_validator" "${TEMP_DIR}/usr/bin/"
cp "${BUILD_DIR}/slonana-genesis" "${TEMP_DIR}/usr/bin/"

# Copy configuration files
cat > "${TEMP_DIR}/etc/slonana/validator.toml" << 'EOF'
# Slonana Validator Configuration
# See docs/CONFIGURATION.md for full documentation

[network]
# Network configuration
gossip_port = 8001
rpc_port = 8899
validator_port = 8003

[validator]
# Validator identity and accounts
identity_file = "/var/lib/slonana/identity.json"
vote_account_file = "/var/lib/slonana/vote-account.json"
ledger_path = "/var/lib/slonana/ledger"

[consensus]
enable_voting = true
vote_threshold = 0.67

[rpc]
enable_rpc = true
rpc_bind_address = "0.0.0.0:8899"

[monitoring]
enable_metrics = true
metrics_port = 9090

[logging]
log_level = "info"
log_file = "/var/log/slonana/validator.log"
EOF

# Create systemd service file
cat > "${TEMP_DIR}/usr/lib/systemd/system/slonana-validator.service" << 'EOF'
[Unit]
Description=Slonana C++ Solana Validator
After=network.target
Wants=network-online.target

[Service]
Type=simple
User=slonana
Group=slonana
WorkingDirectory=/var/lib/slonana
ExecStart=/usr/bin/slonana_validator --config /etc/slonana/validator.toml
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=slonana-validator

# Resource limits
LimitNOFILE=65536
LimitNPROC=32768

# Security settings
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/slonana /var/log/slonana

[Install]
WantedBy=multi-user.target
EOF

# Copy documentation
cat > "${TEMP_DIR}/usr/share/doc/${PACKAGE_NAME}/README.Debian" << 'EOF'
Slonana Validator for Debian
============================

This package provides the Slonana C++ Solana validator implementation.

Quick Start:
1. Configure your validator: sudo nano /etc/slonana/validator.toml
2. Generate identity keys: sudo -u slonana slonana-genesis create-identity
3. Start the service: sudo systemctl start slonana-validator
4. Check status: sudo systemctl status slonana-validator

For full documentation, visit: https://docs.slonana.dev

Configuration:
- Main config: /etc/slonana/validator.toml
- Data directory: /var/lib/slonana
- Log files: /var/log/slonana

Service Management:
- Start: sudo systemctl start slonana-validator
- Stop: sudo systemctl stop slonana-validator
- Status: sudo systemctl status slonana-validator
- Logs: sudo journalctl -u slonana-validator -f

Support:
- GitHub: https://github.com/slonana-labs/slonana.cpp
- Issues: https://github.com/slonana-labs/slonana.cpp/issues
EOF

cp README.md "${TEMP_DIR}/usr/share/doc/${PACKAGE_NAME}/" 2>/dev/null || echo "README.md not found, skipping"
cp LICENSE "${TEMP_DIR}/usr/share/doc/${PACKAGE_NAME}/" 2>/dev/null || echo "LICENSE not found, skipping"

# Calculate installed size
INSTALLED_SIZE=$(du -sk "${TEMP_DIR}" | cut -f1)
echo "Installed-Size: ${INSTALLED_SIZE}" >> "${TEMP_DIR}/DEBIAN/control"

# Build the package
dpkg-deb --build "${TEMP_DIR}" "${PACKAGE_NAME}_${VERSION}_${ARCHITECTURE}.deb"

echo "Debian package built: ${PACKAGE_NAME}_${VERSION}_${ARCHITECTURE}.deb"
echo "Install with: sudo dpkg -i ${PACKAGE_NAME}_${VERSION}_${ARCHITECTURE}.deb"
echo "             sudo apt-get install -f  # Fix dependencies if needed"

# Clean up
rm -rf "${TEMP_DIR}"