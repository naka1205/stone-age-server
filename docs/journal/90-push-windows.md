# journal / 90-push-windows — 推送窗口执行记录

> **本文件收录**:每次 `shared-v0.x.0` 前推的闭合核实:两仓 × 两远端 SHA 与 tag 集合差、CI 三平台、客户端发布态 FetchContent 日志正文。★ 与批次记录分开存放,因为它们答的是**同一个问题的九次重复**。
>
> **批次编号**:§9.0.32 · §9.0.34 · §9.0.38 · §9.0.41 · §9.0.43 · §9.0.47 · §9.0.49 · §9.0.52 · §9.0.54(共 9 节,§9.0.32 – §9.0.54 区间内;§9.0.58 起的窗口记录随批次写入各功能文件)
>
> ★ **编号沿用 `00-architecture.md` 原 §9.0.x 体系,搬家未改号** —— 全仓约 600 处 `§9.0.x` 引用因此继续有效。总映射见 [`README.md`](README.md)。
>
> ★ 本目录是**开发流程账**(逐批次的取证 / 交付 / 复验 / 教训),原文逐字迁入、未改写。
> 架构裁定看 [`../00-architecture.md`](../00-architecture.md);决策看 [`../11-decision-register.md`](../11-decision-register.md);
> 还欠什么看 [`../backlog/`](../backlog/);和原版哪里不一样看 [`../deviations/`](../deviations/)。

---

### 9.0.32 ★ 推送窗口执行记录 —— `shared-v0.12.0`(2026-09-08)

> **一个窗口、一个 tag 盖两批**(欠债 24 的格式守卫 + M.4a 四维公式)。
> 按「服务端提交 → 客户端复验 → 前推 ref → 两仓一起推送」这个**整体动作**执行。

#### ① 结果

| 项 | 值 |
|---|---|
| server master / tag | `2ce10ad` · `shared-v0.12.0`(附注)—— **gitee + github 双远端 `ls-remote` 核实一致** |
| client master | `a865410`(pin v0.11.0 → v0.12.0)—— 双远端核实一致 |
| client `ci_verify` | **八项全过**(★ 换 pin 前「shared/ 与锁定 ref 一致」是红的,那正是它该有的行为;★ 已先 `rm -rf build/ci build/d2-only`,否则 CACHE 把 pin 钉在旧值、验的是旧 ref) |
| server CI | **run #26 三平台(macOS Apple clang / Linux GCC / Windows MSVC)全 success**;日志逐项:14 条全部注册 · ctest 14/14 · 断言防线反向验证通过 · 6 项全过 |
| client CI | ✅ **run #18(`a865410`)三平台全 success**(~~⚠️ 未核 —— private 仓匿名 404~~ ⇒ **当日用 VSCode 的 OAuth token 补核,见 ④**);★ 日志证实走的是**发布态**、锁定 ref = `shared-v0.12.0`、8 项全过 |

#### ② ★★ CI 那个绿**证明了什么** —— 靠结构推出来,不是看到绿灯

**证明了**(靠的是守卫的结构,不是看到绿灯):
`code_format` 在 `EXPECTED_TESTS` 里,而 §2 对缺项是**硬失败** ⇒ 三平台 verify 步骤全绿
⇒ **`code_format` 在三平台都注册且都通过**;连带 GCC 与 MSVC 以 `-Werror` / `/W4 /permissive-`
编了新增的 `rollSpawnStats`(含 `double`/`float` 收窄路径)**0 告警**,
`rules_progression` 10 例 / 68 断言在三平台都过。

⚠️ ~~**没证明**:三个 runner 实际用的是哪个 clang-format 二进制~~
⇒ ✅ **当日已核实,本条作废**(见 ④)。

#### ③ (并入 ②)

★ 原 ③ 的内容(「没证明」那一半)已因 ④ 的补核作废,合并进 ② 的删除线里;
编号保留空位,免得后文对 §9.0.32 各条的引用错位。

#### ④ ✅ 用用户 VSCode 的 OAuth token 补核 —— 两处「未核」全部关闭(当日)

⚠️★ **① 与 ② 里写的「匿名 403 / 404 ⇒ 读不到」只对匿名成立**,不是"无法核实"。
用户 2026-09-08 指出 VSCode 已授权 ⇒ `git credential fill`(host=github.com)取出一个
`gho_` OAuth token(scopes:`repo` · `workflow` · `read:user` · `user:email`)
⇒ **private 仓 CI 与 job 日志正文都能读**(日志 HTTP 由 403 变 200)。
★ **教训**:「取不到」要说清是**匿名取不到**还是**取不到** —— 前者是凭据问题,后者才是能力边界。

| 原「未核」项 | 补核结果 |
|---|---|
| **client CI** | ✅ **run #18(`a865410`)三平台全 success** —— 且日志逐项读出:★ **`发布态(FetchContent + 锁定 ref)`** · 锁定 ref = **`shared-v0.12.0`** · `shared/ 与锁定 ref 一致` ✅ · **5 条全部注册**(含 `code_format`)· `ctest` 5/5 · 黄金用例集 **75 用例 / 2,403 断言** · **全部 8 项通过**。★★ **发布态这一半是本地 `ci_verify` 覆盖不到的**(本地走联调态)⇒ 这才是 DR-TS3 裁定形态的真正实证 |
| ★★ **runner 实际用的 clang-format** | ✅ **六个 job(两仓 × 三平台)全部实测 `clang-format version 21.1.8`** —— **钉的版本真的生效了**,§9.0.30 ③ 那个 `HINTS`→`PATHS` 的修正在三平台都兑现 |
| server CI 逐项 | ✅ 三平台日志读出:**14 条全部注册** · `ctest` 14/14 · ★ **断言防线反向验证**「assert 确实触发并使测试失败」· **全部 6 项通过** |

⚠️★ **仍剩一处窄残余,别当已关**:上表证明的是「PATH 上那个是 21.1.8」(来自 `clang-format --version` 步骤),
**不是**「CMake 的 `find_program` 挑中的就是它」—— `code_format` 通过时 `ctest --output-on-failure`
**不打印测试 stdout**,所以脚本自己报的那句「扫描 N 个文件,<版本>」在日志里**是空的**。
★ 逻辑上那一环是成立的(`PATHS` 在系统 PATH 之后才搜,而 Linux/Windows runner 上那三个 `PATHS` 路径都不存在),
但它是**推理不是观测**。⇒ **便宜的关法**:在 `tests/CMakeLists.txt` 加一句
`message(STATUS "code_format 用的 clang-format = ${SA_CLANG_FORMAT}")`,
让配置期日志直接印出 CMake 挑中的那个二进制。**本窗口不做,记在此处。**
⚠️ 风险仍已量化:21.0.0 / 21.1.8 / 22.1.4 三版本对本 `.clang-format` 判定**逐位一致**(§9.0.30 ⑤)
⇒ 即使挑中别的版本,判定也不会不同。

#### ⑤ ⚠️★ 一处推送副作用:`--follow-tags` 顺带推了 `shared-v0.8.0`

