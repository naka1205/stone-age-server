# P2/P3 验收证据

- `gui-results.json`：最终内容版本、图集哈希、原生键鼠验收结果及保存前后的角色/宠物/道具记录。
- `03-map.png`、`04-manual-battle.png`、`05-rewards.png`、`06-relogin.png`、`07-server-restart.png`：客户端自身 framebuffer 截图，从 1920×1280 等比缩为 960×640。战斗为敌方左上、我方右下。
- `local-verification.json`：本地完整测试与实际键鼠事件数量。
- `server-ci.json`、`client-ci.json`：GitHub API 返回的已完成运行、提交和逐 job 结果。
- `server-ci-excerpts.txt`、`client-ci-excerpts.txt`：逐 job 原始日志中与取源、依赖版本和验收有关的原文摘录。
- `independent-fetch.log`、`independent-fetch.json`：固定客户端提交的独立工作树，从 Gitee 实际拉取共享 tag 后的完整验证日志及哈希记录。

测试密码、数据库连接配置、CA 私钥和原始运行目录不在证据包中。实现范围与未完成系统见[验收记录](../../2026-09-12-playable-loop.md)。
