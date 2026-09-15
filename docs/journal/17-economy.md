# journal / 17-economy — 经济域:GoldLedger 单入口与战斗产币

> **本文件收录**:经济地基批(计划项 A4)与 Windows 本机 OpenSSL 根探测修复。
>
> **批次编号**:§9.0.58 · §9.0.59(共 2 节)
>
> ★ **编号沿用 `00-architecture.md` 原 §9.0.x 体系,搬家未改号** —— 全仓约 600 处 `§9.0.x` 引用因此继续有效。总映射见 [`README.md`](README.md)。
>
> ★ 本目录是**开发流程账**(逐批次的取证 / 交付 / 复验 / 教训)。
> 架构裁定看 [`../00-architecture.md`](../00-architecture.md);决策看 [`../11-decision-register.md`](../11-decision-register.md);
> 还欠什么看 [`../backlog/`](../backlog/);和原版哪里不一样看 [`../deviations/`](../deviations/)。

---

### 9.0.58 ★★ 批次 A4 —— 经济地基:`GoldLedger` 单入口 + 战斗产币(DR-EC1/3/4/6,2026-09-15)

**为什么是它**:战果结算批(§9.0.40)把「金钱」划给经济域(`DR-EC6` 的"每场 +10"已裁定);09-15 用户批准按计划实施经济地基批。原版石币是 **5 个载体 + 4 个独立上限、96 个写点里 60 处(62.5%)绕过唯一带校验的 API**(`12-economy` §2.3 / `08-economy` §3.1)⇒ 新实现的第一件事不是给哪个玩法接钱,而是把**唯一写入口**立起来 —— 入口立起之前接的每一个源都是未来的一处旁路。

**交付**:
- `shared/model/Player.h`:`gold` 字段(随身载体,原 `CHAR_GOLD`);字段注解写明全仓唯一合法写入口是 `GoldLedger`。
- `src/world/GoldLedger.cpp`(新文件,声明在 `world/Api.h` 的 GoldLedger 节):`addGold` / `delGold`,**钳位 → 溢出处置 → 审计三步不可拆**(`00` §8.4.3):
  - ① 钳位照抄源码 `char_base.c:3232/:3267` 的「先钳当前余额」—— 超上限的存量在**下一次变更时被拉回上限**,这是可观察语义不是防御代码(用例钉住);
  - ② 溢出处置由 `reason` 决定(DR-EC4:**销毁不再静默**,`kClamped` 带走 `overflow` 数量);扣账余额不足**拒绝**不清零(DR-EC3),且照抄源码 `:3270`「请求量先钳到上限」的怪癖(用例钉住可观察差异);
  - ③ 审计无条件(`kRejected` 也留痕)。审计出口是 `GoldAuditSink` 接口 —— 账本自身可单测(注入捕获式 sink),生产路径由 `World::Impl` 实现并转进**现有**日志通道 `kGoldChanged`(213)。