`git push origin master --follow-tags` 会推**所有可从被推 ref 达到的附注 tag** ⇒
`shared-v0.8.0`(§9.0.25 记的「只建在本地、两远端都没有」那一个)**被一并推上两个远端**,
现指向 `48cc474`。
★ **功能上无影响**:没有任何 pin 指向它(客户端 pin 是 `shared-v0.12.0`);
它「**不可用于发布态**」这条性质不变(它指向重命名前的树,客户端 `#include "wire/Framing.h"` 在那份里硬错)。
⚠️ 但它**推翻了一条已写下的事实** ⇒ §9.0.25 的表格行已就地更正。
★ **处置 = 保留不删**,判据两条:① 没有任何东西引用它,删除是又一次外发动作而换不到功能收益;
② §9.0.22 ② 那条教训本身就是「**tag 只在本地是隐患**」(「打了 tag ≠ tag 在远端」)——
现在两边一致,反而消除了那个不对称。
⇒ ★ 若要恢复「从未推送」的原状,须显式删远端 tag;**本窗口不做,记在此处**。

---

### 9.0.34 ★ 推送窗口执行记录 —— `shared-v0.13.0`(2026-09-09)

> **一个窗口一个 tag,盖 M.4b 一批 + 两仓各一笔同源缺陷修复。**
> ★ 本窗口的执行顺序与前几次不同,是**先打本地 tag → 客户端换 pin 验全绿 → 才外发推送** ——
> tag 在本地时验不过可以 `git tag -d` 重来,推出去就只能再打一个;
> 外发动作放在最后一步,代价最小。

#### ① 结果

| 项 | 值 |
|---|---|
| server master / tag | `2bab618` · `shared-v0.13.0`(附注,peeled 指向 `2bab618`)—— **gitee + github 双远端 `ls-remote` 核实一致** |
| client master | `56e9ca0`(pin v0.12.0 → v0.13.0)—— 双远端核实一致;★ client 仓**无 tag**(`ls-remote --tags` = 0 行,tag 只在 server 仓) |
| server 本地复验 | `ci_verify` **六项全过** · `ctest` **14/14** · 14 条全部注册 · `SA_WERROR=ON` 清洁构建 0 告警 |
| client 本地复验 | `ci_verify` **八项全过**(★ 已先 `rm -rf build/ci build/d2-only`,否则 CACHE 把 pin 钉在旧值、验的是旧 ref);`d2_rules_progression` **13 例 / 84 断言** ← 与服务端**逐位相同** ⇒ D2 仍成立(含新增 `enemyRank`) |
| server CI | ✅ **run #28(`2bab6187`)三平台全 success**;日志逐项:**14 条全部注册** · `ctest` 14/14 · `clang-format version 21.1.8` · 清洁构建 · **6 项全过**;★ **M.4b 的三个用例集(`world_tick`/`rules_progression`/`model_pool`)在 GCC 与 MSVC 下都注册并通过** ⇒ §9.0.33 ⑧ 那条「GCC/MSVC 交 CI」到此兑现 |
| client CI | ✅ **run #19(`56e9ca04`)三平台全 success**;★ 日志证实走的是 **`发布态(FetchContent + 锁定 ref)`** · 锁定 ref = **`shared-v0.13.0`** · `shared/ 与锁定 ref 一致` ✅ · 5 条全部注册 · `ctest` 5/5 · **75 用例 / 2,403 断言** · **8 项全过** |

★ **发布态那一半仍是本地覆盖不到的**(本地走联调态,直接指向同级工作树)⇒
client CI #19 才是「`shared-v0.13.0` 真能从远端 fetch 下来并编译」的实证。

#### ② ★★ 本窗口演示的是那条判据的**另一面**:窗口内改动 ≠ watched 路径改动

前推判据是「**推送窗口内的全部 watched 路径**变没变」(§9.0.25 ①、`SaShared.cmake` §③)。
本窗口 `d7fdbcd..2bab618` 共 15 个文件,而落在 watched 路径上的**只有 4 个**:

```
shared/model/Enemy.h          ← 新,219 行
shared/model/Pet.h            ← Y 五项
shared/rules/Progression.h    ← enemyRank 声明
shared/rules/Progression.cpp  ← enemyRank 实现
⚠️ idl/generated/cpp/         ← 本窗口一个字没动
```

⚠️★ 同窗口另有 `tools/check_format.py` 的一处**真缺陷**修复(§9.0.33 ⑥ 的哨兵,`d02cd01`),
它**不在 watched 路径里** ⇒ **不因它前推**;客户端那份同源缺陷由 client 自己的
`b97842f` 单独修,**不经 `shared/` 传播**。
⇒ ★ 此前四次前推记的都是「窗口比这批大 ⇒ 该推而漏推」这一面(§9.0.25 ①),
本次记的是反过来的一面:**窗口里有改动,但不在 watched 路径上 ⇒ 不构成前推理由**。
两面合起来才是那条判据 —— 它问的既不是「我这批动了什么」,也不是「这个窗口动了什么」,
而是「**客户端真正编译的那份路径变了没有**」。

#### ③ ⚠️★★ 一处不能被「八项全过」盖掉的事实:`Enemy.h` 在客户端侧**根本没编**

实测(`grep -rn "model/Enemy.h\|model/Pet.h\|model/Player.h\|model/EntityPool.h" src/ tests/`):
**客户端仓没有任何 TU include `shared/model/` 下的头**,`d2` 闸门链的是
`sa_shared`(= `shared/rules` 那半)+ `sa_wire`。

⇒ 本窗口 D2 ②「一份源码两端都编得起来」的实证**只覆盖 `shared/rules/Progression`
(含 `enemyRank`)这一半**;`Enemy.h`(新)与 `Pet.h` 的 Y 五项**从未经过客户端工具链**。
★ 这不是新问题,与 M.1 时 model 头的情形同一条(§9.0.26 未做项),
且**不影响前推判据**(判据看的是 watched 路径变没变,不是「客户端编不编它」——
`shared/model/` 在 watched 路径里,所以漂移守卫照样会报、照样必须前推)。
⚠️ **但它影响你能从那个绿灯里读出什么**:client CI #19 的绿**不能**用来说
「`Enemy.h` 在 MSVC 下也编得过」。★ 这两句话的区别,正是欠债 20 那一族的形状
(「地基绿而运行时不接」)在**编译面**上的同一形状:**头在仓里、tag 里、守卫盯着,但没有一个 TU 碰它。**
⇒ 已写进 client `56e9ca0` 的提交正文,不只记在这里。

#### ④ ★ `--follow-tags` 这次**没有**顺带推上任何东西 —— §9.0.32 ⑤ 的正面兑现

§9.0.32 ⑤ 记的教训是「`--follow-tags` 会推所有可达的附注 tag,推完要看**还顺带上去了什么**」。
本次照做,判据取**集合差**而不是「我的 tag 在不在」:

```
comm -3 <(git tag -l | sort) <(git ls-remote --tags origin | grep -oE 'shared-v[0-9.]+$' | sort -u)
⇒ 输出为空:本地与远端各 13 个 tag(v0.1.0 … v0.13.0),完全一致
```

★ 差集为空这件事本身说明上次那个不对称(v0.8.0 只在本地)**已经消失** ——
这次没有可推而未推的 tag,也没有意外新增。
⇒ **教训的落地形态**:核 tag 用**差集**,不用「我要的那个在不在」;
后者答不了「还上去了什么」这个问题,而那才是上次踩的坑。

#### ⑤ ⚠️★★ 一处本次现场教训:解析失败被读成了「还没跑完」

轮询 server CI 时用 `json.load()` 读 GitHub API 的响应,而 **run 列表里的
commit message 含控制字符 ⇒ `json.load` 默认 `strict=True` 直接抛 `JSONDecodeError`**
(`Invalid control character`)。轮询循环长这样:

