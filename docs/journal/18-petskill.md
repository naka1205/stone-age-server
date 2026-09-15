# journal / 18-petskill.md — 宠技域:批次 B1 直攻系 + B2 宠技槽与集气

> **本文件收录**:宠物技能域批次 —— B1 直攻系、B2 宠技槽与 CHARGE 集气。
>
> **批次编号**:§9.0.61 · §9.0.63(共 2 节;§9.0.62 推送窗口记录在 [`90-push-windows.md`](90-push-windows.md))
>
> ★ **编号沿用 `00-architecture.md` 原 §9.0.x 体系,搬家未改号** —— 全仓约 600 处 `§9.0.x` 引用因此继续有效。总映射见 [`README.md`](README.md)。
>
> ★ 本目录是**开发流程账**(逐批次的取证 / 交付 / 复验 / 教训)。
> 架构裁定看 [`../00-architecture.md`](../00-architecture.md);决策看 [`../11-decision-register.md`](../11-decision-register.md);
> 还欠什么看 [`../backlog/`](../backlog/);和原版哪里不一样看 [`../deviations/`](../deviations/)。

---

### 9.0.61 ★★ 批次 B1 —— 直攻系宠技:RENZOKU / GBREAK / GBREAK2 / MIGHTY / POWERBALANCE(2026-09-15)

**为什么是它**:欠债 1 的批次表(`00` §1.3.1)里 B 批次(宠技表,74 函数 / 1,920 行)是 A/D 之后最大的未做块。按 8.0 生产数据 `petskill2.txt`(147 行,69 个函数)实测分类后,**直攻系** 17 行是其中唯一"L3 快照输入面已齐、无未移植前置"的切片 —— 宠物入场机制(M.2/PET_OUT)、`CombatModifiers` 投影分工(I.4 先例)、攻击管线(批次 0.5)全部就位。⇒ B 批次第一批只做这一片,其余宠技(治疗/状态/召唤等)各自绑未移植链路,继续划出。

**交付**:
- `shared/rules/Combatant.h`:`CombatModifiers` 新增 7 个宠技投影字段(`pet_skill_direct` 门 / `pet_skill_hits` / `pet_skill_damage_percent` / `pet_skill_duck_bonus` / `pet_skill_guard_break` / `pet_skill_attack_percent` / `pet_skill_defense_percent`),**全部默认值 = 无技能**(非 PET_SKILL 指令 / 表外技能与 B1 之前逐位一致,不摇 rng)。字段注解含逐项源码锚点。
- `shared/rules/Battle.cpp`:指令分发处补 `PET_SKILL` 直攻系 case(**在 switch 里补,不在调用方拦** —— 遵守该处批次 0.5 注记的既有纪律);五个技能共用同一条 strike 管线,差异全由 `mods` 承载。关键语义(全部回 `StoneAge/gmsv/src/battle/` 逐处核实):
  - **RENZOKU**:段数是**覆盖**不是抽取(`battle.c:7263` 直接赋 `attack_max`,跳过 `rollAttackCount` 那笔 rng;宠物侧原版本来就不消费);每段伤害 = `(int)((float)D / N)`(**float 除后截断**),`≤0 抬回 1`(`battle_event.c:2723-2726`)—— ⚠️ 源码是 `if(damage<=0) damage=1` **不是** `max(0,·)` 顺序,负伤也抬 1;分摊只作用非反击段(`:3646` 反击链前 `gDamageDiv=1.0`)。
  - **GBREAK / GBREAK2 是两个不同 COM、语义不同**(与 05 文档口径有出入,已按源码):GBREAK **专打防御** —— 守方本回合 GUARD 且未混乱才落伤,否则 0 伤 MISS(`battle_event.c:4530-4544`);GBREAK2 **无条件落伤**,守方裸 COM==GUARD ⇒ ×1.3 否则 ×0.7(`:1699-1705`,判据**不含混乱**,与防御减伤那条不同)。两者都**不套防御减伤**(else-if 链被 `if(opt==GBREAK) ;;` 短路,连 `GuardAdjust` 的 rng 都不摇,`:1695`)。
  - **MIGHTY**:伤害 ×倍率在 `BATTLE_AttackSeq` **最末行**无条件执行(`battle_event.c:1781`)—— 晚于暴击、晚于破防分支、晚于防御减伤;「避N」是**守方回避率 +N**(`:857` `per += gBattleDuckModyfy`,per 是守方回避率;全路径无取负,数据描述「命中率下降」与源码一致)。
  - **POWERBALANCE**:指令时刻**替换式**改写有效攻/防(`WORKATTACKPOWER = FIXSTR + (int)(FIXSTR×攻%)`,`pet_skill.c:740-775`),不是叠加;L3 落在 `effectiveAttack/Defense` 的重算,供该回合所有消费者读。
  - **g\* 生命周期**:原版文件级变量每次行动前重置(`battle.c:7094-7095/:7139`)、攻击循环后与反击链前再重置(`:7791-7792`;`battle_event.c:3646`)⇒ 技能只作用于本行动者的普攻循环,反击不受影响 —— 本实现落在"逐指令投影 + 仅非反击段生效"。
