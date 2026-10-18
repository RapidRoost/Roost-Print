# Bug 15836 - 冲刷量计算不对（导入 3MF 后初始值未刷新）

- 创建人：李佳沁
- 处理人：wangwenbin
- 日期：2026-04-15
- 现象标题：冲刷量计算不对

## 问题现象
导入 `立方体.3mf` 后，打开“耗材丝更换时的冲刷体积”弹窗，初始矩阵显示为统一值（如 224）。
点击“重新计算”后，矩阵才刷新为按颜色差异计算后的非对称值（如 356/514/561 等）。

## 预期行为
导入 3MF 后首次打开弹窗时，应直接展示正确的冲刷矩阵，不需要手动点击“重新计算”。

## 根因分析
在 3MF 导入流程中，`flush_volumes_changed` 被强制设置为 `true`，导致自动重算路径被屏蔽：

1. 导入后项目配置中的 `flush_volumes_changed` 未保留文件原值（包括 `0`）。
2. 自动计算逻辑 `Utils::calc_flushing_volumes()` 在 `flush_volumes_changed == true` 时直接返回。
3. 因此弹窗初始显示的是文件中旧矩阵/默认矩阵，而不是当前颜色对应的实时计算值。

## 修复方案
在 `src/slic3r/GUI/Plater.cpp` 的 3MF 导入逻辑中做最小改动：

1. 不再强制把 `flush_volumes_changed` 改为 `true`。
2. 若 3MF 缺少该字段，默认设置为 `false`（可自动重算）。
3. 导入并同步耗材颜色后，当 `flush_volumes_changed == false` 时自动调用 `Utils::calc_flushing_volumes()`。

## 结果
导入 `立方体.3mf` 后，首次打开冲刷体积弹窗即可看到正确矩阵，无需手动点击“重新计算”。

## 影响范围
- 仅影响 3MF 导入后的冲刷矩阵初始化时机。
- 不改变用户手动修改过冲刷矩阵（`flush_volumes_changed == true`）的保留行为。

## 关联文件
- `src/slic3r/GUI/Plater.cpp`
- `doc/bugfixes/bug-15836-flush-volume-initial-calc-after-3mf-import.md`