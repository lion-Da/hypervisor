# VS Code OUTPUT窗口中文乱码修复脚本
# 使用方法：在项目根目录运行此脚本，然后启动VS Code

Write-Host "🔧 正在配置VS Code编码设置..." -ForegroundColor Green

# 设置当前PowerShell会话编码
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::InputEncoding = [System.Text.Encoding]::UTF8

# 设置代码页为UTF-8
chcp 65001 > $null

Write-Host "✅ PowerShell编码设置完成" -ForegroundColor Green

# 设置环境变量
$env:PYTHONIOENCODING = "utf-8"
$env:LC_ALL = "C.UTF-8" 
$env:LANG = "C.UTF-8"

Write-Host "✅ 环境变量设置完成" -ForegroundColor Green

# 检查VS Code设置文件
$vscodeSettingsPath = ".vscode/settings.json"
if (Test-Path $vscodeSettingsPath) {
    Write-Host "✅ VS Code设置文件已存在" -ForegroundColor Green
} else {
    Write-Host "⚠️ 警告：VS Code设置文件不存在" -ForegroundColor Yellow
}

# 启动VS Code
Write-Host "🚀 正在启动VS Code..." -ForegroundColor Cyan
Write-Host ""
Write-Host "注意事项：" -ForegroundColor Yellow
Write-Host "1. 如果OUTPUT窗口仍有乱码，请重启VS Code" -ForegroundColor White
Write-Host "2. 检查状态栏右下角是否显示 'UTF-8'" -ForegroundColor White
Write-Host "3. 使用 Ctrl+Shift+P -> '重新加载窗口' 来刷新设置" -ForegroundColor White
Write-Host ""

# 在正确编码环境下启动VS Code
& code . 

Write-Host "✅ VS Code启动完成！" -ForegroundColor Green

