# 2026-09-11 审计复现材料

对应[主报告](../../2026-09-11-8.0-fidelity.md)。这些文件是独立审计附件，不加入产品 CMake、CI 或游戏运行路径，不修改原版源码。

## 文件

| 文件 | 用途 |
|---|---|
| [RulesProbe.cpp](RulesProbe.cpp) | F01–F06、F16 的源码语义反例 |
| [WorldProbe.cpp](WorldProbe.cpp) | F07–F10、F14–F15 的世界集成反例，以及 F19 等运行观测 |
| [ClientProbe.cpp](ClientProbe.cpp) | F11–F12 的消息及 HP 表现反例 |
| [ClientBoundsProbe.cpp](ClientBoundsProbe.cpp) | F17 的越界读取 |
| [run_probes.py](run_probes.py) | 干净构建、基线测试和探针执行 |
| [inspect_sources.py](inspect_sources.py) | 静态枚举解析、真实表统计和文件指纹 |
| [source-evidence.json](source-evidence.json) | 本轮静态提取结果、源码及数据 SHA-256 |
| [results.txt](results.txt) | 本轮基线测试和探针关键输出 |

F13 与 F18 依据主报告所列静态控制流，未声称完成 GUI 失败测试或原版运行差分。

## 执行

需要本机已有 CMake、Ninja、兼容 Clang/GCC 的 c++、Python 和本地 doctest 源码。脚本不下载安装依赖；doctest 使用工程现有缓存。当前机器示例，在 stone-age-server/ 下运行：

~~~sh
python3 docs/audits/evidence/2026-09-11/run_probes.py \
  --doctest-source build/l4/_deps/doctest-src

python3 docs/audits/evidence/2026-09-11/inspect_sources.py
~~~

可使用 --build-root 指定**未配置过的新目录**。脚本先执行两仓基线 CTest，再分别编译探针。完整编译日志、测试日志、ASan 输出和 metadata.json 保留在其打印的目录中。其他机器应把 --doctest-source 指向自己的本地缓存。

本轮验证环境为 macOS arm64 / AppleClang 21。ASan 用例会主动触发已发现的越界；网络基线测试需要绑定本地端口。如果运行环境禁止本地 bind，基线会报 Operation not permitted，这不算项目的网络缺陷，应在获准绑定端口的环境重跑，不能改测试绕过。

退出码：

- 0：所有探针期望成立；仍需阅读 OBSERVATION 项及静态问题，不能据此关闭全部审计。
- 1：至少一项探针期望失败。**在主报告基线上这是预期的审计结果**；修复后应由红转绿。
- 2：构建、环境、测试前置或基线验证失败，不能据此声称完成复现。
- ClientBoundsProbe 在越界时由 ASan 终止，具体退出码随平台不同。

探针使用项目已有接口及测试观察面。WorldProbe 的宠物测试通过捕获链获得宠物，再注入一个合法的“战场受伤后”HP，以隔离随机伤害；此处没有注入“回写缺失”本身。RulesProbe 的数值期望来自主报告列出的原始表达式，不编译完整原版服务端。

CTRL/CONTROL 前置断言必须成立。直接源码推定、原式表达式验证、新实现运行观测和完整原版运行是不同证据，本附件只提供前三类。
