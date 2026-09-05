# config/ —— 运行配置样例

⚠️★ 这里的文件是**样例与脚手架**,不是部署配置。
生产配置(分布式拓扑、MySQL / Redis 端点)属阶段 3,那时形状会变。

| 文件 | 用途 |
|---|---|
| `demo.json` | ★★ **1.4 最小双端 demo 的服务端配置**。打开 `demo_battle` ⇒ 握手即入场一场 demo 战斗 |

## demo.json 怎么用

```sh
./build/<preset>/src/stone_age_server --config config/demo.json
```

起来之后监听 `127.0.0.1:8300`。客户端连上、握手通过,即会收到:

```
HandshakeAccepted (0x0002)
BattleSelfInfo    (0x0203)   ← 你是谁(battle_id + slot)
BattleTurnBegin   (0x0202)   ← 现在第几回合
BattleEvents      (0x0205)   ← 每个节拍一批,这就是 1.4 的验收对象
BattleTurnBegin   (0x0202)
...
```

上行发 `BattleCommand` (0x0210) 即参与结算 —— ⚠️ `battle_id` 与 `turn`
必须取自上面那两条,`turn` 对不上会被 world 静默丢弃(见 `01` §1.3 的取向)。

## ⚠️ `demo_battle` 是临时物

`bind_addr` 默认 `0.0.0.0`,这里特意写成 `127.0.0.1` —— demo 没有任何鉴权,
握手通过就是"已认证"(`net/api.h` 说明了为什么:IDL 里根本没有认证这条消息)。

`demo_battle` 打开后**握手即入场**,会话状态从 `authenticated` 直接变成 `online`。
真玩法里"握手完进哪里"是**选角 + 登录点**的结果(阶段 2,要 storage)。
⇒ 阶段 2 接上选角时,`demo_battle` 连同 `World::OnSessionReady` 里那段整块删掉。
