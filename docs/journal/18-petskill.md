# journal / 18-petskill.md — 宠技域:批次 B1 直攻系 + B2 宠技槽与集气 + B3 状态系

> **本文件收录**:宠物技能域批次 —— B1 直攻系、B2 宠技槽与 CHARGE、B3 状态系与铁壁。
>
> **批次编号**:§9.0.61 · §9.0.63 · §9.0.65(共 3 节;§9.0.62/§9.0.64 推送窗口记录在 [`90-push-windows.md`](90-push-windows.md))
>
> ★ **编号沿用 `00-architecture.md` 原 §9.0.x 体系,搬家未改号** —— 全仓约 600 处 `§9.0.x` 引用因此继续有效。总映射见 [`README.md`](README.md)。
>
> ★ 本目录是**开发流程账**(逐批次的取证 / 交付 / 复验 / 教训)。
> 架构裁定看 [`../00-architecture.md`](../00-architecture.md);决策看 [`../11-decision-register.md`](../11-decision-register.md);
> 还欠什么看 [`../backlog/`](../backlog/);和原版哪里不一样看 [`../deviations/`](../deviations/)。

---

### 9.0.61 ★★ 批次 B1 —— 直攻系宠技:RENZOKU / GBREAK / GBREAK2 / MIGHTY / POWERBALANCE(2026-09-15)

**为什么是它**:欠债 1 的批次表(`00` §1.3.1)里 B 批次(宠技表,74 函数 / 1,920 行)是 A/D 之后最大的未做块。按 8.0 生产数据 `petskill2.txt`(147 行,69 个函数)实测分类后,**直攻系** 17 行是其中唯一"L3 快照输入面已齐、无未移植前置"的切片 —— 宠物入场机制(M.2/PET_OUT)、`CombatModifiers` 投影分工(I.4 先例)、攻击管线(批次 0.5)全部就位。

**交付**:
- `shared/rules/Combatant.h`:`CombatModifiers` 新增 7 个宠技投影字段(`pet_skill_direct` 门 / `pet_skill_hits` / `pet_skill_damage_percent` / `pet_skill_duck_bonus` / `pet_skill_guard_break` / `pet_skill_attack_percent` / `pet_skill_defense_percent`),**全部默认值 = 无技能**(非 PET_SKILL 指令 / 表外技能与 B1 之前逐位一致,不摇 rng)。
- `shared/rules/Battle.cpp`:指令分发处补 `PET_SKILL` 直攻系 case(**在 switch 里补,不在调用方拦**);五个技能共用同一条 strike 管线,差异全由 `mods` 承载。关键语义(全部回 `StoneAge/gmsv/src/battle/` 逐处核实):
  - **RENZOKU**:段数是**覆盖**不是抽取(`battle.c:7263`);每段伤害 = `(int)((float)D / N)`(float 除后截断),`≤0 抬回 1`(`battle_event.c:2723-2726`,源码是 `if(damage<=0) damage=1` **不是** `max(0,·)`);分摊只作用非反击段(`:3646`)。
  - **GBREAK / GBREAK2 语义不同**:GBREAK **专打防御**(守方 GUARD 且未混乱才落伤,否则 0 伤 MISS,`:4530-4544`);GBREAK2 **无条件落伤**、守方裸 COM==GUARD ⇒ ×1.3 否则 ×0.7(`:1699-1705`,判据**不含混乱**)。两者都**不套防御减伤**(else-if 链被短路,连 `GuardAdjust` 的 rng 都不摇,`:1695`)。
  - **MIGHTY**:伤害 ×倍率在 `BATTLE_AttackSeq` **最末行**无条件执行(`:1781`);「避N」是**守方回避率 +N**(`:857`,全路径无取负)。
  - **POWERBALANCE**:指令时刻**替换式**改写有效攻/防(`pet_skill.c:740-775`)。
  - **g\* 生命周期**:每次行动前重置(`battle.c:7094-7095/:7139`)、攻击循环后与反击链前再重置(`:7791-7792`;`battle_event.c:3646`)⇒ 落在"逐指令投影 + 仅非反击段生效"。
