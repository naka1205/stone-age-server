# U01 速度裁定的验证证据

对应[U01 实施记录](../../2026-09-12-speed-resolution.md)。本目录记录采用 SSRC80 的 0.3/dex 最低 1 后的结果；上一批 `2026-09-12/` 目录继续记录 `shared-v0.22.0`，不覆盖旧日志。

| 证据 | 内容 |
|---|---|
| [before-speed.txt](before-speed.txt) / [after-speed.txt](after-speed.txt) | 相同 F03 回归在旧实现下 15 条断言失败，修改后 49/49 通过；首轮使用全新构建目录 |
| [server-ctest.txt](server-ctest.txt) / [client-ctest.txt](client-ctest.txt) | AppleClang 21：17/17、5/5；既有世界断言没有放宽 |
| [server-ci.txt](server-ci.txt) | 服务端完整 CI 6/6：清洁 WERROR 构建、17 项 CTest、相克矩阵反向检查和恢复通过 |
| [client-ci-local.txt](client-ci-local.txt) / [client-ci-fetch.txt](client-ci-fetch.txt) | 新 pin 的 local/fetch CI 各 8/8；共享战斗 108 用例/2651 断言 |
| [shared-lock-verification.json](shared-lock-verification.json) | 本地 `shared-v0.23.0`、两仓提交与五个编译路径的对象比对；旧 tag 未移动 |
| [final-doc-guards.txt](final-doc-guards.txt) | 最终 DR 表、文档索引与代码格式检查，3/3 通过 |
| [gcc-ctest.txt](gcc-ctest.txt) | GCC 15.2 服务端完整 17/17 |
| [gui-results.json](gui-results.json) | 实际 TCP/图形客户端 5/5；正常结算退出 0，四类失败退出 1 |
| [gui-server.txt](gui-server.txt) / [gui-completed.txt](gui-completed.txt) | 本次图形联调的服务端/客户端日志；其他 gui-*.txt 为失败场景 |

构建目录 `/private/tmp/stoneage-speed-20260912/`。F03 的重跑命令：

```sh
/private/tmp/stoneage-speed-20260912/server/tests/sa_rules_battle_test '--test-case=*F03*'
```

图形测试复用上一批的 [gui_smoke.py](../2026-09-12/gui_smoke.py)，传入本批的新服务端与 GUI 可执行文件。源码依据是 `StoneAge/gmsv/src/battle/battle.c:4297–4314`，`sequence` 相加见同文件 `:4144`。用户采纳的是这个源码分支，证据不包含运行旧程序或反汇编，也不证明 B80 函数体与它相同。
