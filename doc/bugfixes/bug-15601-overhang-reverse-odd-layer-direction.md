# Bug 15601：质量-悬垂-反转奇数层悬垂方向效果错误

## 1. 基本信息
- Bug ID：`15601`
- 标题：`质量-悬垂-反转奇数层悬垂方向效果错误`
- 反馈人：`江圣龙`
- 处理人：`wangwenbin`
- 影响模块/影响文件：
  - `src/libslic3r/PerimeterGenerator.cpp`

## 2. 现象与复现
- 复现场景：
  - 使用同一模型、同一参数，仅切换“反转奇数层悬垂方向”开关，得到两个 G-code：
    - `默认.gcode`（未勾选）
    - `勾选.gcode`（已勾选）
  - 在预览第 `164` 层观察悬垂墙方向。
- 实际结果：
  - 第 `164` 层出现方向差异（一个顺时针、一个逆时针），表现看起来像“偶数层触发了奇数层逻辑”。
- 期望结果：
  - “反转奇数层悬垂方向”应与用户可见层号一致（第1、3、5...层生效），不产生奇偶认知错位。

## 3. 根因分析
- 旧逻辑在悬垂反转判定中使用：
  - `layer_id % 2 == 1`
- 其中 `layer_id` 为内部层索引：
  - `g.layer_id = (int)this->layer()->id();`
  - 该索引是 `0` 基（且内部存在 raft 偏移语义）。
- 结果：
  - 用户可见第 `164` 层对应内部 `layer_id=163`（无 raft 场景），会被当作 odd 处理。
  - 导致“UI看是偶数层，代码按奇数层触发”的错位感知。

## 4. 修复方案
- 修复原则：
  - 反转判定按“对象层内索引”计算，即先扣除 raft 偏移，再按用户可见层号口径（1-based）判定奇偶。
- 具体改动：
  - 在两处悬垂反转逻辑中统一改为：
    - `object_layer_index = layer_id - raft_layers`
    - `object_layer_index >= 0 && object_layer_index % 2 == 0`
  - 这样 `object_layer_index=0` 对应用户可见第1层（奇数层），与参数文案语义一致。
- 修改位置：
  - `src/libslic3r/PerimeterGenerator.cpp:339`
  - `src/libslic3r/PerimeterGenerator.cpp:701`

## 5. 变更前后对比
- 变更前：
  - `overhang_reverse && layer_id % 2 == 1`
- 变更后：
  - `overhang_reverse && object_layer_index >= 0 && object_layer_index % 2 == 0`

## 6. 影响范围与风险
- 正向影响：
  - “反转奇数层悬垂方向”与用户层号认知一致，消除 odd/even 错位。
  - 兼容 raft 偏移场景，避免简单改 `%2` 造成新错位。
- 风险评估：
  - 仅影响开启 `overhang_reverse` 的悬垂反转判定。
  - 不改变其它墙序策略（如固定墙方向、非悬垂区路径生成）主逻辑。

## 7. 验证建议
- 必测：
  - 相同模型分别切 `默认.gcode` / `勾选.gcode`，逐层对比第1~5层与第164层方向。
  - 有 raft / 无 raft 各测一组，确认奇偶层触发口径一致。
- 边界：
  - `overhang_reverse_threshold=0`（每个奇数层都应触发）
  - `overhang_reverse_internal_only=on`（仅内部墙反转，外墙方向保持预期）
