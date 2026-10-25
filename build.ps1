#Requires -Version 5.1
<#
.SYNOPSIS
    构建（并可打包）LemonLime。Windows + MinGW + Qt6。

.DESCRIPTION
    默认构建 Release 到 .\build，产物是 build\lemon.exe。
    加 -Package 会另建一个发布目录：lemon.exe + windeployqt 拉来的 Qt 运行库
    + statement-templates 模板目录，最后压成 zip。

    注意：
      * 链接前会自动关掉正在运行的 lemon.exe（否则 ld 报 Permission denied）。
      * 模板（assets\statement-templates）是运行时读取的，发布会复制成
        <发布目录>\statement-templates；平时改模板不需要重新编译。
      * LTO 链接很慢（几分钟），用 -NoLto 可以快很多。

.EXAMPLE
    .\build.ps1
    在 .\build 里构建 Release。

.EXAMPLE
    .\build.ps1 -Package
    构建并打包，zip 放在 .\dist\lemon-<时间戳>.zip。

.EXAMPLE
    .\build.ps1 -BuildType RelWithDebInfo -NoLto -Clean
    关掉 LTO、带调试信息、先清空目录重建（排查崩溃时用）。

.EXAMPLE
    .\build.ps1 -Package -Version v1.0.0 -QtDir D:\Qt\6.9.0\mingw_64
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$BuildType = 'Release',

    [switch]$NoLto,

    [switch]$Clean,

    [switch]$Package,

    [string]$Version,

    [string]$QtDir = 'D:\Qt\6.8.3\mingw_64',

    [string]$ToolchainBin = 'D:\Qt\Tools\mingw1310_64\bin',

    [string]$CmakeBin = 'C:\mingw64\bin\cmake.exe',

    [string]$BuildDir,

    [string]$OutDir
)

$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot

if (-not $BuildDir) {
    if ($BuildType -eq 'Release') {
        $BuildDir = Join-Path $root 'build'
    } else {
        $BuildDir = Join-Path $root ("build-" + $BuildType.ToLower())
    }
}

if ($NoLto) { $lto = 'OFF' } else { $lto = 'ON' }

function Write-Step($text) { Write-Host "`n=== $text ===" -ForegroundColor Cyan }

Write-Step '检查工具链'

if (-not (Test-Path $CmakeBin)) { throw "找不到 cmake：$CmakeBin（用 -CmakeBin 指定）" }
if (-not (Test-Path $QtDir)) { throw "找不到 Qt：$QtDir（用 -QtDir 指定）" }

$env:Path = $ToolchainBin + ';' + (Split-Path $CmakeBin -Parent) + ';' + $env:Path

if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue)) { throw 'PATH 里找不到 ninja.exe' }
if (-not (Get-Command g++.exe -ErrorAction SilentlyContinue)) { throw 'PATH 里找不到 g++.exe' }

Write-Host "Qt        : $QtDir"
Write-Host "构建目录  : $BuildDir"
Write-Host "构建类型  : $BuildType (LTO=$lto)"

Write-Step '关闭正在运行的 lemon.exe'

Get-Process lemon -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($BuildDir, [System.StringComparison]::OrdinalIgnoreCase) } |
    ForEach-Object {
        Write-Host "  stop $($_.Path)"
        Stop-Process -Id $_.Id -Force
    }

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "清理 $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

Write-Step 'CMake 配置'
& $CmakeBin -S $root -B $BuildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    "-DENABLE_LTO=$lto" `
    "-DCMAKE_PREFIX_PATH=$QtDir"
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败（exit $LASTEXITCODE）" }

Write-Step '构建 lemon（LTO 时链接会比较久）'
& $CmakeBin --build $BuildDir --target lemon
if ($LASTEXITCODE -ne 0) { throw "构建失败（exit $LASTEXITCODE）" }

$exe = Join-Path $BuildDir 'lemon.exe'
if (-not (Test-Path $exe)) { throw "构建结束但没找到 $exe" }

Write-Host ("构建完成：" + $exe + "  (" + [math]::Round((Get-Item $exe).Length / 1MB, 1) + " MB)") -ForegroundColor Green

if (-not $Package) {
    Write-Host "`n提示：加 -Package 可以顺便生成发布 zip。" -ForegroundColor DarkGray
    return
}

Write-Step '打包'

if (-not $Version) { $Version = Get-Date -Format 'yyyyMMdd-HHmmss' }
if (-not $OutDir) { $OutDir = Join-Path $root ("dist\lemon-" + $Version) }

if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Copy-Item $exe (Join-Path $OutDir 'lemon.exe')

$templates = Join-Path $root 'assets\statement-templates'
if (Test-Path $templates) {
    Copy-Item $templates (Join-Path $OutDir 'statement-templates') -Recurse
    Write-Host '  + statement-templates\'
} else {
    Write-Warning "没有找到 $templates，发布包里将没有题面模板（导出 PDF 会失败）"
}

foreach ($doc in 'README.md', 'LICENSE', 'BUILD.md') {
    $src = Join-Path $root $doc
    if (Test-Path $src) { Copy-Item $src $OutDir }
}

$windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
if (-not (Test-Path $windeployqt)) { throw "找不到 windeployqt：$windeployqt" }

$deployArgs = @('--compiler-runtime', '--no-quick-import', '--no-system-d3d-compiler', '--no-opengl-sw')
if ($BuildType -eq 'Debug') { $deployArgs += '--debug' } else { $deployArgs += '--release' }
if ($BuildType -eq 'RelWithDebInfo') { $deployArgs = $deployArgs | Where-Object { $_ -ne '--release' } }

& $windeployqt @deployArgs (Join-Path $OutDir 'lemon.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败（exit $LASTEXITCODE）" }

$zip = Join-Path (Split-Path $OutDir -Parent) ("lemon-" + $Version + '.zip')
if (Test-Path $zip) { Remove-Item -Force $zip }

Compress-Archive -Path (Join-Path $OutDir '*') -DestinationPath $zip -Force

$zipMB = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "`n打包完成" -ForegroundColor Green
Write-Host "  目录：$OutDir"
Write-Host "  压缩包：$zip  ($zipMB MB)"
