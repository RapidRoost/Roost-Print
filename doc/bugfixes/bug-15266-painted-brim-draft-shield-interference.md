# Bug 修复说明

## 1. 基本信息
- Bug ID: `15266`
- 标题: `手动绘制的 Brim 与风挡路径干涉`
- 日期: `2026-03-16`
- 创建人: `江圣龙`
- 处理人: `王文彬`
- 分支/提交:

## 2. 问题现象
- 在 `Brim type = Painted`（手动绘制耳状 Brim）且 `风挡 = 打开` 的场景下，首层 `skirt/draft shield` 路径会与手绘 brim 发生重叠干涉。
- 在 G-code 预览中可见：
  - 风挡路径压到手绘 brim 区域；
  - 部分场景中，brim 内部仍会残留少量裙边短线。

## 3. 影响范围
- 模块: `首层 skirt / draft shield 与 brim 路径裁剪逻辑`
- 关键文件:
  - `src/libslic3r/GCode.cpp`
- 受影响条件:
  - `draft_shield != disabled`
  - `brim_type = painted`（也可能影响其他有 brim 的类型）
  - `skirt_type = combined` 更容易暴露问题

## 4. 复现步骤（修复前）
1. 选择单模型并进入 Brim 耳朵绘制工具。
2. 手动绘制局部耳状 brim（模型外侧）。
3. 设置 `Skirt类型 = 组合`、`风挡 = 打开`。
4. 切片并查看首层 G-code 预览。
5. 结果: 风挡裙边与手绘 brim 发生路径重叠，且 brim 内部可能残留零碎裙边段。

## 5. 根因分析
- 旧逻辑中，首层 skirt 对 brim 的裁剪条件仅在 `stPerObject` 生效；`stCombined` 未进入该裁剪分支。
- 因此 `stCombined + draft_shield` 时，skirt 直接按自身轨迹输出，未避让 brim。
- 另外，裁剪时若直接使用原始 brim 多边形，受几何精度与孔洞结构影响，可能在 brim 内部留下少量短路径残段。

## 6. 修复策略
- 保持策略: `裁裙边，不动 brim 主体`。
- 具体实现:
  - 将首层 `draft_shield` 的 skirt 裁剪逻辑扩展到 `stCombined`；
  - `stPerObject` 继续按当前对象 brim 裁剪；
  - `stCombined` 改为按全局 `m_brimMap + m_supportBrimMap` 汇总裁剪；
  - 对裁剪区做 `union + 去洞 + 轻微外扩`，减少 brim 内部残留裙边短线。

## 7. 为什么保持“裁裙边、不动 brim 主体”
- `brim` 是附着稳定性主功能，且手绘 brim 是用户显式指定的“必须加强区域”。
- `skirt/draft shield` 是外围辅助功能，应优先避让主功能路径。
- 若裁掉 brim，会直接削弱用户手绘耳朵的附着意图，属于功能语义反转。
- 从工程风险看，裁裙边只在 G-code 输出阶段做局部裁剪，改动集中、回归范围小；裁 brim 需要改 brim 面域生成链路，影响更广。

## 8. 代码改动摘要
- 文件: `src/libslic3r/GCode.cpp`
- 函数: `GCode::generate_skirt(...)`
- 修改点:
  - 放宽 `trim_first_layer` 条件，使其在 `draft_shield` 首层统一生效（不再仅限 `stPerObject`）；
  - 当 `object_for_brim == nullptr`（combined 路径）时，汇总全局 brim 区域参与裁剪；
  - 裁剪前对 `brim_polys` 执行 `union_ex`，清除 holes，并按线宽做小幅外扩后再 `diff_pl`。

## 9. 验证清单
- [ ] `Painted brim + draft shield + combined skirt`：不再与 brim 重叠。
- [ ] brim 内部不再出现零碎裙边残段。
- [ ] `stPerObject` 场景行为保持正确。
- [ ] 非 draft shield 场景无回归。
- [ ] `btAutoBrim / btOuterOnly / btOuterAndInner` 等 brim 类型无明显副作用。

## 10. 风险与回滚
- 回滚方式: 回退 `src/libslic3r/GCode.cpp` 中 `generate_skirt(...)` 的裁剪逻辑改动。
- 风险等级: `低`
- 关注点:
  - 极端窄缝区域下，外扩系数可能影响 skirt 局部连贯性；如有需要可调小外扩参数。
