# 最小可玩环境

当前入口支持登录/注册、创建选角、赛伊那斯地图移动遇敌、手动战斗与捕获、保存退出和重登。数据库与内容格式见[存储架构](../docs/04-storage-schema.md)，验收与范围见[可玩流程记录](../docs/audits/2026-09-12-playable-loop.md)。

## 依赖与构建

使用 MySQL **8.4.8**、Connector/C++ **8.4 JDBC/classic**、Redis **8.6.2**、hiredis 和 OpenSSL。数据库通过 `caching_sha2_password` 与验证服务器身份的 TLS 连接，游戏 TCP 连接也启用标准 TLS。SQL 在专用工作线程执行；世界 tick 只提交不可变请求并读取完成通知。

在服务端仓配置，其中三个路径指向已经解包/构建的依赖：

```sh
cmake -S . -B build/playable -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSA_ENABLE_MYSQL_STORAGE=ON \
  -Dmysql-concpp_DIR=/path/to/mysql-connector-c++-8.4.0 \
  -DSA_HIREDIS_INCLUDE=/path/to/redis-8.6.2/deps/hiredis \
  -DSA_HIREDIS_LIBRARY=/path/to/redis-8.6.2/deps/hiredis/libhiredis.a
cmake --build build/playable --parallel 4
```

macOS 上，CMake 会在构建目录复制 JDBC 动态库，修正其 OpenSSL 路径并签名，原依赖目录保持原样。默认关闭 `SA_ENABLE_MYSQL_STORAGE` 的构建仍能运行规则测试与旧战斗 demo；`--playable` 需要启用该选项。

## 独立本地实例

```sh
python3 tools/playable_env.py start \
  --root /private/tmp/stoneage-playable-runtime \
  --mysql-home /private/tmp/mysql-8.4.8-macos15-arm64 \
  --redis-home /private/tmp/redis-8.6.2 \
  --openssl /opt/homebrew/opt/openssl@3/bin/openssl
python3 tools/playable_env.py status --root /private/tmp/stoneage-playable-runtime
```

脚本只管理指定目录下的独立实例，监听本机 MySQL 13306 / Redis 16379。首次启动执行 `sql/001-session.sql`，创建 `sa_session` 和保留的 `sa_social` schema，随机生成密码和本地 CA；私有配置留在权限为 0700 的运行目录，文件为 0600。它不注册系统服务，也不清除已有数据。

地图与数值表属于仓内版本化内容。两端已带 `content/p2-v1` 对应产物；从原始资料重建时在服务端仓执行：

```sh
python3 tools/build_playable_content.py \
  --source-root .. \
  --client-output ../stone-age-client/assets/content/p2-v1 \
  --server-output content/p2-v1
```

转换器只读取原始文件，需要兄弟仓 `stoneage-plan/tools/extract_samples.py`。双方内容版本必须一致。

## 启动与操作

先在服务端仓启动：

```sh
build/playable/src/stone_age_server \
  --config /private/tmp/stoneage-playable-runtime/server.json \
  --playable /private/tmp/stoneage-playable-runtime/storage.json
```

再在客户端仓，使用已安装的 GameStudio 前缀构建并启动：

```sh
cmake -S . -B build/playable -G Ninja -DCMAKE_PREFIX_PATH=/path/to/GameStudio/install
cmake --build build/playable --parallel 4
build/playable/stone_age_client --playable /private/tmp/stoneage-playable-runtime/client.json
```

账号使用 3–63 位字母、数字、下划线或连字符，密码至少 12 字节。注册后填写角色名、选择合法属性预设和元素并创建角色。方向键/WASD 按屏幕方向移动；遇敌后选择目标，按 1 攻击、2 捕获、3 防御、4 逃跑，也可点击按钮。敌方位于左上朝右下，我方位于右下朝左上。战斗结束后返回地图，在“宠物 / 背包”查看结果。

“保存并退出”只有在数据库事务提交后才完成。再次登录同一账号可选择已有角色。关闭本地测试环境：

```sh
python3 tools/playable_env.py stop --root /private/tmp/stoneage-playable-runtime
```

## 验证

```sh
python3 tools/ci_verify.py --build-dir build/playable --generator Ninja
env SA_STORAGE_CONFIG=/private/tmp/stoneage-playable-runtime/storage.json \
  build/playable/tests/sa_mysql_storage_test
```

真实存储测试创建独立测试账号，覆盖持久化、稳定资产 UID、revision 冲突、重复登录和跨工作线程租约接管。仅移除自己测试账号的 Redis key，不清空 Redis。

macOS 图形验收由 `tools/playable_gui_smoke.py` 驱动真实键鼠事件，需要先编译 `tools/gui_control.swift` 到 `/private/tmp/stoneage-gui-control`。截图从测试客户端自己的 framebuffer 回读。输出目录中的 `results.json` 和 PNG 可作为证据；私有运行配置不入库。

Linux CI 通过 `tools/ci_storage.py` 启动固定版本的 MySQL/Redis 容器执行同一组存储测试。本机未安装 Docker，容器验证由 CI 完成。