- `src/world/World.cpp` / `world/Api.h`:`PetSkillEffect` 注入式效果表 + `projectPetSkill`(逐行动归零重投影)。
- **表外技能的门**:`pet_skill_direct` 才走攻击管线 —— 表外技能**整次行动跳过、不摇攻击取数**,不得退化成普攻。

**复验**:`ctest` **22/22**;`rules_battle` 124 → 127 例 / 2,879 断言;`ci_verify.py` 6 项全过。★ **反向验证三处**(AutoCoder 独立):① RENZOKU 抬下限失效 ⇒ 4 断言红;② GBREAK2 系数翻转 ⇒ 2 断言红;③ 直攻门失效 ⇒ 3 断言红(含 `rng.calls()==2`)。⚠️ 过程注:首版 RV 注入"(int) 整除替换 float 除"**0 条转红** —— 正 int 域上两者截断逐位一致,注入无区分力 ⇒ 换"去抬下限"后精确转红(**注入要先问它改变了哪个可观察值**,§9.0.36 ⑥ 同族第四次)。

---

### 9.0.63 ★★ 批次 B2 —— 宠技槽(B2a)+ CHARGE 集气(B2b)(2026-09-15)

**★★ 数据面校正(推翻派单简报的一处口径)**:**宠技槽在 `enemybase1.txt` 的 0 基 25..31 列,不是 18..24**。推导链:`include/enemy.h` 枚举 `E_T_PETSKILL1=19`;载入器 `enemy.c:311-332`:字符列 `E_T_DATACHARNUM=6` 从文件第 1 列起,整型从 1 基第 7 列起映射枚举 0 ⇒ **0 基文件列 = 枚举 + 6**。判据三条:① 乌力行 c15..18 = {80,20,0,0} 正是 M.4b 已验证的 地80/水20(EARTHAT..WINDAT = c15..c18);② c25..31 值分布(1/2/3/10/…/644/645/-1)与 petskill2.txt 的 id 谱系逐个对上,18..24 对不上;③ 死引用 644/645 各 81 槽不在表内 ⇒ 原版 `PETSKILL_GetArray` 返 -1、指令不成立。**派单简报是输入不是规格,规格仍是源码与真数据。**

**交付**:
- **B2a**:`Model::Enemy` / `Pet` 各加 `pet_skills[7]`(`kPetSkillSlots = CHAR_MAXPETSKILLHAVE`);`spawnEnemy` 整组拷(`enemy.c:1204-1206`);捕获七槽全拷(`pet.c:375-377`);`givePetToPlayer` seam。
- **B2b CHARGE 三拍状态机**(跨回合态在 `BattleInstance::charge_of_slot`,世界侧,不上线协议):第一拍(新发指令,静默蓄力、不摇攻击 rng)→ 续拍(`injectChargeCommands` 注入合成指令、`rng.calls()==0` 有断言、World 减一)→ 完成击(`charge_ready=true` 守方不可回避 + `attack_percent=P` 替换式有效攻;击后回写清态)。源码:`pet_skill.c:614-640` · `battle_event.c:5027-5064` · `battle.c:7259/:7510/:7729/:668`。
- **入口持有门**(`onBattleCommand`):主人有默认宠 + `iNum<7` + 宠物槽上确有该 skill + 宠物状态门,任一不过 ⇒ 整批降级 WAIT(与 I| 同款)。

**复验**:`ctest` **22/22**;`world_tick` 132 例 / 2,391 断言(+5);`ci_verify.py` 6 项全过。★ **反向验证四处**:① 捕获不拷七槽 ⇒ 7 红;② 完成击攻%替换失效 ⇒ 2 红(排除值 1433 被钉住);③ 持有门放行 ⇒ 红;④ 蓄力拍摇 rng ⇒ `rng.calls()==0` 红。全部复绿。

