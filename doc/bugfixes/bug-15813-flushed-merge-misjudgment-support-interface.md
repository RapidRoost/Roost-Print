# Bug 修复说明：支撑界面换料场景下 Flushed 列被错误合并

## 1. 基本信息
- 问题主题：PETG 主体 + PLA 支撑/筏层界面时，预览页 `Flushed`（冲刷）列缺失，表现为“冲刷量不显示”。
- 反馈人：江圣龙
- 处理人：王文彬
- 影响模块：`G-code Preview -> Filament(ColorPrint)`、多盘汇总耗材统计
- 影响文件：`src/slic3r/GUI/GCodeViewer.cpp`

## 2. 问题引入来源（责任提交）
- 引入提交：`50bdefea5eed03c3d538a107123d1a978243c5d5`
- Author：`zhongxuan`
- AuthorDate：`2026-03-19 17:42:13 +0800`
- 该提交说明（Subject，原文）：
  - `fix:[15284]多盘的时候，冲刷时间UI缺失 and fix:[14639]预览页面，单色模型的长度和重量的计算总和上面显示的总量不一致`
- Change-Id：`I119507eb17d12cf41ecec162b531100aaad0185b`

## 3. 现象与复现
- 复现场景：
  - 导入 `666.3mf`
  - 模型主体 PETG
  - 支撑/筏层界面设置为 `2 PLA`
  - 切片后进入预览 `Filament` 统计
- 实际现象：
  - `Flushed` 列消失，冲刷量被并入 `Model`，用户感知为“冲刷没了”。

## 4. 根因分析（技术链路）
- 在提交 `50bdefea5` 中，新增了 `merge_flush_into_model` 逻辑，核心判定为：
  - 机器耗材槽数量 > 1
  - `model_used_filaments_g` 中仅 1 个挤出头有值
  - 存在 `total_flushed_filament`
- 上述判定将“单模型颜色”误当成“真正单喷嘴打印”。
- 在“PETG 模型 + PLA 支撑界面”场景中，模型体积通常仍只落在 1 个挤出头上，但实际打印存在支撑界面材料切换与冲刷，因此被误判。
- 误判后执行：
  - `color_print_displayed_columns = displayed_columns & ~ColumnData::Flushed`
  - `Flushed` 列被隐藏，并把冲刷量并入 `Model`。
- 结论：这是 **预览展示口径误判**，不是切片冲刷计算缺失。

## 5. 修复方案（本次）
- 修复原则：仅在“统计意义上的真正单喷嘴场景”才允许把 `Flushed` 并入 `Model`。
- 新增统一判定函数：`should_merge_flush_into_model(const PrintEstimatedStatistics& stats)`
- 判定条件：
  - `has_flush == true`
  - `active_extruders == 1`（基于 `total_volumes_per_extruder`，必要时回退到各角色 volume map 汇总）
  - `has_support == false`
  - `has_wipe_tower == false`
- 替换范围：
  - 多盘汇总逻辑 1 处
  - ColorPrint 标题列逻辑 1 处
  - ColorPrint 行数据逻辑 1 处
- 修复结果：
  - 支撑界面多材料场景恢复显示 `Flushed` 独立列；
  - 单喷嘴纯模型场景仍保留原先“合并显示”优化。

## 6. 风险与回归建议
- 低风险：仅调整 UI 展示判定口径，不改底层冲刷计算链路。
- 回归建议：
  - 多材料但单模型颜色（含支撑界面材料切换）
  - 多盘混合场景（不同盘不同材料策略）
  - 纯单喷嘴单材料场景（确认仍按预期合并）