```bash
ST=$(curl … | python3 -c "…json.load(sys.stdin)…")   # ← 抛异常,ST 变成空串
case "$ST" in completed*) break;; esac                # ← 空串永不匹配 ⇒ 循环跑满
```

⇒ **空转了整整 8 分钟**,每一趟都在印 traceback,而循环的行为(继续等)
与「任务确实还在跑」**一模一样**。⚠️ 处置是 `json.load(f, strict=False)`,
但那只是这一次的修法;**真正的教训是判据形状**:

★★ **等待循环必须先断言「取到的值非空且在预期取值集合里」,再判断它等不等于终态** ——
否则**任何**取值失败(网络错、鉴权过期、解析崩)都会伪装成「还没完成」,
而这两者的正确处置**方向相反**(一个该重试,一个该停下来修)。
⇒ 这是 §9.0.12 那一族的又一种形态:前面几种是「把红读成绿」,
这一种是「**把错误读成未完成**」—— 同样是**判据的取值域没有被约束住**。

#### ⑥ ⚠️ 残余(逐条实测,不是转述)

| 项 | 状态 |
|---|---|
| §9.0.32 ④ 那条窄残余 | ⬜ **仍在**。实测 `tests/CMakeLists.txt` 里只有 `find_program` / `if(SA_CLANG_FORMAT)` / `ENVIRONMENT` / 报错文本,**没有** `message(STATUS … ${SA_CLANG_FORMAT})` ⇒ 「PATH 上那个是 21.1.8」已六次实测(两仓 × 三平台),但「`find_program` 挑中的就是它」**仍是推理不是观测**。★ 风险已量化(三版本判定逐位一致)⇒ 不紧急,但**别把它当已关** |
| `shared-v0.8.0` | 保留不删(§9.0.32 ⑤ 的处置不变;本窗口未动它) |
| M.4b 自身的未做项 | 见 §9.0.33 ⑧,**推送不改变其中任何一条**(遇敌表 `enemy.txt` · 一般宠 `PET_createPet` · `ENEMY_STYLE` 敌人武器 · demo 不接真实模板 · 敌人池预算按族切分) |

---

### 9.0.38 ★ 推送窗口执行记录 —— `shared-v0.14.0`(2026-09-09)

> **一个窗口一个 tag,盖三批(M.5 / R.1 / M.6)。**
> ★ 前推理由只来自三批中的一笔,而本窗口最值钱的收获在客户端的 watched 路径上。
> 执行顺序同 §9.0.34:**先打本地 tag → 客户端换 pin 验全绿 → 才外发推送**。

#### ① 结果

| 项 | 值 |
|---|---|
| server master / tag | `ade1c8e` · `shared-v0.14.0`(附注,指向 `ade1c8e`)—— **gitee + github 双远端 `ls-remote` 核实一致**(集合差,非「我要的那个在不在」)· master 三处同 SHA |
| client master | `d2af1d9`(pin v0.13.0 → v0.14.0)—— 双远端核实一致;★ client 仓**无 tag** |
| server 本地复验 | `ci_verify` **六项全过** · `ctest` **15/15** · 15 条全部注册 · `SA_WERROR=ON` 清洁构建 0 告警(`build/push14`,全新目录) |
| client 联调态复验 | `ci_verify` **八项全过** · `ctest` 5/5 · 黄金用例集 **76 用例 / 2,415 断言** · 漂移守卫在全五条 watched 路径判一致(`build/push14`,全新目录) |
| server CI | run **#30**,三平台 job 全 success,日志逐项核验:`ctest` 15/15 · 15 条全注册 · 六项全过 · 断言防线反向验证过(macOS·clang / Linux·GCC / Windows·MSVC) |
| client CI | run **#20**,三平台 d2-gate job 全 success,日志逐项核验:**发布态**· 锁定 ref `shared-v0.14.0` · 76 用例 / 2,415 断言 · `ctest` 5/5 · 清洁构建 0 告警 |

⚠️ 待拍板 0 条。**本窗口后:欠债 26 关闭**(见 ⑦)。

#### ② ★★ 前推理由只来自三批中的一笔 —— 判据的又一次求值

| 提交 | 改动面 | 构成前推理由? |
|---|---|:-:|
| M.5 `b89ee4e` | 全在 `src/world/` | ❌ |
| **R.1 `7b8d5b6`** | **`shared/rules/RandomSource.h`** + 两份用例集 + 新增 `tests/support/` | ✅ |
| M.6 `ade1c8e` | 全在 `src/world/` | ❌ |

★ 三个原因并列为同一条判据的多次求值(客户端 `01` §12.13/§12.16):
§12.13 纯本仓 `src/` ⇒ 不前推 · §12.11 兄弟目录变了 ⇒ 要前推 · **本窗口对「窗口内全部
watched 路径」求值 ⇒ 只因 R.1 一笔前推**。⚠️ 不判「我这批动了什么」,别把三批混记成一笔。

#### ③ ★ R.1 的改动面精确求值

`git diff 9d570cc..ade1c8e -- shared idl/generated/cpp tests/RulesBattleTest.cpp tests/RulesProgressionTest.cpp`:

| 路径 | 改动 |
|---|---|
| `shared/rules/RandomSource.h` | +30/−8(退化区间语义,DR-BT23 §9.0.36) |
| `tests/RulesBattleTest.cpp` | +120/−**(DR-BT23 断言组)** |
| `tests/RulesProgressionTest.cpp` | −51(本地 rng 存根删除) |
| `tests/support/ScriptedRandom.h`(新) | +71(三份拷贝合并为唯一一份) |

★ `idl/generated/cpp` **本窗口未动**。

#### ④ ⚠️★★ 本窗口最值得记的一处:客户端的 watched 路径清单**落后于编译面**

客户端 `cmake/SaShared.cmake` 的 `SA_SHARED_WATCHED_PATHS` 原只有四条
(`shared` / `idl/generated/cpp` / 两份用例集)。⚠️ R.1 把两份用例集各自一份的 rng 存根
收敛成唯一一份 `tests/support/ScriptedRandom.h`,两份用例集都 `#include "support/ScriptedRandom.h"`
(`RulesBattleTest.cpp:30` · `RulesProgressionTest.cpp:17`)⇒ **客户端从那一刻起就真编译它了**,
而清单没有它。

★ **本窗口它不致静默** —— 该文件是**新增**的,旧 ref 里没有 ⇒ 停在旧 ref 的症状是
**硬错**(找不到头文件)。
⚠️★★ **但下一次「只改它内容」的那一批会静默**:`shared/` 没变、两份用例集没变,
漂移守卫看不见 `tests/support` ⇒ 发布态照旧编旧存根、照旧八项全过。

⇒ 与 §9.0.34 ⑤ 记的那条「若有人把 watch 收窄成只看 `shared`」是**同一失效的另一半**:
那半是**主动收窄**,这半是**没人扩宽** —— ★★ **判据本身没错,错的是清单本质上是
手工维护的、而编译面是自动扩张的**(用例集多 include 一个目录,编译面就扩了)。
⇒ 已加,并把纪律写在清单旁:**此后新增任何被用例集 include 的服务端侧目录,同一笔就要进这个列表。**
★ 连带实证:该清单同时喂给 `check_engine_isolation.py` ⇒ 隔离检查扫描面实测 **29 → 30 个文件**
(与其它批次「扫描面自动增长」的机制相同,这次是靠主动加而非自动扩)。

