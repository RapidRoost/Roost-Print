# Bug 修复说明（12341 / 15789）

## 基本信息
- 问题族: `墙顺序 Inner/Outer/Inner（内外内）不生效`
- 记录日期: `2026-04-15`
- 说明: 两个单子属于同一问题族，但修复范围不同，本文拆分记录。

## Bug 12341（Classic 路径）
- Bug ID: `12341`
- 标题: `【用户反馈】内墙外墙内墙的功能，在首层不生效`
- 创建人: `康美樱`
- 解决人: `王文彬`
- 记录日期: `2026-03-19`
- 修复范围: `Classic` 墙生成路径

### 12341 问题表现
- 在 `Classic` 模式下，`wall_sequence=InnerOuterInner` 首层未按“内外内”执行。
- 从第 2 层开始表现正常，首层与后续层行为不一致。

### 12341 修复内容
- 在 `process_classic()` 中修复 `InnerOuterInner` 首层生效逻辑。
- 修复结果是：`Classic` 下“内外内”按预期生效。

## Bug 15789（Arachne 路径）
- Bug ID: `15789`
- 标题: `【用户反馈】【7.1.0引入】墙参数 内外内的不生效`
- 创建人: `康美樱`
- 解决人: `王文彬`
- 记录日期: `2026-04-15`
- 修复范围: `Arachne` 墙生成路径

### 15789 问题表现
- 在 `Arachne` 模式下，`wall_sequence=InnerOuterInner` 在部分模型/路径下失效。
- 用户表现为“墙顺序参数设置了内外内，但切片结果不按内外内执行”。

### 15789 根因
- `Arachne` 路径中，聚类后的轮廓顺序在部分场景会偏离 IOI 预期。
- 原有重排策略对这类场景鲁棒性不足，导致 `InnerOuterInner` 不稳定。

### 15789 修复内容（对齐 Orca 2.3.0-beta）
- 在 `process_arachne()` 中引入并应用以下逻辑：
1. 先稳定前置外轮廓（`contour + inset_idx==0`）。
2. 基于线段最小距离做邻接分层重排（proximity reorder）。
3. 采用外墙/内墙阈值并加轻微扩展因子，提升排序稳定性。
- 新增距离计算基础能力：
1. `src/libslic3r/MultiPoint.hpp`：声明 `minimumDistanceBetweenLinesDefinedByPoints(...)`。
2. `src/libslic3r/MultiPoint.cpp`：实现线段最小距离与点到线段距离计算。

## 涉及代码
- `src/libslic3r/PerimeterGenerator.cpp`
- `src/libslic3r/MultiPoint.hpp`
- `src/libslic3r/MultiPoint.cpp`

## 最终结论
- `12341`：修复的是 `Classic` 模式下的 IOI 首层生效问题。
- `15789`：修复的是 `Arachne` 模式下的 IOI 参数失效问题。
- 两者属于同一“墙顺序 IOI”问题族，但对应不同墙生成路径，已在同一文档中分别说明。