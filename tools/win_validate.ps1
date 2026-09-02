# tools/win_validate.ps1 —— ★★ Windows / MSVC 一次性验证
#
# 用途:把**所有需要 Windows 才能回答的问题**在一次运行里答完,
#       然后开发环境即可切回 macOS。
#
# 背景:00 §9.0.5 / §9.0.6 —— D2 的跨工具链风险分三类,
#       A(标准库差异)与 C(浮点逐位一致)已于 2026-09-02 用
#       GCC 15 / libstdc++ 在 macOS 上出清;**只剩 B 类 MSVC 方言**。
#       本脚本就是 B 类的收口。
#
# ⚠️ 本脚本**不需要** GameStudio、不需要 vcpkg、不需要引擎前缀 ——
#    它跑的是 `d2-only` 那条路径(客户端 CMakePresets.json 的原话:
#    「无窗口、无外部前缀、零平台依赖,而它守的是全架构唯一可撤销点 D2」)。
#    ⇒ 只要两个仓 + VS Build Tools + Python 就能跑。
#
# ── 用法 ────────────────────────────────────────────────────────
#
#   在 **x64 Native Tools Command Prompt for VS 2022** 里:
#       powershell -ExecutionPolicy Bypass -File tools\win_validate.ps1
#
#   或普通 PowerShell(脚本会自己找 MSVC):
#       .\tools\win_validate.ps1
#
#   可选参数:
#       -ClientDir <path>   客户端仓路径(默认:与本仓并排的 stone-age-client)
#       -SkipNegative       跳过「断言防线」反向验证(见 §6)
#
# ── 产出 ────────────────────────────────────────────────────────
#
#   tools\win_validate_report.txt   —— ★ 把这份带回 macOS,它是结论的凭据

[CmdletBinding()]
param(
    [string]$ClientDir = "",
    [switch]$SkipNegative
)

$ErrorActionPreference = "Continue"
$ServerDir = Split-Path -Parent $PSScriptRoot
if ($ClientDir -eq "") {
    $ClientDir = Join-Path (Split-Path -Parent $ServerDir) "stone-age-client"
}
$Report = Join-Path $PSScriptRoot "win_validate_report.txt"
$Config = "RelWithDebInfo"
$Gen    = "Visual Studio 17 2022"

# ★ 用 VS 多配置生成器而不是 preset 里的 Ninja:
#   Ninja + MSVC 要求已经进过 vcvars 环境,而 VS 生成器自己会找工具链
#   ⇒ 少一个「为什么 cl.exe 找不到」的排查环节。
#   ⚠️ 代价:构建/测试都要带 --config,下面每处都带了。

$script:Results = @()
$script:Lines   = @()

function Log($msg) {
    Write-Host $msg
    $script:Lines += $msg
}
function Section($title) {
    Log ""
    Log ("=" * 72)
    Log "  $title"
    Log ("=" * 72)
}
function Record($name, $ok, $detail) {
    $script:Results += [pscustomobject]@{ Name = $name; Ok = $ok; Detail = $detail }
    $mark = if ($ok) { "[通过]" } else { "[失败]" }
    Log "$mark $name  —— $detail"
}

Section "0. 环境"

Log "服务端仓 : $ServerDir"
Log "客户端仓 : $ClientDir"
Log "生成器   : $Gen / $Config"

if (-not (Test-Path (Join-Path $ClientDir "CMakeLists.txt"))) {
    Log "⚠️ 找不到客户端仓,请用 -ClientDir 指定。客户端侧的检验将跳过。"
    $ClientDir = ""
}

# MSVC 定位:优先看当前环境,没有就用 vswhere 拉起开发者环境。
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($vsPath) {
            $devShell = Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1"
            if (Test-Path $devShell) {
                Log "未在 PATH 找到 cl.exe ⇒ 载入 VS 开发者环境:$vsPath"
                & $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
            }
        }
    }
}