#### ⑤ 客户端侧动作

`d2af1d9`,两笔改动 + 一份文档:
① `cmake/SaShared.cmake`:pin v0.13.0 → **v0.14.0**;watched 列表加 `tests/support`;
两段注释更新(前推判据、为什么停在旧 ref 危险)。
② `docs/01-client-architecture.md`:**新增 §12.16** + 变更记录一行;★ 顺带修 §13.1 表的结构缺陷
(末尾 5 条游离到 `## 14` 之后 + 表内一个空行把表截成两段),22 条按日期重排回同一张表,
判据是「日期单调 + `## 14` 之后 0 条游离」。⚠️★ **§12.14 ③ 修过同一处 ⇒ 第二次发作** ——
同「文档漂移没有守卫」那族;这次不为它加守卫(成本不划算),把判据记在 §12.16 ⑥ 里。

⚠️★ 两处都要说清:
§12.16 讲的是**客户端侧**的视图(前推理由、watched 清单缺口、复验),本窗口的服务端侧原委
(DR-BT23、为什么停在旧 ref 危险)在 §9.0.36 与 §9.0.38 ④;
★ **clang-format 只扫 `.h/.hpp/.cpp/.cc/.inl` 五类,文档改动不在扫描面** ⇒ 不触发 `code_format`。

#### ⑥ 复验 —— ★ 发布态三平台逐项核验,不凭绿灯结案

客户端 `d2-gate` 三平台日志逐项取核验(**发布态 vs 联调态**,不是「本地绿」)——
⚠️ 发布态从 **GitHub 镜像**取锁定 ref ⇒ 本窗口能验到它,前提是 §① 的 tag 已同时存在两个远端:

| 平台 | 接入模式 | 锁定 ref | 用例/断言 | ctest | 清洁构建 | 测试注册 |
|---|---|---|---|---|---|---|
| macOS·clang | `fetch`(发布态) | `shared-v0.14.0` | 76 / 2,415 | 5/5 | 0 告警 | 5 条全在 |
| Linux·GCC | `fetch` | `shared-v0.14.0` | 76 / 2,415 | 5/5 | 0 告警 | 5 条全在 |
| Windows·MSVC | `fetch` | `shared-v0.14.0` | 76 / 2,415 | 5/5 | 0 告警 | 5 条全在 |

★ 服务端三平台同样逐项核验(六项全过 · 15/15 · 15 条全注册 · 断言防线反向验证过)。
⚠️★ **本窗口的关键兑现:M.5 / M.6 的 `src/world/` 新代码首次在 GCC 与 MSVC 上编译通过**
—— 它们此前只在 Apple clang 21 跑过(§9.0.35 ⑨ / §9.0.37 ⑧ 记的「交 CI」)。

#### ⑦ ⚠️ 未做 / 边界

| 项 | 说明 |
|---|---|
| ✅ **欠债 26 关闭** | 推送窗口闭合:tag 已推两远端、客户端 pin 已前推、发布态三平台全绿并取日志核验。⚠️ 关闭判据是 **§① 的三平台逐项 + §⑥ 的发布态**,不是「客户端联调态绿」(联调态绿不能替发布态作证) |
| ⬜ 客户端仓可见性 | 本仓为 private,CI 的 `d2-gate` 从 public 的 server 镜像取 ref ⇒ **能验到**(与 §9.0.10 那处「只有拉方需要匿名可读」呼应)。⚠️ 若 server 仓改回 private,须同时落地只读 PAT secret + `url.insteadOf` |
| ⬜ 一次旁注 | `--follow-tags` 本次**没有**顺带推上别的(§9.0.34 ④ 的正面兑现)—— 推送输出只有一行 `[new tag] shared-v0.14.0`,集合差推前推后都空 |

---

### 9.0.41 ★ 推送窗口执行记录 —— shared-v0.15.0(2026-09-09)

战果结算批次(§9.0.40)的推送窗口,与本地已 ahead 的 M.7 + §9.0.38 记录一起上(用户批准)。

**执行**(§9.0.34 顺序:打本地 tag → 客户端换 pin 联调态验全绿 → 才外发):
- server:`git push origin master`(`ade1c8e..004bbe0`,双 push URL gitee+github)+ tag `shared-v0.15.0`(指向 `004bbe0`)
- client:pin `v0.14.0` → `v0.15.0`(`fc846f9`),双远端推送

**核实**(不凭绿灯,取日志逐项 —— §9.0.10 纪律):
- 两仓 × 两远端 master SHA **三处一致**(server `004bbe0` · client `fc846f9`);★ **tag 集合差为空**(本地 vs gitee,§9.0.34 纪律:核的是「还顺带上去了什么」—— 显式推单个 tag,没顺带)
- **server CI #31 三平台 success**:SA_WERROR 清洁构建 **0 告警** —— ★ GCC 编 `enemyExp` 浮点公式无告警 ⇒ `-ffp-contract=off` 在 world 生效;15 条测试全注册、`world_tick`/`dr_table` 通过
- **client CI #21 三平台 success**:★★ **发布态 `fetch` 锁定 ref = `shared-v0.15.0`**(76 用例 / 2415 断言)—— 本地联调态覆盖不到的那半兑现;★ 三平台含 MSVC ⇒ `BattleResult` 生成物在最严的编译器下也过
- ⇒ 欠债 **26**(v0.14.0,状态此前滞后)+ 欠债 **27**(v0.15.0)一并闭合

⚠️ 本记录 commit 本地 ahead 1,留下批一起推(同 §9.0.38 `be8af6b` 模式:推送执行记录总在推送**之后**才写)。

---

### 9.0.43 ★ 推送窗口执行记录 —— shared-v0.16.0(2026-09-09)

W.1 移动系统(§9.0.42)的推送窗口,与本地已 ahead 的 §9.0.41 执行记录 commit(`27da376`)一起上
(用户两段批准:先定「先推 `shared-v0.16.0`」,本地准备全绿后再确认外发)。

**执行**(§9.0.34 顺序:打本地 tag → 客户端换 pin 联调态验全绿 → 才外发):
- server:`git push origin master`(`004bbe0..82c9922`,双 push URL gitee+github,含 `27da376` docs + `82c9922` W.1)
  + tag `shared-v0.16.0`(指向 `82c9922`,★ **显式推单个 tag**,不用 `--follow-tags` 免顺带别的 —— §9.0.34 那条坑)
- client:pin `v0.15.0` → `v0.16.0`(`6a3d364`),双远端推送

**核实**(不凭绿灯,取实据 —— §9.0.10 纪律):
- 两仓 × 两远端 master SHA **三处一致**(server `82c9922` · client `6a3d364`);★ **tag 集合差为空**
  (本地 `git tag -l` vs gitee/github `ls-remote`,§9.0.34 纪律:核的是「还顺带上去了什么」—— 只推了 v0.16.0,无顺带无遗漏)
- client 换 pin 后**删 `build/ci` 重跑** `ci_verify`(⚠️ `SA_SHARED_GIT_TAG` 是 CACHE 变量,陈旧目录会把它钉在旧值上,
  「锁定 ref 与源码一致」守卫会红)⇒ 联调态 **8 项全过**:锁定 ref = v0.16.0 与源码一致、`SA_CLIENT_WERROR` **0 告警**、
  **76 用例 / 2415 断言**(与 v0.15.0 同 —— W.1 未改 `shared/rules`,黄金用例集不变)
