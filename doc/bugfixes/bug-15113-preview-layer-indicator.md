# Bug 修复记录

## 1. 基本信息
- Bug ID: `15113`
- 标题: `模型使用高度范围修改器后，在预览页面点击对象管理的层范围时，3D视图区不应该显示指示器`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-15113.html`
- 提交日期: `2026-03-03`
- 修复日期: `2026-03-05`
- 提交人: `冷金辉`
- 处理人: `贺淼`
- 所属计划: `CP 7.1.0 Release`
- 分支/提交: ``

## 2. 问题现象
- 模型存在高度范围修改器（Layer Range）时，进入预览页面。
- 在对象管理中点击某个层范围项，预览页 3D 视图区出现层范围指示器。
- 预期该类编辑指示仅在 3D 编辑视图展示，预览页不应显示。

## 3. 影响范围
- 模块: `对象列表`、`层范围编辑`、`3D 画布事件分发`
- 关键文件:
  - `src/slic3r/GUI/GUI_ObjectLayers.cpp`
  - `src/slic3r/GUI/GUI_ObjectList.cpp`
  - `src/slic3r/GUI/Plater.cpp`
  - `src/slic3r/GUI/GLCanvas3D.cpp`

## 4. 复现步骤（修复前）
1. 打开任意模型并添加高度范围修改器。
2. 切到“预览”页面。
3. 在对象管理中点击某个层范围（Layer Range）条目。
4. 观察预览 3D 区域出现层范围指示器。

## 5. 根因分析
- `ObjectList` 选中层范围后会触发 `ObjectLayers::update_scene_from_editor_selection()`。
- 该函数使用 `plater()->canvas3D()` 分发事件，而 `canvas3D()` 当前返回“当前画布”（可能是 Preview）。
- 因此在预览页也会收到 `handle_layers_data_focus_event`，并设置 `m_sidebar_field`，最终触发指示器渲染。
- 结论: 层范围编辑提示事件被错误路由到 Preview 画布。

## 6. 修复方案
- 将层范围编辑提示事件固定发送到 `View3D` 画布，而不是当前画布。
- 保持预览页不接收该类编辑指示事件，避免误显示。
- 不改动层范围本身逻辑，仅修正事件路由目标。

## 7. 代码改动摘要
### 7.1 `GUI_ObjectLayers.cpp`
- 修改 `ObjectLayers::update_scene_from_editor_selection()`:
  - 由 `wxGetApp().plater()->canvas3D()->handle_layers_data_focus_event(...)`
  - 调整为 `wxGetApp().plater()->get_view3D_canvas3D()->handle_layers_data_focus_event(...)`
- 结果: 层范围指示器仅在 3D 编辑视图显示，预览视图不再显示。

## 8. 验证清单
- [ ] 预览页点击对象管理层范围时，不显示层范围指示器。
- [ ] 3D 编辑页点击对象管理层范围时，仍正常显示层范围指示器。
- [ ] 预览页与 3D 页切换后，层范围显示行为一致且可预期。
- [ ] 高度范围修改器的新增、删除、编辑功能无回归。
- [ ] 编译通过且无新增告警/错误。

## 9. 风险与回退
- 风险等级: `低`
- 可能影响:
  - 若后续需求期望预览页也显示同类编辑提示，需要重新设计事件分发策略。
- 回退方案:
  - 回退 `src/slic3r/GUI/GUI_ObjectLayers.cpp` 本次修改。

## 10. 关联说明
- 本次修复针对 `BUG #15113` 的“预览页不应显示层范围编辑指示器”问题。
- 根因属于画布路由错误（current canvas vs View3D canvas）。
