# journal / 18-petskill.md — 宠技域:批次 B1 直攻系

> **本文件收录**:批次 B1 —— 直攻系宠物技能在 L3 的落地。
>
> **批次编号**:§9.0.61(共 1 节)
>
> ★ **编号沿用 `00-architecture.md` 原 §9.0.x 体系,搬家未改号** —— 全仓约 600 处 `§9.0.x` 引用因此继续有效。总映射见 [`README.md`](README.md)。
>
> ★ 本目录是**开发流程账**(逐批次的取证 / 交付 / 复验 / 教训)。
> 架构裁定看 [`../00-architecture.md`](../00-architecture.md);决策看 [`../11-decision-register.md`](../11-decision-register.md);
> 还欠什么看 [`../backlog/`](../backlog/);和原版哪里不一样看 [`../deviations/`](../deviations/)。

---

### 9.0.61 ★★ 批次 B1 —— 直攻系宠技:RENZOKU / GBREAK / GBREAK2 / MIGHTY / POWERBALANCE(2026-09-15)

**为什么是它**:欠债 1 的批次表(`00` §1.3.1)里 B 批次(宠技表,74 函数 / 1,920 行)是 A/D 之后最大的未做块。按 8.0 生产数据 `petskill2.txt`(147 行,69 个函数)实测分类后,**直攻系** 17 行是其中唯一"L3 快照输入面已齐、无未移植前置"的切片 —— 宠物入场机制(M.2/PET_OUT)、`CombatModifiers` 投影分工(I.4 先例)、攻击管线(批次 0.5)全部就位。⇒ B 批次第一批只做这一片,其余宠技(治疗/状态/召唤 61 行、攻击魔法 36 行)各自绑未移植链路,继续划出。

**交付**:
- `shared/rules/Combatant.h`:`CombatModifiers` 新增 7 个宠技投影字段(`pet_skill_direct` 门 / `pet_skill_hits` / `pet_skill_damage_percent` / `pet_skill_duck_bonus` / `pet_skill_guard_break` / `pet_skill_attack_percent` / `pet_skill_defense_percent`),**全部默认值 = 无技能**(非 PET_SKILL 指令 / 表外技能与 B1 之前逐位一致,不摇 rng)。字段注解含逐项源码锚点。
- `shared/rules/Battle.cpp`:指令分发处补 `PET_SKILL` 直攻系 case(**在 switch 里补,不在调用方拦** —— 遵守该处批次 0.5 注记的既有纪律);五个技能共用同一条 strike 管线,差异全由 `mods` 承载。关键语义(全部回 `StoneAge/gmsv/src/battle/` 逐处核实):
  - **RENZOKU**:段数是**覆盖**不是抽取(`battle.c:7263` 直接赋 `attack_max`,跳过 `rollAttackCount` 那笔 rng;宠物侧原版本来就不消费);每段伤害 = `(int)((float)D / N)`(**float 除后截断**),`≤0 抬回 1`(`battle_event.c:2723-2726`)—— ⚠️ 源码是 `if(damage<=0) damage=1` **不是** `max(0,·)` 顺序,负伤也抬 1;分摊只作用非反击段(`:3646` 反击链前 `gDamageDiv=1.0`)。
  - **GBREAK / GBREAK2 是两个不同 COM、语义不同**(与 05 文档口径有出入,已按源码):GBREAK **专打防御** —— 守方本回合 GUARD 且未混乱才落伤,否则 0 伤 MISS(`battle_event.c:4530-4544`);GBREAK2 **无条件落伤**,守方裸 COM==GUARD ⇒ ×1.3 否则 ×0.7(`:1699-1705`,判据**不含混乱**,与防御减伤那条不同)。两者都**不套防御减伤**(else-if 链被 `if(opt==GBREAK) ;;` 短路,连 `GuardAdjust` 的 rng 都不摇,`:1695`)。
  - **MIGHTY**:伤害 ×倍率在 `BATTLE_AttackSeq` **最末行**无条件执行(`battle_event.c:1781`)—— 晚于暴击、晚于破防分支、晚于防御减伤;「避N」是**守方回避率 +N**(`:857` `per += gBattleDuckModyfy`,per 是守方回避率;全路径无取负,数据描述「命中率下降」与源码一致)。ZCode 实现前独立判定,AutoCoder 复核确认。
  - **POWERBALANCE**:指令时刻**替换式**改写有效攻/防(`WORKATTACKPOWER = FIXSTR + (int)(FIXSTR×攻%)`,`pet_skill.c:740-775`),不是叠加;L3 落在 `effectiveAttack/Defense` 的重算,供该回合所有消费者读。
  - **g\* 生命周期**:原版文件级变量每次行动前重置(`battle.c:7094-7095/:7139`)、攻击循环后与反击链前再重置(`:7791-7792`;`battle_event.c:3646`)⇒ 技能只作用于本行动者的普攻循环,反击不受影响 —— 本实现落在"逐指令投影 + 仅非反击段生效"。