- **server CI(`82c9922`)三平台 jobs 逐个 success**:Windows·MSVC / macOS·Apple clang / Linux·GCC ⇒
  ★ **W.1 的 GCC/MSVC 首验通过**(§9.0.42 ⑦ 记「只在 Apple clang 21 跑过」)
- **client CI(`6a3d364`)success**:发布态 `fetch` 锁定 ref = v0.16.0
- ⚠️★ **一处窄边界要说清**:W.1 的两个新文件 `shared/model/Player.h`(位置字段)与
  `idl/generated/cpp/domain/world_map.sa.h` 的**跨平台编译由 server CI 三平台覆盖**(server 的 `world/`·`net/` 代码 include 它们);
  ⇒ **client 侧无任何 TU include 它们**(客户端地图表现层未做)⇒ **client CI 的绿灯读不出「这两文件在客户端工具链下编得过」**
  —— §1.1 那条「D2 覆盖面是按文件而非按目录」窄边界的又一例,**非 W.1 遗留风险**(它俩已由 server 三平台验过)。
- ⇒ **§9.0.42 ⑦ 的「锁定 ref 前推 `shared-v0.16.0`」待办兑现闭合**。

⚠️ 本记录 commit 本地 ahead 1,留下批一起推(同 §9.0.38 / §9.0.41 模式:推送执行记录总在推送**之后**才写)。

---

### 9.0.47 ★ 推送窗口执行记录 —— shared-v0.17.0 + shared-v0.18.0(补记,2026-09-10)

W.2+W.3(§9.0.45)与 W.5(§9.0.46)两批 2026-09-10 连续推进、连续推送,两个推送窗口的执行记录**都留到本次(道具域批次开工前)一起补** —— 同 §9.0.43「推送执行记录总在推送之后才写」的节奏,只是这次跨了两个窗口(master 上 `3920e31`(W.4)→`3808e32`(W.2+W.3)→`017f132`(W.5)三批相邻,中间无记录 commit)。⚠️★ **补记 ≠ 核实放宽**:凭据一律现取实据(§9.0.10),不凭「当时应该绿了」的记忆。

**窗口一 shared-v0.17.0(W.2+W.3,DR-DT18)**:server master → `3808e32` + tag(指向 `3808e32`);client pin `v0.16.0` → `v0.17.0`(`d16e79c`)。**server CI run #34 / client CI run #23 均 `success`**。

**窗口二 shared-v0.18.0(W.5,DR-DT19)**:server master → `017f132` + tag(指向 `017f132`);client pin `v0.17.0` → `v0.18.0`(`dae0168`)。**server CI run #35 / client CI run #24 均 `success`**。

**核实(取实据,§9.0.10)**:
- **两仓 × 两远端 master SHA 四处一致**:server gitee+github 均 `017f132` · client gitee+github 均 `dae0168`(`ls-remote`);★ **两仓 tag 集合差均为空**(`comm -3` 本地 vs 两远端 —— v0.17.0/v0.18.0 都上去了、无顺带无遗漏,§9.0.34 纪律)
- **发布态锁定 ref = v0.18.0**:本地实读 `client/cmake/SaShared.cmake:105` `SA_SHARED_GIT_TAG "shared-v0.18.0"`(配置实值,非记忆推断)
- ⚠️★ **凭据边界要说清**:本次核到 workflow run 级 `conclusion=success`(该 workflow = 三平台矩阵,§9.0.9)+ 双远端一致 + pin 实值;**发布态 `fetch` 的日志正文本次未逐字重读**(该路径 §9.0.32/§9.0.34 已首验并读过日志,本次凭 CI 绿 + pin 锁定值)—— 这是**补记而非首验**,核到窗口级即止,不谎称重读。
- ⚠️★ 顺带记一处**注释滞后**(本次未动):`SaShared.cmake:38` 说明注释仍写「当前锁定值 = 正式 tag `shared-v0.14.0`」,而 `:105` 实值已 v0.18.0 ⇒ 同「变更记录漏 W.5 行」族(说明文字滞后于配置实值、无守卫),留客户端仓下次动 pin 时一并修。

⇒ **§9.0.45 ⑤ / §9.0.46 ⑤ 的「锁定 ref 前推 `shared`」推送窗口待办双双兑现闭合**。

---

### 9.0.49 ★ 推送窗口执行记录 —— shared-v0.19.0(I.1 背包 L2 地基,2026-09-10)

I.1(§9.0.48)落 `shared/model/Item.h` 新增 + `Player.h` 改 ⇒ watched 路径变更 ⇒ 锁定 ref 前推。本次窗口含 server `234d4cc`(§9.0.47 文档清账,原 ahead 1)+ `655c02d`(I.1 本体)两 commit 一起推。

- **server**:master → `655c02d`(gitee+github 两远端一致)+ tag `shared-v0.19.0`(指向 `655c02d`);★ **tag 集合差两远端均空**(`comm -3` 本地 vs `ls-remote`,无顺带无遗漏)。
- **client**:pin `v0.18.0` → `v0.19.0`(`62a1ff1`),master 两远端一致;顺带修 `SaShared.cmake:38` 滞后注释(§9.0.47 ⚠️ 留的待办兑现,不再滞后于 `:105` 实值)。
- **复验**:server `ctest` 16/16 · `ci_verify` 六项;client `d2-only` 联调态构建 + `ctest` 5/5(引擎隔离 + code_format)。
- **CI**:**server CI #36 三平台全 `success`**(Linux·GCC / Windows·MSVC / macOS·Apple clang = I.1 的 GCC/MSVC 首验)· **client CI #25 三平台全 `success`**(D2 闸门 × 三平台)+ **发布态 FetchContent 日志核实**(取 Linux·GCC job 日志正文,§9.0.10):`锁定 ref: shared-v0.19.0` · `✅ ★★ 锁定 ref 与源码一致 —— shared-v0.19.0` ⇒ 本地联调态覆盖不到的那半(发布态从锁定 ref 拉源码)已绿。

⇒ **§9.0.48 ⚠️「锁定 ref 前推 `shared-v0.19.0`」推送窗口待办兑现闭合**。

---

### 9.0.52 ★ 推送窗口执行记录 —— shared-v0.20.0(I.2 捕获扣道具 + I.3 野怪掉落,2026-09-10)

I.2(§9.0.50)动 `shared/model/Enemy.h` + 新增 `shared/rules/CaptureItem.h`、I.3(§9.0.51)又动 `shared/model/Enemy.h` ⇒ 两批 watched 路径变更,**合一个窗口推**(2026-09-10 用户拍板「暂不单独推 v0.20.0、与掉落批一起推」的兑现)。本次窗口含 server `81075ee`(§9.0.49 v0.19 执行记录,原 ahead)+ `179969d`(I.2)+ `6a8384f`(I.3)三 commit 一起推。

