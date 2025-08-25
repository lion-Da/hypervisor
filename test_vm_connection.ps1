# 导入配置
. "$PSScriptRoot\vm_config.ps1"

Write-Host "Testing connection to VM..."
if (Test-Connection -ComputerName $VM_CONFIG.IPAddress -Count 1 -Quiet) {
    Write-Host "VM is reachable at $($VM_CONFIG.IPAddress)"
} else {
    Write-Host "Error: Cannot reach VM at $($VM_CONFIG.IPAddress)"
    exit 1
}

# 测试网络共享访问
$testPath = "\\$($VM_CONFIG.ComputerName)\$($VM_CONFIG.RemotePath)"
Write-Host "Testing network share path: $testPath"
if (Test-Path $testPath) {
    Write-Host "Network share is accessible"
} else {
    Write-Host "Warning: Cannot access network share at $testPath"
    Write-Host "Please ensure file sharing is enabled on the VM"
    Write-Host "You may need to run the following commands on the VM:"
    Write-Host "1. Enable-PSRemoting -Force"
    Write-Host "2. Set-Item WSMan:\localhost\Client\TrustedHosts -Value '*' -Force"
    Write-Host "3. Enable-NetFirewallRule -Name 'WINRM-HTTP-In-TCP'"
    Write-Host "4. net share hypervisor_test=C:\hypervisor_test /GRANT:dalion,FULL"
}