- 上限公式 `maxHaveGold(trans) = 1,000,000 + trans×1,800,000`(`char_base.c:3212`,`_FIX_MAX_GOLD` 8.0 开)—— **不照抄 8.5 阶梯式**(两代差 100 倍,会抹掉 8.0 经济压力)。⚠️ 玩家实体暂无转生字段 ⇒ 调用方传 0,上限 = 100 万(如实登记,转生域落地时传入即可)。
- 战斗产币(第一个真实源,DR-EC6):`World::tick` 战斗 finished 时对**在场存活玩家**逐个 `addGold(+10)`。三个门照源码:① `CHAR_ISDIE` 死亡不给(`BATTLE_GetExpGold:4252`);② `dpbattle` 决斗点怪不给金(走 `BATTLE_GetDuelPoint`);③ 每场一次(结算段只在 finished 走一遍、随即 retire)。⚠️ **数值 +10 的证据边界**:`csa8.0/gmsv/setup.cf:67` 的 `BATTLEGOLD=10`(B80 有 `getBattleGold` 符号、SSRC80 全树无);实现形态取 SSRC85 `battle.c:3851-3860`(每场固定)。**8.0 真实公式不可判定**(`00` §10.2)⇒ 用例钉的是「这个已裁定取值」,不许读成"原版行为"。
- 观察面 `World::playerGold(session)`(-1 = 无 L2 实体);World 公开面**刻意不提供任何加钱/扣钱方法** —— 要改余额走账本,要验余额走这里。
- **守卫** `tools/check_gold_writes.py`(ctest 项 `gold_writes`):src/ 与 shared/ 里对 `gold` 成员的写**只允许** `GoldLedger.cpp`。⚠️ 编译期 private 收口不可行:`SA::Domain::copyPlayerData`(idl/generated 自由函数)要直写字段做存档快照,挂 friend 会把 shared/model 与 codegen 签名焊死 ⇒ 取「边界检查脚本 + 用例钉住」,与 purity/boundaries 守卫同款。扫描面有意不含 tests/(运行期可达实体只在 src/world 池里,测试构造物摆位不构成旁路)与 idl/(那是存档记录 `PlayerData`,不是 `Model::Player`)。

**复验**(全部本地 MSVC / VS 18 BuildTools,`build\ci`,RelWithDebInfo + SA_WERROR):
- `ctest` **22/22**(本批后 `gold_writes` 入列;`world_tick` 115 → **124 例 / 2,275 断言**,新增 EC×4 + 经济×5;`world_persistence` 5 → **8 例 / 134 断言**,新增 Economy×3:金币进存档快照且钳位可观察 / 超上限存量被拉回 / 重登一致)。
- `gold_writes` 守卫:通过(exit 0)。
- ★ **反向验证三处**(AutoCoder 独立执行,每处还原后 bump mtime 重建 —— §9.0.57 第 7 形态教训):
  ① **账本不落值**:`GoldLedger.cpp` 两处 `player.gold = tx.after` 注入为 `= tx.before`(钳位/提交被架空)⇒ `world_tick` **6 例 / 13 断言**、`world_persistence` **2 例 / 5 断言**精确转红(端到端 +10 断言、快照、重登一致全红),恢复后回绿;
  ② **调用点多付**:`World.cpp` 战斗产币 `kBattleGold` 注入为 `999` ⇒ `playerGold == 10` / `== 20` **4 断言精确转红**(端到端金额判据有区分力),恢复后回绿;
  ③ **守卫有区分力**:往 `World.cpp` 注入一处直写 `p.gold = 777` ⇒ `check_gold_writes.py` **exit 1** 并精确点名注入行(守卫「该红的时候真的会红」,零违规状态下的绿证明不了它),还原后 exit 0。
- 恢复后全量 `ctest` **22/22** 收口(工作树无注入残留,`Select-String` 复核 0 命中)。

**登记残缺**(有据划出,非遗漏):① 其余 4 载体(银行 `CHAR_BANKGOLD` / 个人商店 `CHAR_PERSONAGOLD` / 拍卖 `CHAR_AUCGOLD` / 家族公款 `CHAR_FMBANKGOLD`)未建 —— DR-EC1 裁定**不合并**,各载体属各自域批次;② `delGold` 暂无生产调用点(商店/银行/交易未做),先落地 + 用例钉语义,**第一个真实汇的批次直接接它,不许出现第二份实现**;③ 完整 correlation id 模型挂阶段 2.2 审计事件模型(本批 `correlation` = battle_id);④ 配置化 `BATTLEGOLD` 属 D 线运营旋钮批次;⑤ 战斗产币的**死亡名单精化**(死亡者不领但经验暂存已有,金无暂存态 ⇒ 逃跑/超时离场者也不领)—— 与源码 `BATTLE_Exit` 清 Entry 的行为一致,非偏差。

---

### 9.0.59 ★ Windows 本机 OpenSSL 根自动探测 —— 三处入口的 configure 阻塞解除(2026-09-15)

