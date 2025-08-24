$ErrorActionPreference = 'Stop'

$packageName = 'slonana-validator'
$toolsDir = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$url64 = 'https://github.com/slonana-labs/slonana.cpp/releases/download/v1.0.0/slonana-validator-windows-x64.zip'

$packageArgs = @{
  packageName   = $packageName
  unzipLocation = $toolsDir
  url64bit      = $url64
  checksum64    = 'YOUR_CHECKSUM_HERE'
  checksumType64= 'sha256'
}

Install-ChocolateyZipPackage @packageArgs

# Create Windows service
$serviceName = 'SlonanaValidator'
$serviceDisplayName = 'Slonana Validator'
$serviceDescription = 'High-performance C++ Solana validator'
$exePath = Join-Path $toolsDir 'slonana_validator.exe'
$configPath = Join-Path $env:ProgramData 'Slonana\validator.toml'

# Create config directory
$configDir = Split-Path $configPath
if (!(Test-Path $configDir)) {
    New-Item -ItemType Directory -Path $configDir -Force
}

# Create default configuration
@"
# Slonana Validator Configuration for Windows
[network]
gossip_port = 8001
rpc_port = 8899
validator_port = 8003

[validator]
identity_file = "C:\\ProgramData\\Slonana\\identity.json"
vote_account_file = "C:\\ProgramData\\Slonana\\vote-account.json"
ledger_path = "C:\\ProgramData\\Slonana\\ledger"

[consensus]
enable_voting = true
vote_threshold = 0.67

[rpc]
enable_rpc = true
rpc_bind_address = "127.0.0.1:8899"

[monitoring]
enable_metrics = true
metrics_port = 9090

[logging]
log_level = "info"
log_file = "C:\\ProgramData\\Slonana\\validator.log"
"@ | Out-File -FilePath $configPath -Encoding UTF8

Write-Host "Slonana Validator installed successfully!"
Write-Host "Configuration: $configPath"
Write-Host "To start: Start-Service $serviceName"
