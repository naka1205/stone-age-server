# 2026-09-12 修复与最后补证

当前结论以[修复结果](../../2026-09-12-remediation-results.md)为准。这里保存本轮实际输出；2026-09-11 的旧反例单独保留，未被覆盖。

旧探针保留旧接口与反例输入，入库时只整理了 C++ 排版和文件末尾空行；旧结果日志没有重跑或改写。修复后的接口验收使用正式测试，不能将旧探针直接编译失败解释为新实现回归。

| 文件 | 内容 |
|---|---|
| [server-ci.txt](server-ci.txt) | AppleClang 清洁 WERROR 构建、17 项 CTest、故意改错相克矩阵再恢复的反向检查；最后 6/6 通过 |
| [world-final.txt](world-final.txt) | 增强后的捕获失败线上标记、宠物真实受伤回写、换宠失败事件回归 |
| [client-ctest.txt](client-ctest.txt) | 客户端本地联调的 5 项 CTest |
| [server-gcc.txt](server-gcc.txt) / [client-gcc.txt](client-gcc.txt) | GCC 15.2 的完整 17/17 与 5/5 |
| [server-sanitizers.txt](server-sanitizers.txt) / [client-sanitizers.txt](client-sanitizers.txt) | ASan+UBSan，halt_on_error=1 |
| [gui-results.json](gui-results.json) | 真实图形客户端正常结算、版本拒绝、提前断线、畸形帧、超时的退出码与终态 |
| [gui-server.txt](gui-server.txt) / [gui-completed.txt](gui-completed.txt) | 新服务端与 GUI 成功路径日志；其他 gui-*.txt 为四条失败路径 |
| [unresolved-data.json](unresolved-data.json) | 最后补证：B80 家族经验表初始化值、对照与源码/数据包指纹 |
| [server-first-full-failure.txt](server-first-full-failure.txt) | 首轮完整测试暴露了尚未更新的成长默认值断言；修正后的通过结果在 server-ci.txt |
| [server-ci-sandbox-failure.txt](server-ci-sandbox-failure.txt) | 沙箱禁止 bind 导致真实 socket 测试失败；获准环境下的完整结果在 server-ci.txt |

图形测试启动器 [gui_smoke.py](gui_smoke.py) 只启动新工程程序与本机回环端口，参数示例：

```sh
python3 docs/audits/evidence/2026-09-12/gui_smoke.py \
  --server /path/to/build/server/src/stone_age_server \
  --client /path/to/build/gui/stone_age_client \
  --client-cwd /path/to/stone-age-client \
  --output /path/to/gui-smoke-output
```

只读数据补证可在服务端仓运行：

```sh
python3 docs/audits/evidence/2026-09-12/unresolved_probe.py
```

它复用 `stoneage-plan/tools/probe_data_values.py`，按 COFF 数据符号读取 `.data/.rdata`，不执行原版、不解码机器指令。B80 的常量事实仅适用于当前指纹的私服重编译包，不自动证明官方版本或该常量的全部运行时使用路径。

本轮构建根目录为 `/private/tmp/stoneage-remediation-20260912/`。所有产物路径与测试规模均为本次实际运行；远端发布和未运行的平台不包含在这些日志中。