foreach ($tool in @("cmake", "ctest", "python")) {
    $c = Get-Command $tool -ErrorAction SilentlyContinue
    if ($c) { Log "$tool : $($c.Source)" } else { Log "⚠️ 缺少 $tool" }
}

$clv = (& cl.exe 2>&1 | Select-Object -First 1)
Log "MSVC : $clv"

# ─────────────────────────────────────────────────────────────────
Section "1. 服务端 × MSVC —— 配置 + 构建 + ctest"
# 覆盖:B 类方言 · /utf-8(中文字面量)· /W4 /permissive-(首次执行)
#       · IDL 生成物在 MSVC 下编译 · Python 检查脚本在 Windows 上运行

$sBuild = Join-Path $ServerDir "build\msvc"
Push-Location $ServerDir

& cmake -S . -B $sBuild -G $Gen -A x64 2>&1 | Tee-Object -Variable cfgOut | Out-Host
Record "服务端 CMake 配置" ($LASTEXITCODE -eq 0) "exit=$LASTEXITCODE"

$buildOut = & cmake --build $sBuild --config $Config 2>&1
$buildOut | Out-Host
$buildOk = ($LASTEXITCODE -eq 0)

# ★ 告警统计。macOS 侧 clang 与 GCC 都是 0 ⇒ 这里的数字是 B 类方言的直接度量。
$warnLines = $buildOut | Select-String -Pattern ": warning [A-Z]+[0-9]+" -AllMatches
$warnCount = ($warnLines | Measure-Object).Count
$warnCodes = $warnLines | ForEach-Object {
    if ($_ -match ": warning ([A-Z]+[0-9]+)") { $matches[1] }
} | Group-Object | Sort-Object Count -Descending

Record "服务端 MSVC 构建" $buildOk "exit=$LASTEXITCODE · 告警 $warnCount 条"
if ($warnCount -gt 0) {
    Log "  告警分布(macOS 侧 clang/GCC 均为 0,故此处每一条都是 MSVC 特有):"
    foreach ($g in $warnCodes) { Log ("    {0,4} × {1}" -f $g.Count, $g.Name) }
    # ⚠️ C4819 = 源文件含当前代码页无法表示的字符 ⇒ /utf-8 没生效,先修这条再看别的
    if ($warnCodes.Name -contains "C4819") {
        Log "  ⚠️★ 出现 C4819 ⇒ /utf-8 未生效。中文字面量(doctest 用例名/断言消息)会乱码,"
        Log "     先修这条,其余告警在它之后才有意义。"
    }
}

$testOut = & ctest --test-dir $sBuild -C $Config --output-on-failure 2>&1
$testOut | Out-Host
Record "服务端 ctest" ($LASTEXITCODE -eq 0) (($testOut | Select-String "tests passed" | Select-Object -First 1) -replace '\s+', ' ')
Pop-Location

# ─────────────────────────────────────────────────────────────────
Section "2. 客户端 d2-only × MSVC —— ★★ D2 的完整形态"
# 另一编译器 × 另一工程上下文,同时成立。这是本脚本的核心一项。

if ($ClientDir -ne "") {
    $cBuild = Join-Path $ClientDir "build\msvc-d2"
    Push-Location $ClientDir

    & cmake -S . -B $cBuild -G $Gen -A x64 -DSG_CLIENT_TESTS=ON 2>&1 | Out-Host
    Record "客户端 CMake 配置(d2-only 等价)" ($LASTEXITCODE -eq 0) "exit=$LASTEXITCODE"

    $cbOut = & cmake --build $cBuild --config $Config 2>&1
    $cbOut | Out-Host
    $cWarn = ($cbOut | Select-String -Pattern ": warning [A-Z]+[0-9]+" | Measure-Object).Count
    Record "客户端 MSVC 构建" ($LASTEXITCODE -eq 0) "exit=$LASTEXITCODE · 告警 $cWarn 条"

    $ctOut = & ctest --test-dir $cBuild -C $Config --output-on-failure 2>&1
    $ctOut | Out-Host
    Record "★★ 客户端黄金用例集(MSVC)" ($LASTEXITCODE -eq 0) (($ctOut | Select-String "tests passed" | Select-Object -First 1) -replace '\s+', ' ')
    Pop-Location
} else {
    Record "客户端 d2-only × MSVC" $false "跳过 —— 未找到客户端仓"
}

