# Bug 修复说明（12341）

## 基本信息
- Bug ID: `12341`
- 标题: `【用户反馈】内墙外墙内墙的功能，在首层不生效`
- 创建人: `康美樱`
- 解决人: `王文彬`
- 当前状态: `已解决`
- 记录日期: `2026-03-19`

## 问题现象
- 当墙顺序设置为 `Inner/Outer/Inner`（内墙/外墙/内墙）时，首层未按该顺序执行。
- 从第 2 层开始，顺序表现正常，导致首层与后续层行为不一致。

## 根因分析
- 该逻辑在两条代码路径中均存在“首层禁用 InnerOuterInner”的限制：
1. `Classic` 路径：`InnerOuterInner` 分支要求 `layer_id > 0`。
2. `Arachne` 路径：
   - 首层会把 `InnerOuterInner` 从 `is_outer_wall_first` 判定中排除；
   - IOI 重排逻辑同样限制为 `layer_id > 0`。
- 由于默认墙生成器为 `Arachne`，仅修改 `Classic` 不能覆盖默认用户场景。

## 修复方案
- 统一放开 `InnerOuterInner` 在首层的生效限制（Classic + Arachne 两条路径同时修复）。
- 保留首层外侧 Brim 的既有策略：
  - 在 `首层 + 外侧Brim` 条件下仍优先外墙，以保持 Brim 连续打印行为。

## 涉及代码
- `src/libslic3r/PerimeterGenerator.cpp`
  - `process_classic()`：移除 `InnerOuterInner` 的 `layer_id > 0` 限制。
  - `process_arachne()`：
    - 去掉“首层强制禁用 IOI”的判定；
    - IOI 重排改为在“非首层外侧 Brim”时启用。

## 结果
- `Inner/Outer/Inner` 在首层与非首层保持一致生效。
- 首层存在外侧 Brim 时，仍维持外墙优先策略，避免引入 Brim 相关回归。