- `src/world/World.cpp` / `world/Api.h`:`PetSkillEffect` 效果表 + `projectPetSkill`(每次行动前归零重投影,同 `projectItemUsePower` 的逐回合重算)。⚠️ **不是 petskill2.txt 的解析器**:表是注入式(同 I.4 `ItemEffect` 先例),真数据行由 D 线导入期解析 GBK 后经 `loadPetSkillEffects` 注入;当前用例用真数据行做 fixture(连击2=10 / 三段=11 / 一击必杀=40 / 背水1=50 / 破防=3 / 破防2=543)。
- **表外技能的门**:L3 的 PET_SKILL 分支只在 `pet_skill_direct` 为 true 时走攻击管线 —— 表外技能(尚未移植的治疗/状态/咒术宠技 id)**整次行动跳过、不摇攻击取数**,**不能**静默退化成一次普攻(原版同义:查不到 petskill 函数即指令不成立)。

**复验**(本地 MSVC / `build\ci`,RelWithDebInfo + SA_WERROR):
- `ctest` **22/22**;`rules_battle` **124 例 / 2,856 断言**(+9 例 / +463 断言);`world_tick` **127 例 / 2,299 断言**(+3 例 / +110 断言);`ci_verify.py` **6 项全过**(22 条全部注册,含 `shared_purity` / `code_format`)。
- ★ **反向验证三处**(AutoCoder 独立执行,与 ZCode 分离;每处还原后 bump mtime 重建):
  ① **RENZOKU 分摊抬下限失效**(去掉 `damage<=0 ⇒ 1`)⇒ `rules_battle` **4 断言转红**(`delta == -1`),还原回绿;
  ② **GBREAK2 系数翻转**(守方 GUARD ⇒ ×0.7 / 否则 ×1.3)⇒ **2 断言转红**(`g2_guard` / `g2_plain`),还原回绿;
  ③ **直攻门失效**(表外 PET_SKILL 退化成普攻)⇒ **3 断言转红**(两处 `events.size()` + `rng.calls()==2` —— 连 rng 消耗数都被钉住),还原回绿。
  - ⚠️ 过程注:第一版 RV① 注入"(int) 整除替换 float 除"**0 条转红** —— 正 int 域上 `(int)((float)a/b)` 与 `a/b` 截断结果一致,该注入对 fixture 无区分力 ⇒ 换成"去抬下限"注入后精确转红。**注入要先问"它改变了哪个可观察值"**(同 §9.0.36 ⑥ 那族教训第四次)。

**登记残缺**(有据划出,非遗漏):① 宠技其余三大类:状态系 13 行(依赖 L4 状态 12..43)、攻击魔法系 36 行(依赖 `__ATTACK_MAGIC` 咒术管线)、特殊/变身/召唤系 61 行(各绑未移植子系统)⇒ 各归后续批次;② `Model::Pet` 尚无宠技槽(`unionTable.indexOfPetskill[7]`,源码 `:371-373`)⇒ 玩家从 UI 选技能的链路未通,当前 `skill_id` 由协议直给;③ `petskill2.txt` 的 GBK 解析器与 D 线导入(同 `itemset6.txt` 一批);④ 敌方 AI 使用宠技(`ENEMYSKILL_*` 3 行 + `_PRO_BATTLEENEMYSKILL`)未移植,`fillEnemyCommands` 仍只填普攻;⑤ 客户端宠技 UI / `W|` 指令发送(协议通路已备);⑥ MIGHTY 与暴击的叠加顺序已按源码(先暴击后乘倍率)但**暴击与倍率同时非默认的用例**未单列 —— 判据同源码无条件末乘,如有疑义回 `:1781`。
