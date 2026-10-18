# Bug 15518：立方柱高度达350时擦拭塔超出盘外，发送置灰但无告警提示

## 1. 基本信息
- Bug ID：`15518`
- 标题：`立方柱高度达350时，擦拭塔超出盘外不报错（发送打印按钮置灰但无告警）`
- 反馈人：`测试反馈（待补充）`
- 处理人：`wangwenbin`
- 影响模块/影响文件：
  - `src/slic3r/GUI/GLCanvas3D.cpp`
  - `src/slic3r/GUI/GCodeViewer.hpp`
  - 关联链路：`src/slic3r/GUI/PartPlate.cpp`、`src/slic3r/GUI/PartPlate.hpp`、`src/slic3r/GUI/MainFrame.cpp`

## 2. 现象与复现
- 复现场景（参数、模型、入口）：
  - 模型：`111.3mf`
  - 场景：模型高度约 `350mm`，预览中可见擦拭塔/挤出路径超出平台边界。
  - 入口：预览页点击“发送打印”。
- 实际结果：
  - “发送打印”按钮置灰，不可发送。
  - 未稳定出现“超出热床边界的G-code路径”告警提示。
- 期望结果：
  - 当越界导致不可发送时，界面应稳定给出明确告警信息。

## 3. 责任提交追溯（必填）
- 关联历史修复（3505）
  - commit hash：`c6f7e852dc94b0749dc92f2563dd3e89448772d9`
  - Author：`wangwenbin <wangwenbin@creality.com>`
  - AuthorDate：`Tue Dec 24 10:50:09 2024 +0800`
  - Subject 原文：`3505 当前界面选用机型与Gcode不一致，拖入Gcode提示 超出热床边界的G-code路径`
  - Change-Id：`I3d34f4391e17268d78d9dfa7a7e6bf9ec96d6f10`
- 本次联动修复：工作区修改 `GLCanvas3D.cpp` / `GCodeViewer.hpp`（同一条 ToolpathOutside 判定链）

## 4. 根因分析
- 触发条件：
  - 15518 场景：切片结果 `toolpath_outside=true`（擦拭塔越界），发送链路会禁用按钮。
  - 3505 场景：导入 G-code，当前界面机型与 G-code 源机型不一致（例如当前 i7 平台更小）。

- 代码链路（关键函数/关键判断）：
  - 发送禁用链路（本地切片语义）：
    - `PartPlate::check_gcode_path_contain_in_bed()` 计算并写入 `m_gcode_result->toolpath_outside`
    - `PartPlate::is_slice_result_ready_for_print()` 依赖 `!toolpath_outside`
    - `MainFrame::get_enable_print_status()` 据此置灰
  - 预览告警链路（导入 G-code 语义）：
    - `GLCanvas3D::_set_warning_notification_if_needed(EWarning::ToolpathOutside)`
    - 3505 的核心设计是：当 G-code 携带 `printable_area` 时，优先按 G-code 源机型的 printable area 判断越界，不应被当前界面机型直接覆盖。

- 为什么“3505修复”会影响“15518修复”：
  - 两个需求的“判定语义”不同：
    - 15518需要与发送禁用同源（`toolpath_outside`）。
    - 3505需要在导入 G-code 时优先使用源机型 printable area（几何包含判断）。
  - 若把 `toolpath_outside` 设为全局最高优先级，会把 3505 的 source printable area 语义压掉：
    - 当前机型较小时（i7），`toolpath_outside` 可能按当前平台语义触发，导致误报“超出平台”；
    - 这就是你反馈的 3505 回归场景。

- 15518 原始漏报的直接原因：
  - ToolpathOutside 分支曾存在“高度条件门控”（`has_printable_area()==false` 分支内与高度逻辑耦合），在 `Z=350` 边界会产生漏触发窗口。

## 5. 修复方案
- 修复目标：同时满足 3505 与 15518，不互相覆盖。

- 最终判定策略（统一规则）：
  - 在 `GLCanvas3D` 的 `ToolpathOutside` 分支采用分层优先级：
    1. 若 `has_data && has_printable_area`：优先按 `printable_area.contains(paths_bbox_ex)` 判断（3505语义优先）。
    2. 若 `printable_area` 元数据异常（如 `plater_box.max.x() <= 0`）：回退到 `is_toolpath_outside() || !is_contained_in_bed()`。
    3. 若无 `printable_area`：直接走 `is_toolpath_outside() || !is_contained_in_bed()`（15518语义）。
    4. 最后保留多色 `exclude_area` 检查，避免遗漏多色禁区越界。

- 具体修改点：
  - `src/slic3r/GUI/GLCanvas3D.cpp`
    - 重排 `EWarning::ToolpathOutside` 判定优先级，先 source printable area，再 fallback。
    - 去除 outside 与高度条件的不合理耦合，修复 `Z=350` 漏报窗口。
  - `src/slic3r/GUI/GCodeViewer.hpp`
    - 新增 `is_toolpath_outside() const`，供 GL 侧读取 fallback 状态。
    - 修复 `get_paths_bounding_box_ex()` 堆分配泄漏（改为栈对象返回）。
    - `has_printable_area()` 改为 const + `empty()` 判定。

- 结果：
  - 3505 场景：导入 G-code 时，预览以 G-code 源机型 printable area 为准，不再被当前 i7 小平台误报。
  - 15518 场景：无 printable area 时仍能通过 `toolpath_outside`/`contained_in_bed` 稳定触发越界告警。

## 6. 影响范围与风险
- 正向影响：
  - 同时修复 3505 回归与 15518 漏报：
    - 不再出现“导入 G-code 机型不一致导致误报越界”；
    - 也不再出现“发送置灰但无告警”的漏提示。
  - `get_paths_bounding_box_ex()` 内存泄漏风险消除。

- 可能风险：
  - 若第三方/历史 G-code 的 `printable_area` 元数据缺失或异常，将进入 fallback 语义，显示行为取决于 `toolpath_outside/contained_in_bed`。
  - 多告警并存（超高+越界）时，提示出现顺序可能与旧版本略有不同。

- 是否改变旧行为：
  - 保留 3505 的核心行为（导入 G-code 以源 printable area 为准）。
  - 保留 15518 目标行为（越界时稳定告警，且与发送禁用语义一致）。

## 7. 回归建议
- 必测场景：
  - 3505 场景：当前机型选 i7（小平台）+ 导入来自大平台的 G-code，实际不越界时不应误报。
  - 15518 场景：`111.3mf` + `Z=350` 擦拭塔越界，发送置灰且必须出现越界告警。

- 边界场景：
  - 导入 G-code 且 `printable_area` 缺失/异常（验证 fallback 正确）。
  - 多色并存在 `exclude_area`（确认禁区命中仍可告警）。
  - belt 机型与非 belt 机型各测一组。

- 反向场景（确保未引入新问题）：
  - 非越界模型不出现 ToolpathOutside 告警。
  - 时序摄影等既有 warning 逻辑保持不变。
  - GCodeConflict / ToolHeightOutside 告警仍按原路径触发。
