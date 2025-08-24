Name:           slonana-validator
Version:        1.0.0
Release:        1%{?dist}
Summary:        High-performance C++ Solana validator implementation

License:        MIT
URL:            https://github.com/slonana-labs/slonana.cpp
Source0:        slonana-validator-%{version}.tar.gz

BuildRequires:  gcc-c++ >= 11
BuildRequires:  cmake >= 3.16
BuildRequires:  openssl-devel >= 3.0
BuildRequires:  systemd-rpm-macros

Requires:       openssl >= 3.0
Requires:       systemd
Requires(pre):  shadow-utils
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd

%description
Slonana.cpp is a high-performance C++ implementation of a Solana validator
that provides full compatibility with the Solana network while delivering
superior performance and reliability.

Features:
* Full Solana protocol compatibility
* High-performance SVM execution engine
* Comprehensive monitoring and metrics
* Hardware wallet support
* Production-ready deployment tooling

%prep
%setup -q

%build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INSTALL_PREFIX=%{_prefix} \
         -DCMAKE_CXX_STANDARD=20
make %{?_smp_mflags}

%install
cd build
make install DESTDIR=%{buildroot}

# Create directories
install -d %{buildroot}%{_sysconfdir}/slonana
install -d %{buildroot}%{_localstatedir}/lib/slonana
install -d %{buildroot}%{_localstatedir}/log/slonana
install -d %{buildroot}%{_unitdir}

# Install configuration
cat > %{buildroot}%{_sysconfdir}/slonana/validator.toml << 'EOF'
# Slonana Validator Configuration
# See docs/CONFIGURATION.md for full documentation

[network]
gossip_port = 8001
rpc_port = 8899
validator_port = 8003

[validator]
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

# Install systemd service
cat > %{buildroot}%{_unitdir}/slonana-validator.service << 'EOF'
[Unit]
Description=Slonana C++ Solana Validator
After=network.target
Wants=network-online.target

[Service]
Type=simple
User=slonana
Group=slonana
WorkingDirectory=/var/lib/slonana
ExecStart=%{_bindir}/slonana_validator --config %{_sysconfdir}/slonana/validator.toml
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

%pre
getent group slonana >/dev/null || groupadd -r slonana
getent passwd slonana >/dev/null || \
    useradd -r -g slonana -d %{_localstatedir}/lib/slonana -s /bin/bash \
    -c "Slonana validator" slonana
exit 0

%post
%systemd_post slonana-validator.service
# Set proper permissions
chown -R slonana:slonana %{_localstatedir}/lib/slonana
chown -R slonana:slonana %{_localstatedir}/log/slonana

%preun
%systemd_preun slonana-validator.service

%postun
%systemd_postun_with_restart slonana-validator.service

%files
%license LICENSE
%doc README.md
%{_bindir}/slonana_validator
%{_bindir}/slonana-genesis
%config(noreplace) %{_sysconfdir}/slonana/validator.toml
%{_unitdir}/slonana-validator.service
%attr(755,slonana,slonana) %dir %{_localstatedir}/lib/slonana
%attr(755,slonana,slonana) %dir %{_localstatedir}/log/slonana

%changelog
* Thu Jan 01 2025 Slonana Labs <contact@slonana.dev> - 1.0.0-1
- Initial RPM package release
- Full Solana validator implementation
- Hardware wallet support
- Production monitoring and metrics