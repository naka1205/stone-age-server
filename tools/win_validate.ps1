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
#   在 **x64 Native Tools Command Prompt for VS**(任意版本)里:
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

# ★ 用 VS 多配置生成器而不是 preset 里的 Ninja:
#   Ninja + MSVC 要求已经进过 vcvars 环境,而 VS 生成器自己会找工具链
#   ⇒ 少一个「为什么 cl.exe 找不到」的排查环节。
#   ⚠️ 代价:构建/测试都要带 --config,下面每处都带了。
#
#   ★ 这条取舍在首台执行机上当场兑现:该机 PATH 上的 `ninja` 是 depot_tools 的
#     `ninja.bat`(转 Python),实测 `cmake -G Ninja` 直接 unknown error
#     ⇒ 走 preset 的 Ninja 路径这一趟根本起不来。
#
# ⚠️★ **但生成器版本不能写死**(2026-09-02 实测纠正)。
#   初版写死 `Visual Studio 17 2022`,而首台执行机只装了 **VS 18 Community(2026)**
#   ⇒ 配置期就失败,六项检验一条都拿不到。
#   ★ 这恰恰是本脚本最不该发生的失败:它是**一次性收口**,人已经在 Windows 上了,
#     为一行版本号空跑一趟,代价是再组织一次「进 Windows」。
#   ⇒ 改为「装了哪个用哪个」,并与**当前 cmake 认识的生成器**求交集 ——
#     两侧都会漂:CMake 4.x 会移除老生成器,新 VS 又要求新 CMake。

