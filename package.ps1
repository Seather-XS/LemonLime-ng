#Requires -Version 5.1
<#
.SYNOPSIS
    把编译好的 lemon.exe 变成「拷到别的机器上就能双击运行」的完整软件，顺手删掉用不到的文件。

.DESCRIPTION
    默认就地部署到 .\build（也就是 lemon.exe 所在目录）：
      1. （可选）先构建 —— 直接调 build.ps1，沿用同一个 build 目录与配置；
      2. windeployqt 拉齐 Qt 运行库 + platforms / styles / iconengines / imageformats 插件
         与 MinGW 运行库（libgcc / libstdc++ / libwinpthread）；
      3. 复制题面模板 assets\statement-templates（PDF 导出要用）与 LICENSE；
      4. 删掉用不到的文件：Qt 自带的几十种语言翻译（只留 zh_CN / zh_TW）、
         opengl32sw.dll、D3Dcompiler_47.dll、多余图片插件（只留 SVG）、调试符号等；
      5. 跑一次 lemon.exe --self-test 自检：把 PATH 里的 Qt / MinGW 全部去掉，
         确认它真的能独立跑（平台插件、SVG 图标、题面模板、内嵌翻译、主窗口都过一遍）。

    已经存在的构建产物（*.obj、*.a、CMakeFiles\ ...）默认原样留着，ninja 还能继续增量编译；
    要一个「干净的发布目录」就用 -OutDir 指到别处，或者加 -PruneBuildJunk 就地清掉。

.PARAMETER BuildDir
    构建目录（默认 .\build）。

.PARAMETER OutDir
    部署目录。默认与 -BuildDir 相同（就地部署）；指到别处时会先清空那个目录，
    再把 lemon.exe 拷过去部署。

.PARAMETER QtDir
    Qt 安装目录；默认从 CMakeCache.txt 里读，读不到就用 build.ps1 的默认值。

.PARAMETER NoBuild
    不重新构建，只对现成的 lemon.exe 做部署 + 精简。

.PARAMETER KeepEverything
    不删任何文件（排查「是不是删多了」时用）。

.PARAMETER PruneBuildJunk
    顺便删掉构建中间产物（*.obj / *.a / CMakeFiles / build.ninja / CMakeCache.txt...）。
    注意：下次构建会变成完整重编。

.PARAMETER SkipSelfTest
    不跑 --self-test 自检。

.PARAMETER NoTemplates
    不复制题面模板（如果打算继续用仓库里的 assets\statement-templates 就加它）。

.EXAMPLE
    .\package.ps1
    构建 + 就地部署到 .\build，然后自检。

.EXAMPLE
    .\package.ps1 -NoBuild -KeepEverything
    不构建、不删文件，只看 windeployqt 铺了些什么。

.EXAMPLE
    .\package.ps1 -OutDir .\build\release
    生成一个干净的发布目录（不含任何构建中间产物）。
