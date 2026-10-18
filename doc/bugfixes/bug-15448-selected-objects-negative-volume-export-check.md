# 选中对象导出 STL 时负零件检测范围修复

## 1. 问题背景
- 场景：在 Plater 中执行“仅导出选中对象”的 STL 导出。
- 旧行为：
  - 单选完整对象时，负零件检测只看当前对象。
  - 多选完整对象时，负零件检测会扫描整个工程中的所有对象。
- 问题表现：
  - 即使当前选中的多个对象都不包含负零件，只要工程里其他未选中的对象包含负零件，仍然会弹出“Negative parts detected...”确认弹窗。
- 期望行为：
  - 当用户选择“仅导出选中对象”时，无论单选还是多选，都只检查当前选中的对象。

## 2. 影响范围
- 入口函数：
  - `src/slic3r/GUI/Plater.cpp`
  - `Plater::export_stl(bool extended, bool selection_only, bool multi_stls)`
- 关联选择集接口：
  - `src/slic3r/GUI/Selection.hpp`
  - `src/slic3r/GUI/Selection.cpp`
  - `Selection::get_selected_object_instances()`

## 3. 根因分析
- 旧逻辑按以下分支处理：
  - `selection_only && !selection.is_multiple_full_object()`：单选完整对象，只检查当前对象。
  - 其余情况：直接遍历 `p->model.objects`，检查整个工程。
- 这导致“多选完整对象 + 仅导出选中对象”被错误地落入“检查整个工程”的分支。
- 实际上，这个分支应当只检查当前 selection 对应的对象集合，而不是所有对象。

## 4. 修复方案
- 在 `Plater::export_stl(...)` 中抽出统一判断函数：
  - `object_has_negative_volume(const ModelObject*)`
- 调整检测逻辑为三类：
  - `selection_only && selection.is_multiple_full_object()`
    - 通过 `selection.get_selected_object_instances()` 收集当前选中的对象索引。
    - 去重后，仅检查这些选中对象是否包含 `NEGATIVE_VOLUME`。
  - `selection_only && !selection.is_multiple_full_object()`
    - 保持原有单选完整对象逻辑，只检查当前对象。
  - `!selection_only`
    - 保持原有整工程导出逻辑，检查整个 `p->model.objects`。

## 5. 代码改动
- [Plater.cpp](/e:/C3DSlicer_release/C3DSlicer/src/slic3r/GUI/Plater.cpp:16109)
  - 重构 `export_stl(...)` 中的负零件检测逻辑。
  - 多选完整对象时改为仅检查选中对象。
- [Selection.hpp](/e:/C3DSlicer_release/C3DSlicer/src/slic3r/GUI/Selection.hpp:402)
  - 将 `get_selected_object_instances()` 声明改为 `const`。
- [Selection.cpp](/e:/C3DSlicer_release/C3DSlicer/src/slic3r/GUI/Selection.cpp:3159)
  - 将 `get_selected_object_instances()` 实现改为 `const`。
  - 同步将内部遍历改为 `const_iterator / const_reverse_iterator`，以兼容 const 上下文。

## 6. 编译问题与处理
- 在修改 `Plater.cpp` 后，新增调用发生在 `const Selection& selection` 上下文中。
- 因此原来的 `Selection::get_selected_object_instances()` 非 `const` 声明会触发编译错误。
- 修复方式：
  - 为该接口补充 `const` 限定。
  - 同步修正实现内部的迭代器类型，避免从 `const_iterator` 到 `iterator` 的转换错误。

## 7. 验证结果
- 功能验证结论：
  - 单选完整对象时，行为保持不变。
  - 多选完整对象且选中对象都没有负零件时，不再因为未选中对象中的负零件而弹窗。
  - 整工程导出时，仍然按原逻辑检查所有对象。
- 编译验证：
  - 已执行以下命令并通过：

```powershell
cmake --build build_Release --config Release --target libslic3r_gui -- /m:4
```

## 8. 回归建议
- 建议至少覆盖以下场景：
  1. 单选一个带负零件的对象导出 STL，确认仍会弹窗。
  2. 多选多个对象，其中选中对象均不带负零件，但工程中存在未选中的负零件对象，确认不弹窗。
  3. 多选多个对象，其中任一选中对象带负零件，确认会弹窗。
  4. 从文件菜单执行整工程导出，确认仍会检测全部对象。
  5. 选中 wipe tower 或非完整对象时，确认导出入口行为不回归。