**有意不复刻**:CHAR_SLOT 转生门;id 600 狼人特判(表内无 600,投产不可达);集气中来令以"丢弃"表意;StatusSeq 清指令 ⇒ 集气夭折照抄(原 :5440 无豁免);宠物存档未含 pet_skills。

---

### 9.0.65 ★★ 批次 B3 —— 状态系宠技 + 铁壁(2026-09-15)

**为什么是它**:B1/B2 之后,状态系宠技 7 行(毒/猛毒/石/乱/醉/眠 + 铁壁)的全部前置已就位:`rollStatusAttack`(L4.1)、单槽互斥、`clearsCommandOnApply`、`super_wall` 消费端。数据面:id 60/61(毒 turn3/5)、80(石)、90(乱)、100(醉)、110(眠),id 552 = 铁壁(`PETSKILL_MagicStatusChange`,option `铁壁|3|30|全`)。

**回源码复核(本批关键事实,与 15-status.md 登记的文档口径有出入处已按源码核正)**:
- **宠技状态攻击的实参不是文档登记的 Range=30/Bai=1.0**:原文 `battle_event.c:2907-2916` 是 `BATTLE_StatusAttackCheck(…, suitpoison, 40, 2.0, &perStatus)` —— **PerOffset = 局部变量 `suitpoison`,初值 30**(`:2689`「基本中毒%」),仅装备毒分支(`:2904`)会改写它 ⇒ 技能路径 PerOffset=30、Range=40、Bai=2.0。`15-status.md` 的 30/1.0 是魔法/精灵调用点(`:7575`/`:7642`)的参数组。
- **装备毒遮蔽判据原文**(`:2903`):`if(gBattleStausChange == -1 && SUITPOISON > 0)` ⇒ 装备毒只在**技能未指定状态**时兜底(且兜底时 PerOffset 被改写成装备毒值)。
- **回合数**:`StatusTbl[st] = gBattleStausTurn + 1`(`:2918-2919`),**酒醉再折半**(`:2921-2923`)⇒ 共享纯函数 `statusWorkOnApply`(施加落地回合数的单一真源,DR-BT5:世界写与 L3 镜像共用)。
- **施加当场清守方指令**:只列麻/眠/石/障四种(`:2932-2937`)—— 比 `checkCanAct` 的 8 项窄,两者不矛盾(那四种由派发时否决)。
- **铁壁全链路**:`PETSKILL_MagicStatusChange`(pet_skill.c:1726)置 COM1=S_SUPERWALL ⇒ `battle.c:8410-8416`(**独立 case,不落普攻执行组**)⇒ `PETSKILL_MagicStatusChange_Battle`(battle_event.c:7203)⇒ `BATTLE_MultiMagicStatusChange`(battle_magic.c:2001-2031):目标已有**任一** MagicTbl 状态则整笔跳过(族内单槽),否则 `MAGICSUPERWALL=turn; OTHERSTATUSNUMS=nums`;**施加端无 rng、无攻击、无事件**(`Bm|` 串 8.0 已注释)。消费:`battle_event.c:1195-1200` 防御公式。**过期**:`BATTLE_MagicStatusSeq`(battle.c:9059-9078)在每个单位**行动位**递减,调用点 `battle.c:7077`;阵亡单位被跳过 ⇒ 计时暂停。`全` 对宠技路径**不产生全体展开**(`BATTLE_MultiList` 对 0..19 恒产单体表)⇒ 数据描述"己方全体"与 mechanics 不符,照抄 mechanics。