#>
[CmdletBinding()]
param(
    [string]$BuildDir,

    [string]$OutDir,

    [string]$QtDir,

    [string]$ToolchainBin = 'D:\Qt\Tools\mingw1310_64\bin',

    [string]$CmakeBin = 'C:\mingw64\bin\cmake.exe',

    [switch]$NoBuild,

    [switch]$KeepEverything,

    [switch]$PruneBuildJunk,

    [switch]$SkipSelfTest,

    [switch]$NoTemplates,

    # -OutDir 指向已有内容的目录时，默认拒绝（怕误删），加了这个才允许清空
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot

if (-not $BuildDir) { $BuildDir = Join-Path $root 'build' }
if (-not $OutDir) { $OutDir = $BuildDir }

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
$inPlace = ($BuildDir.TrimEnd('\') -eq $OutDir.TrimEnd('\'))

function Write-Step($text) { Write-Host "`n=== $text ===" -ForegroundColor Cyan }
function Write-Ok($text) { Write-Host $text -ForegroundColor Green }
function Write-Dim($text) { Write-Host $text -ForegroundColor DarkGray }

# ------------------------------------------------------------------ 从 CMakeCache 读配置
$cache = Join-Path $BuildDir 'CMakeCache.txt'
$buildType = 'Release'
$ltoOn = $true

if (Test-Path -LiteralPath $cache) {
    $line = Select-String -Path $cache -Pattern '^CMAKE_BUILD_TYPE:STRING=(.*)$' | Select-Object -First 1
    if ($line) { $buildType = $line.Matches[0].Groups[1].Value.Trim() }

    if (-not (Select-String -Path $cache -Pattern '^ENABLE_LTO:BOOL=ON' -Quiet)) { $ltoOn = $false }

    if (-not $QtDir) {
        $qtLine = Select-String -Path $cache -Pattern '^Qt6_DIR:PATH=(.*)$' | Select-Object -First 1
        if ($qtLine) {
            # <Qt>/lib/cmake/Qt6 → <Qt>
            $QtDir = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qtLine.Matches[0].Groups[1].Value.Trim()))
        }
    }
}

if (-not $QtDir) { $QtDir = 'D:\Qt\6.8.3\mingw_64' }

Write-Host "构建目录  : $BuildDir"
Write-Host "部署目录  : $OutDir$(if ($inPlace) { '（就地部署）' })"
Write-Host "构建类型  : $buildType (LTO=$(if ($ltoOn) { 'ON' } else { 'OFF' }))"
Write-Host "Qt        : $QtDir"

# ------------------------------------------------------------------ 1) 构建
if (-not $NoBuild) {
    Write-Step "构建（$buildType）"
    $buildArgs = @{
        BuildDir     = $BuildDir
        BuildType    = $buildType
        QtDir        = $QtDir
        ToolchainBin = $ToolchainBin
        CmakeBin     = $CmakeBin
    }

    if (-not $ltoOn) { $buildArgs['NoLto'] = $true }

    & (Join-Path $root 'build.ps1') @buildArgs
}

$sourceExe = Join-Path $BuildDir 'lemon.exe'

if (-not (Test-Path -LiteralPath $sourceExe)) {
    throw "找不到 $sourceExe，先构建一次（或者不要加 -NoBuild）"
}

# ------------------------------------------------------------------ 2) 准备部署目录
if (-not $inPlace) {
    Write-Step "准备干净的部署目录 $OutDir"

    if (Test-Path -LiteralPath $OutDir) {
        # 保护：不是「已经部署过的目录」而且里面还有东西，就先拒绝，免得误删用户自己的数据。
        if (-not (Test-Path -LiteralPath (Join-Path $OutDir 'lemon.exe')) -and -not $Force) {
            $count = @(Get-ChildItem -LiteralPath $OutDir -Force -ErrorAction SilentlyContinue).Count

            if ($count -gt 0) {
                throw "$OutDir 里已经有 $count 项，而且看不到 lemon.exe。-OutDir 会先清空它，确认要这么做就加 -Force。"
            }
        }

        Remove-Item -LiteralPath $OutDir -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $OutDir 'lemon.exe')
} else {
    Write-Dim '就地部署：构建中间产物保持不动（要干净目录用 -OutDir）。'
}

$exe = Join-Path $OutDir 'lemon.exe'

# ------------------------------------------------------------------ 3) windeployqt
Write-Step '部署 Qt 运行库（windeployqt）'

$windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'

if (-not (Test-Path -LiteralPath $windeployqt)) { throw "找不到 windeployqt：$windeployqt" }

# --no-opengl-sw / --no-system-d3d-compiler：不铺 opengl32sw.dll 与 D3Dcompiler_47.dll
# --no-translations：不铺 Qt 自带翻译（界面文字用的是程序内嵌的 translation/*.qm）
$deployArgs = @('--release', '--compiler-runtime', '--no-quick-import', '--no-opengl-sw',
    '--no-system-d3d-compiler', '--no-translations')

if ($buildType -eq 'Debug') {
    $deployArgs[0] = '--debug'
} elseif ($buildType -eq 'RelWithDebInfo') {
    $deployArgs = $deployArgs | Where-Object { $_ -ne '--release' }
}

& $windeployqt @deployArgs $exe

if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败（exit $LASTEXITCODE）" }

# ------------------------------------------------------------------ 4) 题面模板 + LICENSE
if (-not $NoTemplates) {
    Write-Step '题面模板'

    $templates = Join-Path $root 'assets\statement-templates'
    $target = Join-Path $OutDir 'statement-templates'

    if (Test-Path -LiteralPath $templates) {
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }

        Copy-Item -LiteralPath $templates -Destination $target -Recurse
        Write-Host "  + statement-templates\（PDF 导出用）"
    } else {
        Write-Warning "没有找到 $templates，导出 PDF 会失败"
    }
}

$license = Join-Path $root 'LICENSE'

if (Test-Path -LiteralPath $license) {
    Copy-Item -LiteralPath $license -Destination (Join-Path $OutDir 'LICENSE') -Force
    Write-Host '  + LICENSE'
}