**现象**(observed,`civ.log` 2026-09-15 10:54):本机 `ci_verify.py` 在 CMake configure 即失败:`Could NOT find OpenSSL (missing: OPENSSL_CRYPTO_LIBRARY OPENSSL_INCLUDE_DIR)` ⇒ 后续全部无法进行。而 09-15 03:38 的 `win_validate` 11/11 全绿 —— 同一台机器,先绿后红。

**取证**(最小探针 + `--debug-find`,复现于本机 CMake 4.3):
- 本机装有 Shlib 安装版 OpenSSL **4.0.2**(`C:\Program Files\OpenSSL-Win64`,include 147 头文件齐全,`lib\VC\x64\MD\libcrypto.lib` 布局完全符合 FindOpenSSL 的 PATH_SUFFIXES);
- FindOpenSSL 的默认前缀集实测试了 `C:/Program Files/OpenSSL`、`C:/OpenSSL-Win64`、`C:/Program Files (x86)/OpenSSL-Win64`……**唯独没试 `C:/Program Files/OpenSSL-Win64`**(带空格 + 版本后缀的布局不在其默认前缀集里)⇒ 干净环境下 configure 必然失败;
- 显式 `-DOPENSSL_ROOT_DIR='C:\Program Files\OpenSSL-Win64'` 后**立即解析成功**(探针 `PROBE_OPENSSL_OK`,include + MD/MDd 两个 config 都找到);
- 03:38 那次全绿是因为当时 shell 环境残留了该变量的定义;10:54 的干净环境没有 ⇒ 「上次能跑」掩盖了「默认搜索从来就没找到过」。GitHub CI 的 windows runner 自带 OpenSSL 且在默认搜索路径内 ⇒ CI 从未踩过,只有本机裸配置会踩。

**修复**:`runtime/tls/CMakeLists.txt` 在 `find_package(OpenSSL)` 之前加一段探测 —— 仅当 `WIN32 AND MSVC` 且 `OPENSSL_ROOT_DIR` 在 cache 与环境中**都未设置**时,按序探测 6 个候选根,取第一个存在 `include/openssl/ssl.h` 的写入 `CACHE PATH`(docstring 标记「探测所得」)。**显式传入时本段完全不生效**(反向核验:显式传入后 configure 成功且 cache 无该标记);CI(Linux 分支)逻辑上不可达。

**验收**(ZCode 执行,AutoCoder 复核):
- 全新 configure(不设 `OPENSSL_ROOT_DIR`,civ.log 同口径)退出码 **0**;`CMakeCache.txt` 实测 `OPENSSL_INCLUDE_DIR=C:/Program Files/OpenSSL-Win64/include`、`LIB_EAY_RELEASE=.../lib/VC/x64/MD/libcrypto.lib`(⚠️ 本版 FindOpenSSL 在 MSVC 分支不产 `OPENSSL_CRYPTO_LIBRARY` 缓存条目,`LIB_EAY_*`/`SSL_EAY_*` 即其本平台等效值),版本 4.0.2 ≥ 1.1.1;
- `ci_verify.py` 退出码 **0**:**22 条全部注册**(含 `idl_verify` —— 本机装有 protoc,未触发预案中的降级)、`ctest` **22/22**、**六项全过**、断言防线反向验证通过;
- 观察面注:服务端仓 `runtime/` 属 **watched 路径**(客户端 `SA_SHARED_WATCHED_PATHS` 含 `runtime/tls`)⇒ 本修复与 §9.0.58 同窗口前推 `shared-v0.27.0` + 客户端换 pin 验发布态。

**登记残缺**:① 候选根 2–6 未逐一实测(本机只有候选 1 存在);② 非 Windows 分支未实跑(`WIN32 AND MSVC` 守卫,Linux CI 上不可达属设计);③ 过程注:验收 configure 因取证脚本自身 grep 问题重跑过 3 次(同命令、均退出码 0,不影响结论)。