- `src/world/World.cpp` / `world/Api.h`:`PetSkillEffect` 效果表 + `projectPetSkill`(每次行动前归零重投影,同 `projectItemUsePower`)。⚠️ **不是 petskill2.txt 的解析器**:表是注入式(同 I.4 `ItemEffect` 先例),真数据行由 D 线导入期解析 GBK 后注入;当前用例用真数据行做 fixture。
- **表外技能的门**:L3 的 PET_SKILL 分支只在 `pet_skill_direct` 为 true 时走攻击管线 —— 表外技能**整次行动跳过、不摇攻击取数**,**不能**静默退化成一次普攻(原版同义:查不到 petskill 函数即指令不成立)。

**复验**(本地 MSVC / `build\ci`,RelWithDebInfo + SA_WERROR):
- `ctest` **22/22**;`rules_battle` 124 → **127 例 / 2,879 断言**;`world_tick` 同步扩展;`ci_verify.py` **6 项全过**。
- ★ **反向验证三处**(AutoCoder 独立执行,与 ZCode 分离;每处还原后 bump mtime 重建):
  ① **RENZOKU 分摊抬下限失效**(去掉 `damage<=0 ⇒ 1`)⇒ `rules_battle` **4 断言转红**(`delta == -1`),还原回绿;
  ② **GBREAK2 系数翻转**(守方 GUARD ⇒ ×0.7 / 否则 ×1.3)⇒ **2 断言转红**(`g2_guard` / `g2_plain`),还原回绿;
  ③ **直攻门失效**(表外 PET_SKILL 退化成普攻)⇒ **3 断言转红**(两处 `events.size()` + `rng.calls()==2` —— 连 rng 消耗数都被钉住),还原回绿。
  - ⚠️ 过程注:第一版 RV① 注入"(int) 整除替换 float 除"**0 条转红** —— 正 int 域上 `(int)((float)a/b)` 与 `a/b` 截断结果一致,该注入对 fixture 无区分力 ⇒ 换成"去抬下限"注入后精确转红。**注入要先问"它改变了哪个可观察值"**(同 §9.0.36 ⑥ 那族教训第四次)。

---

### 9.0.63 ★★ 批次 B2 —— 宠技槽(B2a)+ CHARGE 集气(B2b)(2026-09-15)

**为什么是它**:B1 之后宠技仍缺两块地基 —— ① **技能属于哪只宠物**(`Model::Pet`/`Enemy` 无宠技槽 ⇒ 玩家无法"拥有"技能、捕获无法带技、入口无法校验持有);② **集气突击 CHARGE**(数据里 60 行,唯一跨回合的直攻系技能)。B2 一次补齐两者。

**★★ 数据面校正(本批最要紧的取证,推翻了派单简报的一处口径)**:
- **宠技槽在 `enemybase1.txt` 的 0 基 25..31 列,不是 18..24**。推导链:`include/enemy.h` 枚举 `E_T_TEMPNO=0` 起、`E_T_PETSKILL1=19`;载入器 `enemy.c:311-332`:字符列 `E_T_DATACHARNUM=6`(NAME + 5 个 ATOMFIXNAME)从文件第 1 列起,整型从**1 基第 `DATACHARNUM+1=7` 列**起映射枚举 0 ⇒ **0 基文件列 = 枚举 + 6** ⇒ PETSKILL1..7 = **c25..c31**。派单简报的 18..24 实为 WINDAT(c18)+ 六种状态抗性(c19..c24)——判据:① 乌力行 c15..18 = {80,20,0,0} 正是 M.4b 已验证的 地80/水20(EARTHAT..WINDAT = c15..c18);② c25..31 的值分布(1/2/3/10/11/12/13/20/30/40/41/50/150/152/644/645/-1)与 petskill2.txt 的 id 谱系逐个对上,18..24 的分布(0/10/30/50/100/1000/-100)对不上;③ "死引用"也在宠技列上:644/645 各 81 槽,不在 petskill2.txt ⇒ 原版 `PETSKILL_GetArray` 返 -1、指令不成立(空字符串列同 0,载入器 `strlen` 跳过)。
- 真实数据:乌力 c25=1(普攻)其余空;**CHARGE(id 30)共 60 行**、背水(50)6 行;fixture 采用真值形状。