# ------------------------------------------------------------------ 5) 删掉用不到的文件
$removed = New-Object System.Collections.Generic.List[string]
$outRoot = (Resolve-Path -LiteralPath $OutDir).Path.TrimEnd('\')

function Remove-Junk($path) {
    if (Test-Path -LiteralPath $path) {
        $full = (Resolve-Path -LiteralPath $path).Path
        $removed.Add($full.Substring($outRoot.Length).TrimStart('\'))
        Remove-Item -LiteralPath $full -Recurse -Force
    }
}

if ($KeepEverything) {
    Write-Dim '-KeepEverything：跳过精简，下面什么都不删。'
} else {
    Write-Step '删掉用不到的文件'

    # 5.1 用不上的大块头 / 调试文件
    foreach ($pattern in 'opengl32sw.dll', 'D3Dcompiler_*.dll', 'd3dcompiler_*.dll', 'libEGL.dll',
        'libGLESv2.dll', 'vc_redist*.exe', 'qtdiag*.exe', '*.pdb', '*.ilk', '*.exp', '*.lib', '*.prl') {
        Get-ChildItem -Path $OutDir -Filter $pattern -File -ErrorAction SilentlyContinue |
            ForEach-Object { Remove-Junk $_.FullName }
    }

    # 5.2 Qt 插件目录：只留真正用到的几个
    $allowedPluginDirs = @('platforms', 'styles', 'iconengines', 'imageformats', 'translations')
    $pluginDirs = @('tls', 'networkinformation', 'generic', 'scenegraph', 'qmltooling', 'qml',
        'assetimporters', 'virtualkeyboard', 'texttospeech', 'geometryloaders', 'renderers',
        'designer', 'sqldrivers', 'multimedia', 'position', 'sensors', 'webview', 'canbus')

    foreach ($dir in $pluginDirs) {
        if ($allowedPluginDirs -contains $dir) { continue }

        Remove-Junk (Join-Path $OutDir $dir)
    }

    # 5.3 图片插件：程序只画自己资源里的 SVG（PNG 是 QtGui 内置的），
    #     不读用户的 gif / jpeg / ico。
    $imageDir = Join-Path $OutDir 'imageformats'

    if (Test-Path -LiteralPath $imageDir) {
        Get-ChildItem -Path $imageDir -File | Where-Object { $_.Name -ne 'qsvg.dll' } |
            ForEach-Object { Remove-Junk $_.FullName }
    }

    # 5.4 Qt 自带翻译：界面语言用的是程序内嵌的 zh_CN / zh_TW / en_US，
    #     其余几十种语言的 qt_*.qm 全都没用。
    $translationDir = Join-Path $OutDir 'translations'

    if (Test-Path -LiteralPath $translationDir) {
        Get-ChildItem -Path $translationDir -File |
            Where-Object { $_.Name -notin @('qt_zh_CN.qm', 'qt_zh_TW.qm') } |
            ForEach-Object { Remove-Junk $_.FullName }

        if (-not (Get-ChildItem -Path $translationDir -File -ErrorAction SilentlyContinue)) {
            Remove-Junk $translationDir
        }
    }

    Write-Host ("  共删掉 {0} 个（目录/文件）" -f $removed.Count) -ForegroundColor Green

    foreach ($item in ($removed | Sort-Object)) { Write-Dim "    - $item" }

    if ($PruneBuildJunk) {
        Write-Step '顺便清掉构建中间产物（下次构建会完整重编）'

        foreach ($name in 'CMakeFiles', '3rdparty', 'lemon_autogen', 'lemon-base_autogen',
            'lemon-core_autogen', 'Testing', 'tests', '.qt') {
            Remove-Junk (Join-Path $OutDir $name)
        }

        foreach ($pattern in '*.obj', '*.a', '*.qm', 'build.ninja', '.ninja_deps', '.ninja_log',
            'CMakeCache.txt', 'cmake_install.cmake', 'compile_commands.json', 'CPackConfig.cmake',
            'CPackSourceConfig.cmake', 'CTestTestfile.cmake', 'translations.qrc') {
            Get-ChildItem -Path $OutDir -Filter $pattern -File -ErrorAction SilentlyContinue |
                ForEach-Object { Remove-Junk $_.FullName }
        }

        Write-Host ("  共删掉 {0} 个（目录/文件）" -f $removed.Count) -ForegroundColor Green

        foreach ($item in ($removed | Sort-Object)) { Write-Dim "    - $item" }

        # 把中间产物连同 CMakeCache / build.ninja 一起删了，构建系统也就没了；
        # 这里只 configure 一次（不编译），让 cmake --build / ninja 立刻能用。
        if ($inPlace -and (Test-Path -LiteralPath $CmakeBin)) {
            Write-Step '重新生成构建系统（只 configure，不编译）'

            $savedCmakePath = $env:Path
            # ToolchainBin 在前，保证选中的 g++ 和 Qt 预编译库是同一套（见 build.ps1）。
            $env:Path = $ToolchainBin + ';' + (Split-Path $CmakeBin -Parent) + ';' + $env:Path

            try {
                $ltoValue = if ($ltoOn) { 'ON' } else { 'OFF' }

                & $CmakeBin -S $root -B $OutDir -G Ninja "-DCMAKE_BUILD_TYPE=$buildType" `
                    "-DENABLE_LTO=$ltoValue" "-DCMAKE_PREFIX_PATH=$QtDir" | Out-Null

                if ($LASTEXITCODE -ne 0) { throw "重新配置失败（exit $LASTEXITCODE）" }

                Write-Host '  已重新配置：下次构建会完整重编一次，但构建系统是现成能用的。'
            } finally {
                $env:Path = $savedCmakePath
            }
        }
    }
}

# ------------------------------------------------------------------ 6) 自检
$selfTestOk = $false

if (-not $SkipSelfTest) {
    Write-Step '自检：去掉 PATH 里的 Qt / MinGW，确认能独立运行'

    $report = Join-Path $env:TEMP ('lemon-self-test-' + [guid]::NewGuid().ToString('N') + '.txt')
    $savedPath = $env:Path
    $savedCwd = (Get-Location).Path

    try {
        # 只留系统目录：这样跑起来就不是靠 PATH 里的 Qt，而是靠旁边的 DLL 与插件。
        $env:Path = "$env:SystemRoot\system32;$env:SystemRoot"
        Set-Location $OutDir

        $process = Start-Process -FilePath $exe -ArgumentList '--self-test', ('"' + $report + '"') -PassThru
        $process | Wait-Process -Timeout 120 -ErrorAction SilentlyContinue

        if (-not $process.HasExited) {
            $process | Stop-Process -Force
            Set-Location $savedCwd
            throw '自检超时：多半是依赖没拷全（平台插件 / Qt DLL）'
        }

        $code = $process.ExitCode
    } finally {
        $env:Path = $savedPath
        Set-Location $savedCwd
    }

    if (Test-Path -LiteralPath $report) {
        Get-Content -LiteralPath $report | ForEach-Object { Write-Host "      $_" }

        Remove-Item -LiteralPath $report -Force -ErrorAction SilentlyContinue
    } else {
        Write-Host '      （没有生成自检报告）' -ForegroundColor Yellow
    }

    if ($code -ne 0) { throw "自检失败（exit $code）" }

    $selfTestOk = $true
}

# ------------------------------------------------------------------ 7) 结果
Write-Step '结果'

$runtime = @()
$runtime += Get-Item -LiteralPath $exe -ErrorAction SilentlyContinue
$runtime += Get-ChildItem -Path $OutDir -Filter '*.dll' -File -ErrorAction SilentlyContinue

foreach ($dir in 'platforms', 'imageformats', 'iconengines', 'styles') {
    $runtime += Get-ChildItem -Path (Join-Path $OutDir $dir) -File -ErrorAction SilentlyContinue
}

$templatesTarget = Join-Path $OutDir 'statement-templates'

if (Test-Path -LiteralPath $templatesTarget) {
    $runtime += Get-ChildItem -Path $templatesTarget -Recurse -File -ErrorAction SilentlyContinue
}

$runtimeSize = [math]::Round((($runtime | Measure-Object Length -Sum).Sum) / 1MB, 1)
Write-Host ("  运行库 + 程序 + 模板：{0} 个文件，{1} MB" -f (@($runtime).Count), $runtimeSize) -ForegroundColor Green
Write-Host '  最大的几个：'

$runtime | Sort-Object Length -Descending | Select-Object -First 5 | ForEach-Object {
    Write-Dim ("    {0,-28} {1,7} MB" -f $_.Name, [math]::Round($_.Length / 1MB, 2))
}

Write-Host ''
Write-Ok  "完成：$exe"

if ($selfTestOk) { Write-Host '自检通过：这个目录拷到没有 Qt 的机器上也能直接运行。' }

if ($inPlace) {
    if ($PruneBuildJunk -and -not $KeepEverything) {
        Write-Dim '（构建中间产物已清掉，下次构建会完整重编；构建系统已重新配置好。）'
    } else {
        Write-Dim '（构建中间产物仍在原处，ninja 还能继续增量编译；要纯发布目录用 -OutDir。）'
    }
}
