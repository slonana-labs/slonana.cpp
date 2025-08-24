$ErrorActionPreference = 'Stop'

$serviceName = 'SlonanaValidator'

# Stop and remove service if it exists
if (Get-Service $serviceName -ErrorAction SilentlyContinue) {
    Stop-Service $serviceName -Force
    Remove-Service $serviceName
}

Write-Host "Slonana Validator uninstalled successfully!"