**交付**:
- `shared/rules/Status.h`:`kPetSkillStatusPer=30` + `statusWorkOnApply`(turn+1、酒醉折半的单一真源)。
- `shared/rules/Combatant.h`:`pet_skill_apply_status` / `pet_skill_status_turns`(默认 0=无)。
- `shared/rules/Battle.cpp`:strike 改造 —— 行动级 `staus_change/staus_turn` 镜像(`battle.c:7096` 重置);**装备毒改兜底**(`== -1` 判据,每段命中重判,兜底不依赖伤害);技能状态走 `StatusChange(applied=true)` 事件 + `clearsCommandOnApply` 经 `ActionEffects.status_cleared_target` 回给调用方(同 `command_cleared` 先例,不上 IDL)。
- `src/world/World.cpp`:`MagicStatusState` + `magic_status_of_slot`(实例内部态,同 ChargeState 形状);`projectMagicStatus`(super_wall/other_status_nums 投影)/ `tickMagicStatus`(每回合行动循环前递减过期)/ `applyMagicStatusPetSkill`(施加落地);`applyEvents` 的 STATUS_CHANGE 分支按施加者投影参数算回合数;铁壁**不置** `pet_skill_direct`(它不是普攻管线)。
- 效果表扩展:`apply_status` / `status_turns` / `magic_status` / `magic_turns` / `magic_nums` 列;fixture 用真数据行。

**复验**:`ctest` **22/22**;`rules_battle` **134 例 / 2,919 断言**(+7 用例);`world_tick` **138 例 / 2,490 断言**(+6 用例);`ci_verify.py` **6 项全过**。★ **反向验证三处**(ZCode 执行、AutoCoder 抽查注入面):① 投影断开 ⇒ World `宠技B3*` 4 用例 12 断言红;② 遮蔽判据删除 ⇒ L3 遮蔽用例 2 断言红;③ 铁壁永不过期 ⇒ 过期断言 2 处红。全部还原复绿。⚠️ 过程纠偏(ZCode 如实记):第一轮注入发现自建的 `player_sequence=100` 参数误落到第 7 位(suit_poison 位),污染对照基线 —— 改为显式具名实参后重建干净基线再重做注入。**具名实参不是风格,是防"位置错位污染基线"的纪律。**

**登记残缺**:① 铁壁过期时点有意缩小:原版在每单位**行动位**递减、阵亡暂停;本实现每回合行动循环前统一递减一次(早半回合过期;且对在场有状态者一律递减 —— 我们的宠物槽不产指令,照原版口径铁壁会永不过期,两害取轻);② 铁壁不上事件(原版本就无事件,消费面走既有 `mods.super_wall`);③ 混乱(90)/催眠(110)与同路径行共用代码未单独立用例;④ 状态系宠技的 `PerOffset=30` 修正在 `15-status.md` 的登记口径之外,该文档残缺 ③ 的"30/1.0"仍指魔法/精灵路径,未冲突但**两套参数组并存**要读这里;⑤ 持久化未触及。

---

## 宠技域覆盖总览(截至本批)

| 类别 | 数据行 | 状态 |
|---|---|---|
| 直攻系(RENZOKU/CHARGE/MIGHTY/POWERBALANCE/GBREAK 系) | 17+60 | ✅ B1+B2 |
| 状态系(毒/石/乱/醉/眠)+ 铁壁 | 7 | ✅ B3 |
| 普攻/防御/守护(NormalAttack/NormalGuard/Guardian) | ~4 | 部分随 B1 表可表达,守卫 Guardian 未做 |
| 攻击魔法系(AttackMagic/Combined/SetMagicPet) | 36 | ⬜ 等 `__ATTACK_MAGIC` 咒术管线 |
| 状态 12..43 依赖 / 特殊 / 变身 / 召唤 / 偷窃等 | ~24 | ⬜ 各绑未移植子系统 |
| 敌方 AI 用技(ENEMYSKILL_* / _PRO_BATTLEENEMYSKILL) | 3 | ⬜ 模板槽已备,`fillEnemyCommands` 待扩展 |
