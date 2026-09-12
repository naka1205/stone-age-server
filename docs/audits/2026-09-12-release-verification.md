# 修复版本发布验证（2026-09-12）

用户已授权按[实施计划](../12-implementation-roadmap.md)继续执行，本记录承接本地 [U01 验证](2026-09-12-speed-resolution.md)。旧 tag 的源码保持不变。

| 验证 | 当前结果 |
|---|---|
| 发布前复验 | 服务端 CTest 17/17、客户端 5/5；两仓工作区干净 |
| 共享内容 | `shared-v0.23.0` 指向 `6a2882f`；共享目录、IDL 生成物、共享用例及支持文件与当前工作树匹配 |
| Gitee / GitHub master、共享 tag | 两远端一致：server `4440957`、client `2282376`；`v0.22.0` → `ffd9823`、`v0.23.0` → `6a2882f`，均已实际 `ls-remote` 核对 |
| 干净检出、从远端 FetchContent | 单独克隆 Gitee 客户端，无同级服务端；全新缓存取到 `6a2882f` / `shared-v0.23.0`；CTest 5/5、CI 8/8、战斗 108 用例 / 2651 断言 |
| 服务端三平台 CI | [run 34668170412](https://github.com/naka1205/stone-age-server/actions/runs/34668170412)：Linux/GCC、macOS/AppleClang、Windows/MSVC 全部成功；各注册 17 项、CTest 17/17、CI 6/6、断言反向检查通过 |
| 客户端三平台 CI | [run 34668198719](https://github.com/naka1205/stone-age-client/actions/runs/34668198719)：三平台均按发布态取 `shared-v0.23.0`，CTest 5/5、CI 8/8、108 用例 / 2651 断言 |

本页只记录本次发布。历史 `shared-v0.22.0` 和 `v0.23.0` 的本地构建、图形验证证据继续保存在各自目录。

本次远端日志摘录、独立取源 CTest 与提交摘要保存在 [evidence/2026-09-12-release/](evidence/2026-09-12-release/summary.json)。后续文档提交不改变本表验证的运行代码或已发布 tag。