- **server**:master `655c02d` → `6a8384f`(gitee+github 两远端一致)+ tag `shared-v0.20.0`(轻量 tag,指向 `6a8384f` = I.3);★ **tag 集合差两远端均空**(`comm -3` 本地 vs `ls-remote`,双端各 20 个 `shared-v*`,无顺带无遗漏);推 tag 用精确 tag 名(不 `--follow-tags`,免顺带别的 tag)。
- **client**:pin `v0.19.0` → `v0.20.0`(`9fcb992`,`SaShared.cmake` :38 注释 + :109 值共 3 处),master 两远端一致。
- **复验**:server `ctest` 16/16 · `ci_verify` 六项(I.2/I.3 落地时已本地过);client 发布态由 CI 覆盖(见下)。
- **CI**:**server CI #37 全 `success`**(Linux·GCC / Windows·MSVC / macOS·AppleClang + `client 联调 D2(FetchContent)` 集成 job)· **client CI #26 三平台全 `success`**(D2 闸门 × 三平台)+ **发布态 FetchContent 日志核实**(取 Linux·GCC job 日志正文,§9.0.10):`shared/(D2)= 发布态(FetchContent + 锁定 ref)` · `锁定 ref: shared-v0.20.0` · `HEAD: 6a8384f…` · `★ 锁定 ref 与源码一致 —— shared-v0.20.0` ⇒ 本地联调态覆盖不到的那半(发布态从锁定 ref 拉 gitee 源码)已绿。

⇒ **§9.0.50 / §9.0.51 ⚠️「锁定 ref 前推 `shared-v0.20.0`」推送窗口待办双双兑现闭合**;server `81075ee` ahead 一并清零。

---

### 9.0.54 ★ 推送窗口执行记录 —— shared-v0.21.0(I.4 使用道具,2026-09-11)

I.4(§9.0.53)动 `shared/model/Item.h` + `shared/rules/{Battle.cpp,Combatant.h}` ⇒ watched 路径变更。
本次窗口含 server `6e57229`(§9.0.52 推送记录,原 ahead)+ `1708cab`(I.4)两 commit。

- **server**:master `6a8384f` → `1708cab`(gitee+github 两远端一致)+ 轻量 tag `shared-v0.21.0`(指向 `1708cab`);
  ★ **tag 集合差两远端均空**(`comm -3` 本地 vs `ls-remote`,推送前本地 21 / 远端各 20、差恰为 `shared-v0.21.0`,
  推送后各 21、差空 ⇒ 无顺带无遗漏);推 tag 用精确 ref(`refs/tags/shared-v0.21.0`,不 `--follow-tags`)。
  ⓘ **计数口径提醒**:`ls-remote --tags` 不滤 `^{}` 解引用行时会数出 37 个(实为 20)——
  ★ 这正是「核 tag 用集合差不用计数」的另一个理由(§9.0.34 那条只说了「计数答不了顺带推了什么」)。
- **client**:pin `v0.20.0` → `v0.21.0`(`d89f675`,`SaShared.cmake` :38 注释 + :113 值,两处同批改),
  master 两远端一致。
- **★ 本批在推送前多做了一步(值得成为惯例)**:tag 打在本地、**尚可 `git tag -d` 重来**时,
  先用 **GCC 15.2 本地全量验**(`SA_WERROR=ON`:零告警 + `ctest` 16/16)⇒ 把「CI 的 Linux·GCC 红了
  只能再打一个 tag」这条风险提前出清。⚠️ MSVC 仍只能靠 CI(无本地 Windows)。
- **复验**:server `ctest` 16/16 · `ci_verify` 六项 · client `d2-only`(**联调态**)84 例 / 2493 断言全绿。
  ⚠️ 联调态编的是工作树那份,**不能替发布态作证**(`SaShared.cmake` 卷首自述)⇒ 发布态见下。
- **CI**:**server CI #38 三平台 jobs 逐个 `success`**(Windows·MSVC / Linux·GCC / macOS·AppleClang)
  = **I.4 的 MSVC 首验通过** · **client CI #27 三平台 `success`**(D2 闸门 × 三平台)+
  **发布态 FetchContent 日志正文核实**(取 Linux·GCC job,§9.0.10):
  `shared/(D2)= 发布态(FetchContent + 锁定 ref)` · `HEAD: 1708cab28e851dab…` ·
  `锁定 ref: shared-v0.21.0` · `✅ ★★ 锁定 ref 与源码一致 —— shared-v0.21.0`。
- **★★ 最该记的一条数字**:**84 例 / 2493 断言在三处逐位一致** —— 本地 server(clang)·
  本地 client `d2-only`(联调态)· CI 发布态(从 gitee 拉 `shared-v0.21.0` 源码重编)。
  ⇒ 本批**改变了 rng 消耗**(用一次恢复药多摇一次,`battle_magic.c:419`),属 R.1/DR-BT23 同族的
  「序列平移」类改动 —— 三处一致正是这类改动**唯一**能拿到的正确性证据(§0 第③层)。

