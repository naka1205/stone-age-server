#!/bin/sh
# idl/tests/run.sh —— IDL 全量校验
#
# 四道关(前三道见 02-protocol.md §8,第四道见 smoke.cpp 卷首):
#   1. schema 一致性:编号唯一性 / 编号稳定性 / 号段 / 分层 / 无条件编译
#   2. 生成物与 schema 同步(生成物入库,DR-TS2)
#   3. 生成物可编译(POD 约束、枚举窄化、msg_id 映射)
#   4. 运行时:编解码往返 / 截断输入 / 写缓冲不足 / ★ 体积回归
#
# 用法:sh idl/tests/run.sh

set -e
cd "$(dirname "$0")/.."

echo "── 1/2 schema 检查 + 生成物同步 ──"
python3 codegen/sgidl_gen.py --verify

echo "── 3 编译生成物 ──"
CXX=${CXX:-c++}
$CXX -std=c++20 -Wall -Wextra -Werror -Igenerated/cpp tests/smoke.cpp -o /tmp/sg_idl_smoke

echo "── 4 运行 ──"
/tmp/sg_idl_smoke

echo "✅ IDL 四道关全部通过"
