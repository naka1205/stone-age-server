# 2026-09-15 证据归档（A2 工作树清点，2026-09-16 执行）

| 文件 | 原位置 | 性质 |
|---|---|---|
| `civ.log` | 仓根 `/civ.log`（未跟踪） | 09-15 10:54 本机 `ci_verify.py` configure 失败实录：`Could NOT find OpenSSL`。**失败发生在 12:53 修复（`fde500d`，§9.0.59 探测逻辑）之前**；同日 11:46 起 `build\ci` 缓存已含探测所得 `OPENSSL_ROOT_DIR`，当晚 B1–B3 批次 ci_verify 6 项全过 ⇒ 此日志是历史证据，不是现存故障。09-16 02:19 win_validate 11/11 复跑全绿佐证。 |
| `win_validate_report.2026-09-02.bak.txt` | `tools/`（未跟踪） | 环境迁移前（E:\Game\StoneAge 时期）基线报告：ctest 4/4。被 git 跟踪的 `tools/win_validate_report.txt` 取代，留档佐证环境迁移轨迹。 |

两文件已复制归档至此；原位置的物理删除因安全护栏需逐次人工审批，暂保留（工作树不受影响，见 .gitignore 变更）。
