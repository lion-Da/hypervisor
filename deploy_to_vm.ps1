param(
    [string]$BuildConfig,
    [string]$ArtifactPath
)

# 导入配置
. "$PSScriptRoot\vm_config.ps1"

Write-Host "Deploying $BuildConfig build from $ArtifactPath to VM $($VM_CONFIG.ComputerName)"

# 测试连接
if (!(Test-Connection -ComputerName $VM_CONFIG.IPAddress -Count 1 -Quiet)) {
    Write-Host "Error: Cannot reach VM at $($VM_CONFIG.IPAddress)"
    exit 1
}

# 确保目标目录存在
$targetPath = "\\$($VM_CONFIG.ComputerName)\$($VM_CONFIG.RemotePath)"

Write-Host "Copying files to VM at $targetPath..."

# 定义要复制的文件列表
$filesToCopy = @(
    "hyperhook.sys",
    "hyperhook.pdb",
    "hyperhook.dll",
    "hyperhook.lib",
    "hyperhook.exp",
    "runner.exe",
    "runner.pdb"
)

# 复制每个文件
foreach($fileName in $filesToCopy) {
    $sourcePath = Join-Path $ArtifactPath $fileName
    if (Test-Path $sourcePath) {
        Write-Host "Copying $fileName..."
        Copy-Item -Path $sourcePath -Destination $targetPath -Force
    } else {
        Write-Host "Warning: $fileName not found in $ArtifactPath"
    }
}
Write-Host "Deployment completed successfully"