⚠️★ **顺带勘误一处旧记(现取实据,非凭记忆)**:§9.0.52 写「server CI #37 全 success
(**三平台 + client-integration**)」—— 调 API 取 #37 的 jobs 列表实为
`['macOS · Apple clang', 'Linux · GCC', 'Windows · MSVC']`,**没有 client-integration job**
(#38 同样三个)。⇒ 那半句是写记录时想当然添的。★ 同族第 N 次:**凭据要现取,"应该有的东西"
不会因为写进文档就存在**(§9.0.10)。

⇒ **§9.0.53 / `01` §13 欠债 32 ⚠️「锁定 ref 前推 `shared-v0.21.0`」推送窗口待办兑现闭合**;
server `6e57229` ahead 一并清零,**两仓 vs 两远端 `0/0`**。
⇒ ★ **道具域(I.1–I.4)四批至此全部落地并全部推送完毕**。

---

### 9.0.60 ★ 推送窗口执行记录 —— `shared-v0.27.0`(2026-09-15)

> **一个窗口、一个 tag 盖三笔**:批次 I|(入口校验,§9.0.57)· 批次 A4 经济地基(§9.0.58)· Windows OpenSSL 根探测(§9.0.59)。

#### ① 结果

| 项 | 值 |
|---|---|
| server master / tag | `fde500d` · `shared-v0.27.0`(附注)—— **gitee(`origin`)与 github(`workercrew` 镜像)`ls-remote` 核实一致**(`df1ff64` tag 对象 → peel `fde500d`) |
| client master | `2d93a9d`(pin v0.26.0 → **v0.27.0**)—— gitee + github(workercrew)双远端核实一致 |
| server CI(workercrew) | [run 34931043755](https://github.com/workercrew/stone-age-server/actions/runs/34931043755):**Windows MSVC / Linux GCC / macOS Apple clang 全 success** + MySQL 8.4.8 + Redis 8.6.2 integration success |
| client CI(workercrew) | [run 34932589367](https://github.com/workercrew/stone-age-client/actions/runs/34932589367):D2 闸门三平台(Windows MSVC / macOS Apple clang / Linux GCC)**全 success**,发布态 fetch 锁定 ref = `shared-v0.27.0` |
| 本机独立发布态复验 | 干净克隆 gitee 客户端到**无同级服务端**的临时目录,`ci_verify --expect-mode fetch`:**8 项全过**(fetch 模式 · 锁定 ref = v0.27.0 · 与源码一致 · 7 条全部注册 · ctest 7/7 · **115 用例 / 2,763 断言**) |
| server 本机 ci_verify | **6 项全过**(22 条全部注册含 `gold_writes` · ctest 22/22 · SA_WERROR 清洁构建 0 告警 · 断言防线反向验证通过) |

#### ② 镜像拓扑变更(记账,非代码)

- **GitHub 镜像从 `naka1205/*` 迁到 `workercrew/*`**:`naka1205/stone-age-client` 已 404、`workercrew/stone-age-client` 2026-09-14 新建(先为空仓)。本窗口起两仓 CI 与镜像推送均在 `workercrew` 下(server push 权限实测 403 on naka1205 ⇒ 凭据属 workercrew)。⚠️ **两仓 `.github/workflows/ci.yml` 的 `REPO_URL=github.com/${{ github.repository_owner }}/stone-age-server` 依赖 repository_owner == workercrew ⇒ 已随迁自动成立**(客户端 CI 三平台取源成功即实证);`docs/audits/2026-09-12-*` 里的 `naka1205` 链接已失效,**历史审计链接不回改**(记此为证)。
- 首推时序遵守 §9.0.9:server(master+tag)先行、client 换 pin 后推,tag 两远端 `ls-remote` 各自核实。

#### ③ 教训

- **`code_format` 在本机客户端验证里"静默缺失"一次**:本机 PATH 无 clang-format ⇒ CMake 按"缺工具不注册"先例跳过注册,首跑注册清单 7 缺 1 硬失败 —— **守卫按设计工作了**(它就是为"少一条则绿色不完整"而立)。处置:本机 `pip install clang-format==21.1.8`(与 CI 同 pin)后复跑 7/7 全过。⇒ 与服务端 §9.0.30 同一条:CI 上必须有,接住"没有"的是 EXPECTED_TESTS,不是安装步骤。
### 9.0.62 ★ 推送窗口执行记录 —— `shared-v0.28.0`(2026-09-15)

> **一个窗口、一个 tag 盖两笔**:批次 B1 直攻系宠技(§9.0.61)。

#### ① 结果

| 项 | 值 |
|---|---|
| server master / tag | `6d9b6e5` · `shared-v0.28.0`(附注)—— gitee + github(workercrew)`ls-remote` 核实一致 |
| client master | `a241d39`(pin v0.27.0 → **v0.28.0**)—— 双远端一致 |
| server CI(workercrew) | run **completed/success**(`6d9b6e5`) |
| client CI(workercrew) | run **completed/success**(`a241d39`,D2 闸门三平台,发布态 fetch 锁定 ref = v0.28.0) |
| 本机独立发布态复验 | 干净克隆(无同级服务端)`ci_verify --expect-mode fetch`:**8 项全过**(fetch · v0.28.0 · 与源码一致 · 7 条全部注册 · ctest 7/7 · **124 用例 / 2,856 断言**) |
| server 本机 ci_verify | **6 项全过**(22 条全部注册 · ctest 22/22 · SA_WERROR 清洁构建 · 断言防线反向验证通过) |

#### ② 教训(本窗口唯一要紧的一条)

- **反向验证注入"要先问它改变了哪个可观察值"**:RV① 首版注入"(int) 整除替换 float 除"**0 条转红** —— 正 int 域上 `(int)((float)a/b)` 与 `a/b` 截断结果**逐位一致**,注入对 fixture 无区分力;换成"去掉 `≤0 抬 1`"注入后 4 断言精确转红。同 §9.0.36 ⑥「断言的形状」/ M.5 ⑤ 那族第四次:注入无区分力 ≠ 代码没缺口,**先证明注入本身有区分力,再采信"没有红"**。
### 9.0.64 ★ 推送窗口执行记录 —— `shared-v0.29.0`(2026-09-15)

> **一个窗口、一个 tag 盖两笔**:批次 B2 宠技槽 + CHARGE(§9.0.63)。

#### ① 结果

| 项 | 值 |
|---|---|
| server master / tag | `675a7b4` · `shared-v0.29.0`(附注)—— gitee + github(workercrew)`ls-remote` 核实一致 |
| client master | `766d591`(pin v0.28.0 → **v0.29.0**)—— 双远端一致 |
| server CI(workercrew) | run **completed/success**(`675a7b4`) |
| client CI(workercrew) | run **completed/success**(`766d591`,D2 闸门三平台,发布态 fetch 锁定 ref = v0.29.0) |
| 本机独立发布态复验 | 干净克隆(无同级服务端)`ci_verify --expect-mode fetch`:**8 项全过**(fetch · v0.29.0 · 与源码一致 · 7 条全部注册 · ctest 7/7 · **127 用例 / 2,879 断言**) |
| server 本机 ci_verify | **6 项全过**(22 条全部注册 · ctest 22/22) |

#### ② 教训

- **派单简报的列位口径错误在开工前被执行者用数据分布拦下**(宠技列 c25..c31 非 18..24;判据三条,见 §9.0.63)——「回源码核实」这条纪律同样适用于**派单人写进任务简报的事实**;简报是输入,不是规格,规格仍是源码与真数据。
### 9.0.66 ★ 推送窗口执行记录 —— `shared-v0.30.0`(2026-09-15)

> **一个窗口、一个 tag 盖两笔**:批次 B3 状态系宠技 + 铁壁(§9.0.65)。

#### ① 结果

| 项 | 值 |
|---|---|
| server master / tag | `621d61d` · `shared-v0.30.0`(附注)—— gitee + github(workercrew)`ls-remote` 核实一致 |
| client master | `8685a8d`(pin v0.29.0 → **v0.30.0**)—— 双远端一致 |
| server CI(workercrew) | run **completed/success**(`621d61d`) |
| client CI(workercrew) | run **completed/success**(`8685a8d`,D2 闸门三平台,发布态 fetch 锁定 ref = v0.30.0) |
| 本机独立发布态复验 | 干净克隆(无同级服务端)`ci_verify --expect-mode fetch`:**8 项全过**(fetch · v0.30.0 · 与源码一致 · 7 条全部注册 · ctest 7/7 · **134 用例 / 2,919 断言**) |
| server 本机 ci_verify | **6 项全过**(22 条全部注册 · ctest 22/22) |
### 9.0.69b ★ 推送窗口执行记录 —— `shared-v0.31.0`(2026-09-16,补记于 2026-09-17)

> **一个窗口、一个 tag 盖一笔**:批次 A-β d1 AttackSeq 尾补摇忠实重排(§9.0.68)。
>
> ⚠️★ **本条是补记**:窗口本身在 09-16 就已闭合(见 ①),但当时只更新了 roadmap 与 journal 索引,
> **漏了本文件的记录** —— 于是 roadmap 上留了一句「推送窗口待执行(gitee 凭据侧阻塞)」,
> 而远端实际上早就有了。⇒ 见 ② 的教训。

#### ① 结果(2026-09-17 逐项 `ls-remote` 复核)

| 项 | 值 |
|---|---|
| server master / tag | `fa979e0` · `shared-v0.31.0`(**附注** tag,subject「A-beta d1: AttackSeq tail shake faithful reorder + GBREAK wrapper zeroing (9.0.68)」)—— gitee + github(workercrew)`ls-remote` 核实一致 |
| client master | `6f68f4b`(pin v0.30.0 → **v0.31.0**,`cmake/SaShared.cmake:119`)—— 双远端一致 |
| 服务端工作树 | 干净(本批 A-β d2 的改动尚未提交,见 §9.0.69 ⑥) |

⚠️ **本表只记「远端状态」,不记 CI 结论** —— 该窗口的 CI 运行号未在本次复核中取证,
**不臆造**。若需补齐,应在 workercrew 侧按上述两个 commit 查 run 状态后回填。

#### ② 教训(本批最值钱的一条:文档状态会**独立于代码**腐烂)

「推送窗口待执行」这句话在 roadmap 上挂了整整一天,而两个远端**都已经有 v0.31.0**。
成因不是谁偷懒,而是**窗口执行只更新了两处文档(roadmap 指针 + journal 索引),
漏了第三处(`90-push-windows.md`)** —— 三处里漏一处,状态就分叉了。

★ **这与 §9.0.68 ① 的教训同族**:那里是"测试转红 = 位置判据",这里是"文档说没做 = 待核实项,
不是事实"。⇒ **执行推送窗口时,`ls-remote` 的结果才是真源**;roadmap 的措辞是**待办标记**,
不能当作状态。本次复核即按此纪律做的(四个 `ls-remote` 逐条打表,未引用 roadmap 自己的说法)。
### 9.0.69c ★ 推送窗口执行记录 —— `shared-v0.32.0`(2026-09-18)

> **一个窗口、一个 tag 盖一笔**:批次 A-β d2 目标判定族 + 忠犬守护全链(§9.0.69)。

#### ① 结果(逐项 `ls-remote` 取证,未引用任何 roadmap/README 的措辞)

| 项 | 值 |
|---|---|
| server master / tag | `6c8601b` · `shared-v0.32.0`(**附注** tag `41daf5f`,subject「A-beta d2: target-check family …」)—— gitee + github(workercrew)`ls-remote` 核实一致 |
| client master | `3cea798`(pin v0.31.0 → **v0.32.0**,`cmake/SaShared.cmake:129`)—— 双远端一致 |
| 本机独立发布态复验 | 干净克隆(无同级服务端)到临时目录,`ci_verify --expect-mode fetch`:**8 项全过**(fetch 模式 · 锁定 ref = v0.32.0 · 与源码一致 · 7 条全部注册 · ctest 7/7 · **147 用例 / 3,035 断言**) |
| server 本机 ci_verify | **6 项全过**(22 条全部注册 · ctest 22/22 · 断言防线反向验证通过) |
| 两端用例集一致性 | 客户端发布态复现的 **147 / 3,035** 与服务端 `rules_battle` 同值 ⇒ D2「一份规则两端编译」本批成立 |

⚠️ **本表不记 CI 运行号** —— 本机无 `gh` CLI,workercrew 侧 run 状态未在本次取证,
**不臆造**(同 §9.0.69b 的处置)。若要补齐,按上表两个 commit 查 run 后回填。

#### ② 教训(本批最值钱的一条:把「每次窗口都变的值」抄进注释,它必然烂)

`cmake/SaShared.cmake` 的 `SA_SHARED_GIT_TAG` **上方**有一句注释写着「当前锁定值 = shared-v0.26.0」,
而 pin 早已前推到 v0.31.0 —— **六个 tag 的差距**。★ 关键不是这个数错了,是它**怎么错的**:

- 最近三次 pin 前推(`766d591`/`8685a8d`/`6f68f4b`)**都改了这个文件里的 pin 那一行**,
  三次都没有改这句注释,而**没有任何东西会因此报错** —— 构建照常、CI 照常、测试照常。
  ⇒ 与 `00` §10.4 那族静默错误同形:**不报错、不崩溃,只是悄悄地说了一句错话**。
- 同族的第二处在客户端 `README.md` 的 D2 闸门行:也写 `shared-v0.26.0` **并附了用例数**
  (115 例 / 2,763 断言)—— 那个数**在写下它的时候就注定要过期**,因为用例集每批都在长。

★ **处置不是「这次记得改」,是别再复述**:值只留一处(`set()` 那一行,且 `ci_verify.py`
  已有「锁定 ref 与源码一致」把它钉住),注释改为只解释**为什么**是这个值。
  ⇒ 一条可推广的判据:**注释里凡是会出现「当前 X = 某个每次都会变的值」这种句式,
  就是一笔注定要还的债** —— 要么删掉复述,要么给它加一条会失败的断言
  (如 `ci_verify.py` 对 ref 做的那样)。本仓采用的是前者。

ⓘ 与 §9.0.69b 的教训**同族但不同处**:那条是「窗口要更新三处文档,漏一处状态就分叉」
  (该更新的没更新);本条是「不该复述的复述了」(本就不该存在的第二处真源)。
  两者共同点:**同一个事实有两个存放点,而只有一个是真源**。

#### ③ 同批补:`docs_index` 守卫的两处盲区(修注释时**顺手发现的,不是顺手改的**)

修上面那句注释时去核 `journal/README.md` 的 per-file 表,发现**该表已烂**:

| 行 | 表里写 | 实际 | 状态 |
|---|---|---|---|
| `90-push-windows.md` | 10 节 | **15 节** | 五个窗口没算进去 |
| `10-battle-core.md` | 6 节 | **7 节** | 漏 A-β d2 |
| `14-item.md` | 4 节 | **5 节** | 漏 I\| 指令入口校验 |
| `18-petskill.md` | **整行都没有** | 3 节 | B1/B2/B3 三批从未登记 |
| **合计** | **63** | **72** | 上面几项之和 |

⚠️★ 而且**我上一条提交刚把它从 9 改成 10** —— 照着一个已经错了五次的数继续加一,
  等于**给错误盖上确认章**。这条值得单独记:在错误的数据上做正确的运算,结果仍是错的。

**两处盲区(都在 `tools/check_docs_index.py`)**:

1. **字母后缀编号两边同时瞎 ⇒ 判据③恒绿**。旧式样 `9\.0\.[0-9.]+ `(编号后紧跟空格)
   **匹配不上** `### 9.0.69b`;而映射表那一侧的式样同样匹配不上。
   ⇒ `owner - listed` 与 `listed - owner` **都是空集**,判据③永远通过。
   ★ 这是"两个错误互相抵消"的典型:**单看任何一条断言都通过,而它守的东西已经漏了**。
   实测:旧式样 70 个编号 / 新式样 72 个,差的正是 §9.0.69b 与 §9.0.69c 两个。
   ⚠️ 附带风险:放宽式样后若不改判据②(`int("69b")` 抛 `ValueError`),
   脚本会**崩溃**而不是报红 —— 那比漏报更坏,故同时引入 `base_num()` 取主编号。
2. **per-file 表的「节数」列没有任何断言**。判据③守的是「按编号查得到文件」,
   而这是**另一张表**(每文件收了几节),纯手写 ⇒ 加节的人不会来看它 ⇒ 单调落后且永不报错。
   与 ② 的注释是**同一族**:**手抄的派生数据 = 注定要烂的第二真源**。
   ⇒ 补**判据⑤**:从各文件真实标题数出节数,与表里写的比;数字不再靠人维护。

**反向验证(4 处,全部精确转红后还原复绿)**:

| 注入 | 结果 |
|---|---|
| 把 `90-push-windows.md` 节数改回烂值 10 | ❌「表里写 10,实际 15」 |
| 删掉 `18-petskill.md` 整行 | ❌「这些文件在表里**整行缺失**:18-petskill.md(实际 3 节)」 |
| 删掉映射表的 `§9.0.69c` 行 | ❌「映射表漏了 1 个编号:['§9.0.69c']」—— ★ 并同时验证**旧式样下这一步两边都是空集** |
| 把「合计」72 改回 63 | ❌「写 63,实际 72」 |

★ **本次的元教训**:这两处盲区**不是被测试发现的,是被"去核对一个具体数字"发现的** ——
  修注释时顺手去查表,才发现表本身是错的。⇒ 与 §9.0.69 ① 同族(那个 bug 也是回源码复核才抓住,
  用例全绿):**绿色只证明"已断言的东西没坏",不证明"该断言的都断言了"**。
  一条守卫最危险的状态不是报红,是**它守的东西已经烂了而它恒绿**。