**交付**:
- **B2a 宠技槽**:`shared/model/Enemy.h` / `Pet.h` 各加 `pet_skills[7]`(`kPetSkillSlots = CHAR_MAXPETSKILLHAVE=7`);`EnemyTemplate` 加 7 列并在 `spawnEnemy` 整组拷(`enemy.c:1204-1206`);捕获的 `createPetFromCapture` 七槽全拷(`pet.c:375-377`);`givePetToPlayer` 注入 seam(测试用)。
- **B2b CHARGE 三拍状态机**(跨回合态在 `BattleInstance::charge_of_slot[slot]`,世界侧;不上线协议,意图回写走 L3 `ActionEffects`):
  - **第一拍**(指令回合,新发 CHARGE):L3 在行动位产 charge_beat 回写 ⇒ World 置 `{beats=N-1, percent=P, target}`;静默、不摇攻击 rng。
  - **续拍**:`injectChargeCommands` 给集气中槽位注入合成指令 ⇒ L3 静默(不摇 rng,`rng.calls()==0` 有断言)⇒ World 减一。原版菜单置灰(`battle_command.c:945`)的对应物是**集气中丢弃来令**(`onBattleCommand` 就地记明:真实数据 N=1,至多影响两回合;指令面拆宠物通道后应收窄)。
  - **完成击**(beats==0):注入合成 `PET_SKILL{skill_id,target}`、`projectChargeState` 置 `charge_ready=true`(守方**一律不可回避**,rollDodge 第一道)+ `attack_percent=P`(复用 POWERBALANCE 的 `effectiveAttack` **替换式**落点:有效攻 = str + str×P%);击后 `charge_strike` 回写清态(原 `battle.c:7729` 击后 COM1=NONE)。
  - 源码锚点:`PETSKILL_ChargeAttack`(`pet_skill.c:614-640`,N 归一 `N<1||N>10⇒1`、option `N 攻%+P`)· `BATTLE_Charge`(`battle_event.c:5027-5064`:low>0 减一 NoAction;low==0 时 `pow += pow*N*0.01`(N=COM3 high)、WORKATTACKPOWER 替换、COM1=S_CHARGE_OK)· `battle.c:7259`(每次行动前 S_CHARGE ⇒ BATTLE_Charge)· `:7510`(S_CHARGE_OK 落普攻执行组 fall-through)· `:668`(集气中 BATTLE_IsCharge 豁免)。
- **入口持有门**(`onBattleCommand`,W| 分支对应物):PET_SKILL 指令接收时校验 —— 主人(`slot_of` 映射)有 `default_pet`、`iNum < 7`、宠物七槽上 `pet_skills[iNum] == skill_id`、宠物侧状态门;任一不过 ⇒ **整批降级 WAIT**(不产事件、不摇 rng,与 I| 同款;`rng.calls()` 有断言)。原版发起者=主人、操作对象=默认宠(`battle_command.c:277-278`)。

**复验**(本地 MSVC / `build\ci`):
- `ctest` **22/22**;`rules_battle` **127 例 / 2,879 断言**(+3);`world_tick` **132 例 / 2,391 断言**(+5:CHARGE 三拍 / 持有门三向 / 模板拷贝 / 捕获七槽 / 敌人只普攻);`ci_verify.py` **6 项全过**。
- ★ **反向验证四处**(ZCode 执行 4 处,AutoCoder 独立抽查确认注入面与残留 = 0):
  ① 捕获不拷七槽 ⇒ 7 断言红;② 完成击攻%替换失效 ⇒ 2 断言红(命中"按原 attack 打"的排除值 1433);③ 持有门放行 ⇒ 无宠 subcase 红(RENZOKU 漏进来打了);④ 蓄力回合摇攻击 rng ⇒ `rng.calls()==0` 红。全部还原回绿。

**有意不复刻**(就地记明):① CHAR_SLOT 转生门(转生域未移植);② id 600 狼人特判(petskill2.txt 实测无 600,投产数据不可达);③ 集气中来令以"丢弃"表意(原版菜单置灰);④ StatusSeq 清指令 ⇒ 集气夭折**照抄**(原 :5440 无豁免);⑤ 完成击目标已亡时集气态仍被消费(判据点是"行动位到达");⑥ 宠物存档未含 pet_skills(持久化域)。

**登记残缺**:① 敌方 AI 使用宠技(ENEMYSKILL_* / `_PRO_BATTLEENEMYSKILL`)—— `fillEnemyCommands` 仍只填普攻,模板宠技槽已备;② 客户端宠技 UI 与 `W|` 发送;③ D 线 `petskill2.txt` GBK 导入器(注入式表 → 真数据表);④ 状态系宠技(依赖 L4 状态 12..43)、攻击魔法系(依赖 `__ATTACK_MAGIC`)、Guardian/特殊系照旧划出;⑤ 「攻方集气完成 ⇒ 守方不可回避」门①在原始 8.5 树 `BATTLE_DuckCheck`(:763-805)未见对应分支,本实现沿用仓库 spec(05 §3.2,批次 0 已落地的既有消费点)——证据边界见 `combatant.h` charge_ready 注记。