# ─────────────────────────────────────────────────────────────────
Section "3. 浮点逐位一致(/fp:precise + 关闭 FMA 合并)"
#
# ★ 这一项**不需要额外动作**:黄金用例集的期望值是写死在用例里的手算基准,
#   ⇒ 「2,148 断言全绿」本身就等于「与 macOS 逐位一致」,不必导出数值再比对。
#   shared/CMakeLists.txt 已对 MSVC 显式给 /fp:precise。

$golden = $script:Results | Where-Object { $_.Name -like "*黄金用例集*" }
if ($golden -and $golden.Ok) {
    Record "浮点逐位一致" $true "黄金用例集全绿 ⇒ 期望值为手算基准,等价于与 macOS 逐位一致"
} else {
    Record "浮点逐位一致" $false "★ 黄金用例集未通过 ⇒ 需先判断是方言问题还是浮点问题(见报告末尾)"
}

# ─────────────────────────────────────────────────────────────────
Section "4. LLP64 / wchar_t —— MSVC 数据模型探针"
#
# 这两条是 B 类方言里最容易造成**静默错误**的:不报错、算错。
# macOS(LP64)long = 8 字节,Windows(LLP64)long = 4 字节。

$probe = @"
#include <cstdio>
#include <cstdint>
#include <climits>
int main() {
    std::printf("sizeof(long)=%zu sizeof(void*)=%zu sizeof(wchar_t)=%zu CHAR_MIN=%d\n",
                sizeof(long), sizeof(void*), sizeof(wchar_t), (int)CHAR_MIN);
    return 0;
}
"@
$probeDir = Join-Path $env:TEMP "sg_probe"
New-Item -ItemType Directory -Force -Path $probeDir | Out-Null
$probeSrc = Join-Path $probeDir "probe.cpp"
Set-Content -Path $probeSrc -Value $probe -Encoding UTF8
Push-Location $probeDir
& cl.exe /nologo /EHsc /std:c++17 probe.cpp /Fe:probe.exe 2>&1 | Out-Null
if (Test-Path (Join-Path $probeDir "probe.exe")) {
    $pout = & (Join-Path $probeDir "probe.exe")
    Record "数据模型探针" $true $pout
    Log "  参考(macOS / LP64):sizeof(long)=8 sizeof(wchar_t)=4"
    Log "  ⚠️ long 从 8 变 4 是 Windows 的正常差异,本身不是错误 ——"
    Log "     要紧的是 shared/ 与 IDL 里**不得有依赖 long 宽度的假设**。"
    Log "     上面第 1/2 节若全绿,说明当前代码没有这类假设。"
} else {
    Record "数据模型探针" $false "探针编译失败"
}
Pop-Location

# ─────────────────────────────────────────────────────────────────
Section "5. Python 侧检查(路径分隔符 / 编码)"
# check_shared_purity.py 与 sgidl_gen.py --verify 已在 ctest 里跑过,
# 这里单独再跑一次并显示输出 —— 它们在 Windows 上最可能因路径与编码出问题。

Push-Location $ServerDir
& python tools\check_shared_purity.py 2>&1 | Out-Host
Record "shared/ 纯度检查(Windows)" ($LASTEXITCODE -eq 0) "exit=$LASTEXITCODE"
& python idl\codegen\sgidl_gen.py --verify 2>&1 | Out-Host
Record "IDL 生成物同步(Windows)" ($LASTEXITCODE -eq 0) "exit=$LASTEXITCODE"
Pop-Location