function Resolve-VsGenerator {
    # VS 主版本号 → CMake 生成器名。新的在前,命中即取。
    $known = @(
        @{ Major = 18; Gen = "Visual Studio 18 2026" }
        @{ Major = 17; Gen = "Visual Studio 17 2022" }
        @{ Major = 16; Gen = "Visual Studio 16 2019" }
    )
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $majors = @()
    if (Test-Path $vswhere) {
        $majors = @(& $vswhere -products '*' -prerelease `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationVersion |
            ForEach-Object { [int](($_ -split '\.')[0]) } |
            Sort-Object -Descending -Unique)
    }
    $help = (& cmake --help 2>&1) -join "`n"
    foreach ($k in $known) {
        if (($majors -contains $k.Major) -and $help.Contains($k.Gen)) {
            return [pscustomobject]@{ Gen = $k.Gen; Majors = $majors }
        }
    }
    # 兜底:交给 CMake 的默认生成器(4.x 的默认就是本机最新的 VS)。
    # ⚠️ 但**不静默** —— 报告里会写明走了兜底,否则「这次用的是哪个编译器」
    #    这条凭据就断了,而它是本次结论的一部分。
    return [pscustomobject]@{ Gen = ""; Majors = $majors }
}

$vs      = Resolve-VsGenerator
$Gen     = $vs.Gen
$GenArgs = if ($Gen) { @("-G", $Gen, "-A", "x64") } else { @() }

$script:Results = @()
$script:Lines   = @()

# ★★ 控制台编码统一到 UTF-8(2026-09-02 第二次执行后补)。
#
#   PowerShell 5.1 用 `[Console]::OutputEncoding`(默认 = 系统 ANSI,简中是 cp936)
#   解码**子进程**输出。而本仓的子进程输出**是 UTF-8**:
#     · 两个 Python 检查脚本已显式 reconfigure 到 UTF-8;
#     · 测试可执行文件里的中文字面量经 `/utf-8` 编译后,运行期就是 UTF-8 字节。
#
# ⚠️★ 不统一的后果不是「显示乱码」这么轻 —— 本脚本靠 **-match 中文关键字** 判两件事:
#     ① protoc 缺失时的诊断是否清晰(第 5 节);
#     ② ★★ 「相克矩阵重排错误」是否被打印(第 6 节)—— 而那正是
#        **断言被 NDEBUG 编译掉**这一最危险结果的探测分支。
#   解码错了 ⇒ 该分支**永远不会命中**,最危险的结果会被误报成「注入没生效」。
#   ⇒ 这与 `/utf-8`、`-ffp-contract=off` 同类:决定「字节怎么被解释」的,都是语义。
$OrigConsoleEnc = [Console]::OutputEncoding
try {
    chcp 65001 > $null            # 让 cl.exe / ctest 也按 UTF-8 输出
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
} catch { }

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
Log ("已装 VS 主版本 : " + $(if ($vs.Majors.Count -gt 0) { $vs.Majors -join ", " } else { "(vswhere 未报告)" }))
if ($Gen) {
    Log "生成器   : $Gen / $Config"
} else {
    Log "⚠️ 生成器 : 未匹配到「VS 装了 × cmake 也认识」的生成器 ⇒ 走 CMake 默认生成器 / $Config"
    Log "   ⚠️ 这一行必须留在报告里 —— 「用的哪个编译器」是本次结论的一部分。"
}

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

# ★★ 必须**清洁构建**,否则「告警 N 条」是假证据 ——
#   第二次跑时增量构建什么都不重编,告警数必然是 0,而那个 0 不代表任何事。
#   本脚本的产物是一份**凭据**,凭据不能依赖「这是第几次跑」。
if (Test-Path $sBuild) {
    Log "清理旧构建目录(保证告警数是真的):$sBuild"
    Remove-Item $sBuild -Recurse -Force -ErrorAction SilentlyContinue
}

& cmake -S . -B $sBuild @GenArgs 2>&1 | Tee-Object -Variable cfgOut | Out-Host
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

    # ★ 同上:清洁构建,否则告警数无意义。
    if (Test-Path $cBuild) {
        Log "清理旧构建目录:$cBuild"
        Remove-Item $cBuild -Recurse -Force -ErrorAction SilentlyContinue
    }

    & cmake -S . -B $cBuild @GenArgs -DSG_CLIENT_TESTS=ON 2>&1 | Out-Host
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

# ① shared/ 纯度 —— ★ 与 protoc 无关,任何机器都该能跑
& python tools\check_shared_purity.py 2>&1 | Out-Host
Record "shared/ 纯度检查(Windows)" ($LASTEXITCODE -eq 0) "exit=$LASTEXITCODE"

# ② IDL 生成物同步 —— ⚠️ 它需要 **protoc**,而 protoc 是**构建期工具**:
#    DR-TS1 边界 ①(运行时不链接 libprotobuf)+ DR-TS2(生成物入库)
#    ⇒ 「本机没有 protoc」是正常状态,且**与 B 类 MSVC 方言无关**,不在本趟收口范围。
#    ⇒ 无 protoc 时记为「未验」而不是「失败」——
#      ★ 但脚本仍要跑一遍,验它在 Windows 上给的是**一句能照着做的诊断**,
#        而不是 subprocess 抛的 `WinError 2` traceback(2026-09-02 首跑就是那样)。
$idlOut = (& python idl\codegen\sgidl_gen.py --verify 2>&1) -join "`n"
$idlOut | Out-Host
$idlExit = $LASTEXITCODE
if (Get-Command protoc -ErrorAction SilentlyContinue) {
    Record "IDL 生成物同步(Windows)" ($idlExit -eq 0) "exit=$idlExit"
} else {
    $cleanDiag = ($idlOut -match "找不到 protoc") -and ($idlOut -notmatch "Traceback")
    Record "IDL 生成物同步(Windows)" $cleanDiag `
        $(if ($cleanDiag) {
              "⏭ 未验 —— 本机无 protoc(构建期工具)。★ 已确认给的是清晰诊断而非 traceback"
          } else {
              "⚠️ 本机无 protoc,且脚本未给出清晰诊断(出现 traceback)⇒ 须修脚本"
          })
    Log "  ⚠️ 「schema 与生成物同步」这道关**本趟未验**。它不属 B 类方言,但 CI 上不能缺(02 §8)。"
}
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
#
# ⚠️★★ **初版有两处写错,2026-09-02 首次执行时实测纠正**:
#
#   ① **目标打错了。** 初版打 `sg_rules_battle_test` / `-R rules_battle`,但守相克矩阵的
#      `assert(false && "相克矩阵与 05-battle.md §3.4 原表不符")` 在
#      **tests/contract_smoke.cpp:72**,不在黄金用例集里;
#      而黄金用例集用的是 **doctest 的 CHECK —— 它与 NDEBUG 无关**,
#      不管 `/UNDEBUG` 生效与否,改错矩阵它都会红。
#      ⇒ 那样得到的「失败」**证明不了** `sg_enable_assertions()` 的 MSVC 分支有效,
#        反而会把一个**假阳性**读成「断言在 MSVC 下有效」。
#        ★★ 这正是本节要防的那类错误,只不过发生在上一层。
#
#   ② **注入模式写错了。** 初版找 `1.5f`,而 `kElementMatrix` 是 **double**,
#      源码里根本没有 `1.5f` ⇒ 本节从未真正执行过(首跑即「跳过注入」)。
#
# ★ 换成 contract_smoke 后它是个**精确探针** —— CheckElementMatrix() 先 printf 再 assert
#   ⇒ 四种结果可分辨,不再只看退出码:
#     ① 失败且输出含 "Assertion"        ⇒ 断言生效(想要的)
#     ② 通过但输出含「相克矩阵重排错误」  ⇒ ★★ 断言被编译掉了(危险的那种)
#     ③ 注入后构建失败                  ⇒ 不结论(注入方式的问题,不是断言的问题)
#     ④ 通过且什么都没打印              ⇒ 注入没进到产物,本项不成立

if ($SkipNegative) {
    Record "断言防线反向验证" $true "按 -SkipNegative 跳过"
} else {
    $target = Join-Path $ServerDir "shared\rules\constants.h"
    $backup = "$target.win_validate_backup"
    Copy-Item $target $backup -Force
    try {
        $orig = Get-Content $target -Raw -Encoding UTF8
        # 打在 kElementMatrix 初始化列表里的**第一格 1.5**(攻地 / 守水)。
        # ★ 模式绑在标识符 `kElementMatrix` 上,不绑在中文注释上 —— 注释会被重排,代码不会。
        #   它的期望值由 contract_smoke 的 kDocMatrix 逐项对照 ⇒ 必然被那条 assert 抓住。
        $pattern = [regex]'(?s)(kElementMatrix.*?=\s*\{.*?)1\.5'
        if ($pattern.IsMatch($orig)) {
            Set-Content -Path $target -Value ($pattern.Replace($orig, '${1}9.9', 1)) `
                        -Encoding UTF8 -NoNewline

            & cmake --build $sBuild --config $Config --target sg_contract_smoke 2>&1 | Out-Null
            $nbOk = ($LASTEXITCODE -eq 0)

            # ⚠️ `--timeout 60`:MSVC 的 assert 走 _wassert,控制台程序**理论上**写 stderr
            #    后 abort,但别把整趟验证押在这个假设上 —— 万一弹窗,超时也算失败,不会挂死。
            $ntOut = (& ctest --test-dir $sBuild -C $Config -R contract_smoke `
                              --output-on-failure --timeout 60 2>&1) -join "`n"
            $ntFailed  = ($LASTEXITCODE -ne 0)
            $sawAssert = ($ntOut -match "[Aa]ssertion")
            $sawPrintf = ($ntOut -match "相克矩阵重排错误")

            if (-not $nbOk) {
                Record "★ 断言防线反向验证" $false `
                    "注入后**构建失败** ⇒ 本项不结论(是注入方式的问题,不是断言的问题)"
            } elseif ($ntFailed -and $sawAssert) {
                Record "★ 断言防线反向验证" $true `
                    "改错矩阵后 assert 确实触发并使测试失败 ⇒ /UNDEBUG 在 MSVC 下有效"
            } elseif ($ntFailed) {
                Record "★ 断言防线反向验证" $true `
                    "测试确实失败,但输出未见 assert 字样(可能被 abort 截断)⇒ 防线成立,证据偏弱"
            } elseif ($sawPrintf) {
                Record "★ 断言防线反向验证" $false `
                    "⚠️★★ 已打印「相克矩阵重排错误」却仍然通过 ⇒ **assert 被 NDEBUG 编译掉了**,前面所有绿色都不可信!"
            } else {
                Record "★ 断言防线反向验证" $false `
                    "⚠️★ 改错后仍通过、且连 printf 都没出现 ⇒ 注入没进到编译产物,本项不成立"
            }
        } else {
            Record "★ 断言防线反向验证" $false `
                "未匹配到 kElementMatrix 的初始化列表 ⇒ 注入模式已过期,须同步修本脚本(勿改测试)"
        }
    } finally {
        Copy-Item $backup $target -Force
        Remove-Item $backup -Force
        # 还原后重建,避免留下改坏的产物
        & cmake --build $sBuild --config $Config --target sg_contract_smoke 2>&1 | Out-Null
        Log "  已还原 constants.h 并重建 sg_contract_smoke。"
    }
}

# ─────────────────────────────────────────────────────────────────
Section "汇总"

# ⚠️★ `@(...)` 不能省(2026-09-02 实测):PowerShell 里 `Where-Object` 只筛出**一项**时
#   返回的是标量而不是数组,`.Count` 取不到值 ⇒ 汇总会印成「失败 」(空)。
#   一份**藏起失败条数**的报告不配当凭据 —— 而失败恰好只有一条时最容易发生。
$pass = @($script:Results | Where-Object { $_.Ok }).Count
$fail = @($script:Results | Where-Object { -not $_.Ok }).Count
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
try { [Console]::OutputEncoding = $OrigConsoleEnc } catch { }
exit $(if ($fail -eq 0) { 0 } else { 1 })
