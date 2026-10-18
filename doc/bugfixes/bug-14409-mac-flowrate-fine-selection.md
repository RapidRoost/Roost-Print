# Bug 14409：【Mac】校准中流量细调无法选择效果

## 1. 基本信息
- Bug ID：`14409`
- 标题：`【Mac】校准中流量细调，无法选择效果`
- 反馈人：`檀献祖`
- 处理人：`wangwenbin`
- 影响模块/影响文件：
  - `src/slic3r/GUI/calib_dlg.cpp`
  - `src/slic3r/GUI/calib_dlg.hpp`
  - `src/slic3r/GUI/Widgets/HoverBorderIcon.cpp`
  - `src/slic3r/GUI/Widgets/HoverBorderIcon.hpp`

## 2. 现象与复现
- 复现场景（参数、模型、入口）：
  - 入口：`校准 -> 流量 -> Pass 2（细调）`
  - 在 9 宫格（-20 ~ 20）里任意左键点击一个候选项。
- 实际结果：
  - 在 mac 上点击后短暂出现绿色高亮，鼠标移开后高亮消失，用户感知为“未选中/无法选择”。
- 期望结果：
  - 点击后应保持唯一选中态，鼠标移开不应丢失选中高亮。

## 3. 责任提交追溯（若有）
1. commit hash：`92a68f8fce5567950675668cac0c7699834e7a0e`
- Author：`weizhixiao`
- AuthorDate：`2025-05-09 11:32:49 +0800`
- Subject 原文：`feat:[校准]校准界面的还原`
- Change-Id：`Ib85ee6502b8bd544d0ccda45d0d6d775a1cbb95a`

2. commit hash：`337c32f6251db582efa8ea60077daf1e46bbf985`
- Author：`weizhixiao`
- AuthorDate：`2025-05-30 17:03:08 +0800`
- Subject 原文：`fix:[8771]【mac】点击同步预设、冲刷、添加、减少耗材图标之后一直显示选中状态`
- Change-Id：`I5de0ab22b13ef80085e795c0079f897e8c5d3746`

## 4. 根因分析
- 触发条件：
  - mac 平台在 `Pass 2` 9 宫格里点击并移出按钮区域。
- 代码链路（关键函数/关键判断）：
  - `High_Flowrate_Dlg` 中 9 个按钮原先仅在 `wxEVT_LEFT_DOWN` 更新 `m_CurrentValue`，未维护持久“选中项”。
  - `ImgBtn` 的边框高亮原先仅依赖 `Hovered` 状态渲染，没有 `Checked` 的持久选中映射。
  - mac 分支 `HoverBorderIcon::OnMouseEnter/OnMouseLeave` 使用 `set_state(9,9)` / `set_state(0,9)`，状态处理与 hover 清除叠加后，移出鼠标时视觉高亮被撤销。
- 为什么会出现该现象：
  - UI 视觉状态与业务值解耦不完整：业务值更新了，但视觉“选中态”仍由 hover 驱动，导致 mac 鼠标离开后看起来“没选中”。

## 5. 修复方案
- 修复思路：
  - 将“悬停态（hover）”与“选中态（checked）”拆分，建立单一选中索引并持久渲染。
- 修改点（文件 + 关键逻辑）：
  1. `src/slic3r/GUI/Widgets/HoverBorderIcon.cpp`
  - mac 下 enter/leave 仅更新 `Hovered` 位：`StateHandler::Hovered`。
  - 新增 `ImgBtn::SetSelected/IsSelected`，提供持久选中控制。
  - `ImgBtn` 边框颜色增加 `StateColor::Checked` 映射，选中后保持高亮。

  2. `src/slic3r/GUI/calib_dlg.hpp` / `src/slic3r/GUI/calib_dlg.cpp`
  - `High_Flowrate_Dlg` 新增 `m_option_buttons`、`m_selected_index`、`set_selected_option(int)`。
  - 9 宫格从逐个 `wxEVT_LEFT_DOWN` 改为统一 `wxEVT_LEFT_UP`，命中后调用 `set_selected_option`。
  - `set_selected_option` 同步两类状态：
    - 业务值：更新 `m_CurrentValue`
    - 视觉值：仅一个按钮 `SetSelected(true)`，其他按钮置 `false`
  - 弹窗初始化时默认 `set_selected_option(0)`，与原默认流量值 `0.8f` 保持一致。

- 为什么这样改：
  - 状态机语义正确：hover 是瞬时态，checked 是持久态。
  - 跨平台一致性更高，不再依赖不同平台对 enter/leave 的细节差异。

## 6. 影响范围与风险
- 正向影响：
  - mac 上细调 9 宫格可稳定保持选中态，用户可明确感知当前选择。
  - Windows 交互表现保持一致或更稳定（由按下触发改为抬起命中触发）。
- 可能风险：
  - 事件从 `LEFT_DOWN` 调整为 `LEFT_UP` 后，极端拖拽释放场景的触发时机会变化（符合标准按钮语义）。
- 是否改变旧行为：
  - 是，UI 选中展示从“悬停主导”改为“点击持久主导”；业务默认值与确认逻辑不变。

## 7. 回归建议
- 必测场景：
  - mac：`校准 -> 流量 -> Pass 2`，逐个点击 9 个选项，鼠标移开后仍保持唯一高亮。
  - mac：点击“确定”后，确认下发值与当前选中项一致。
  - Windows：同流程确认无回退问题（选中、确认、取消均正常）。
- 边界场景：
  - 快速连点不同选项，始终只保留一个选中态。
  - 鼠标按下后拖出按钮区域再释放，不应误选。
- 反向场景（确保未引入新问题）：
  - 仅悬停不点击时，不应产生持久选中态。
  - 打开弹窗后直接确认，应使用默认选中项（`0.8f`）并保持视觉一致。