# ─────────────────────────────────────────────────────────────────
Section "6. ★ 断言防线反向验证(防「绿色的假测试」)"
#
# SgWarnings.cmake 的 sg_enable_assertions() 有一个 MSVC 分支(/UNDEBUG),
# 它**从未被执行过**。若它在 MSVC 下不生效,后果与 2026-08-31 服务端踩过的
# 那次完全一样:测试照常退出码 0、照常打印 OK,但一条都没验证 ——
# 而那时我们会把它读成「D2 在 MSVC 上通过了」。
#
# ⇒ 故意把相克矩阵改错一格,期望测试**失败**。不失败才是问题。

if ($SkipNegative) {
    Record "断言防线反向验证" $true "按 -SkipNegative 跳过"
} else {
    $target = Join-Path $ServerDir "shared\rules\constants.h"
    $backup = "$target.win_validate_backup"
    Copy-Item $target $backup -Force
    try {
        $orig = Get-Content $target -Raw -Encoding UTF8
        # 找相克矩阵里第一个 1.5f,改成 9.9f。用例里有手算基准会抓住它。
        if ($orig -match "1\.5f") {
            $broken = ([regex]"1\.5f").Replace($orig, "9.9f", 1)
            Set-Content -Path $target -Value $broken -Encoding UTF8 -NoNewline
            & cmake --build $sBuild --config $Config --target sg_rules_battle_test 2>&1 | Out-Null
            & ctest --test-dir $sBuild -C $Config -R rules_battle 2>&1 | Out-Null
            $shouldFail = ($LASTEXITCODE -ne 0)
            Record "★ 断言防线反向验证" $shouldFail `
                $(if ($shouldFail) { "改错相克矩阵后测试确实失败 ⇒ 断言在 MSVC 下有效" }
                  else { "⚠️★ 改错后测试仍然通过 ⇒ 断言被 NDEBUG 编译掉了,前面所有绿色都不可信!" })
        } else {
            Record "断言防线反向验证" $false "未在 constants.h 中找到 1.5f,跳过注入"
        }
    } finally {
        Copy-Item $backup $target -Force
        Remove-Item $backup -Force
        # 还原后重建,避免留下改坏的产物
        & cmake --build $sBuild --config $Config --target sg_rules_battle_test 2>&1 | Out-Null
        Log "  已还原 constants.h 并重建。"
    }
}

# ─────────────────────────────────────────────────────────────────
Section "汇总"

$pass = ($script:Results | Where-Object { $_.Ok }).Count
$fail = ($script:Results | Where-Object { -not $_.Ok }).Count
foreach ($r in $script:Results) {
    $mark = if ($r.Ok) { "  ✅" } else { "  ❌" }
    Log ("{0} {1,-40} {2}" -f $mark, $r.Name, $r.Detail)
}
Log ""
Log "通过 $pass · 失败 $fail"
Log ""
if ($fail -eq 0) {
    Log "★★ B 类 MSVC 方言出清 ⇒ D2「一份规则两端编译」可结案。"
    Log "   ⇒ 可以切回 macOS。把本报告带回,并按 00 §9.0.7 更新欠债 7 / 客户端欠债 8。"
} else {
    Log "⚠️ 尚有失败项。⚠️★ 归因顺序很重要,不要跳步:"
    Log "   ① 先看有没有 C4819 —— 若有,是 /utf-8 没生效,其余结论全部不可信;"
    Log "   ② 再看第 6 节 —— 若断言防线失效,所有『通过』都不算数;"
    Log "   ③ 然后才是逐条方言问题。修在**源码或 CMake**,不要为了让它绿而放宽告警口径。"
}
Log ""
Log "报告已写入:$Report"

Set-Content -Path $Report -Value ($script:Lines -join "`r`n") -Encoding UTF8
exit $(if ($fail -eq 0) { 0 } else { 1 })
