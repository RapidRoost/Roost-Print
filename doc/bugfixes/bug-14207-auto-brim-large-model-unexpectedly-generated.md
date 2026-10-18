# Bug 修复说明

## 1. 基本信息
- Bug ID: `14207`
- 标题: `Auto Brim 在大底面低矮模型上仍默认生成（与 Bambu 行为不一致）`
- 日期: `2026-02-24`
- 反馈人: `江圣龙`
- 处理人: `wangwenbin`
- 分支/提交:

## 2. 问题现象
- 当 `Brim type = Auto` 时，导入大底面低矮模型（例如 `1800x1800x50` 方块）后，切片仍会生成 brim。
- 对比 `BambuStudio-02.04.00.70` 导入同一模型，在 `Auto` 下默认不生成 brim。
- 用户预期:
  - 大底面模型本身附着面积大，`Auto` 模式应倾向不加 brim（至少应与 Bambu 行为一致）。

## 3. 影响范围
- 模块: `Auto Brim 宽度计算逻辑`
- 关键文件:
  - `src/libslic3r/Brim.cpp`
- 受影响流程:
  - FDM 切片时 `brim_type = btAutoBrim` 的首层分组 brim 宽度计算
  - 尤其是“大底面 + 低高度”的模型（大平面/长对角线）

## 4. 复现步骤（修复前）
1. 导入大底面低矮模型（例如 `1800x1800x50` 方块）。
2. 将 `Brim type` 设置为 `Auto`。
3. 保持其他参数默认并切片。
4. 观察首层预览/G-code。
5. 结果: 仍生成 brim（与 Bambu 同模型 `Auto` 默认不加 brim 的表现不一致）。

## 5. 根因分析
- `Auto Brim` 并非简单按“模型大/小”决定，而是通过 `configBrimWidthByVolumeGroups(...)` 对每个首层分组计算自动宽度。
- 当前 C3D 版本的分组公式中，`brim_width` 取 `max(height_to_area * maxSpeed, thermalLength * ...)`：
  - `height_to_area * maxSpeed` 更偏向细高/稳定性风险。
  - `thermalLength * ...` 与首层对角线长度（热长度）相关，会放大“大平面”的翘边风险判断。
- 对于大底面低矮模型，虽然不易倾倒，但首层对角线很大，`thermalLength` 分支会显著抬高自动 brim 宽度，导致 `Auto` 仍生成 brim。
- 对比 BambuStudio-02.04.00.70，其分组公式中该热长度项在 `max(...)` 内被乘以 `0.`（等效禁用），因此大底面低矮模型更容易得到 `< 5mm` 的结果并被省略。

## 6. 修复策略
- 参考 BambuStudio-02.04.00.70 的分组自动 brim 逻辑，对齐 `configBrimWidthByVolumeGroups(...)` 的行为。
- 在分组自动 brim 宽度公式中关闭热长度项分支（保持公式结构不变，最小改动）。
- 保留其他自动 brim 行为不变：
  - `height_to_area * maxSpeed` 分支仍有效（细高/不稳定模型仍可能自动生成 brim）
  - 小 brim 省略阈值（`< 5mm`）与上限（`18mm`）逻辑保持不变

## 7. 代码改动摘要
- 文件: `src/libslic3r/Brim.cpp`
- 函数: `configBrimWidthByVolumeGroups(double adhesion, double maxSpeed, const std::vector<ModelVolume*> modelVolumePtrs, const ExPolygons& expolys, double &groupHeight)`
- 关键修改点:
  - 将分组公式中的热长度分支从
    - `thermalLength * 8. / thermalLengthRef * std::min(height, 30.) / 30.`
  - 调整为
    - `0. * thermalLength * 8. / thermalLengthRef * std::min(height, 30.) / 30.`
  - 效果等同于禁用该分支，行为与 BambuStudio-02.04.00.70 对齐

## 8. 验证清单
- [ ] `1800x1800x50` 方块在 `Brim type = Auto` 下切片后默认不生成 brim。
- [ ] 同模型在 `BambuStudio-02.04.00.70` 中与当前版本表现一致（Auto 不加 brim）。
- [ ] 细高模型（高宽比明显）在 `Auto` 下仍可正常生成 brim（无功能回退）。
- [ ] `Brim type = Outer/Inner/Outer+Inner/No-brim` 行为不受影响。
- [ ] `Brim type = Auto` 的小模型/附着风险模型结果无明显异常回归。

## 9. 风险与回滚
- 回滚方式: 回退 `src/libslic3r/Brim.cpp` 中 `configBrimWidthByVolumeGroups(...)` 的公式改动。
- 风险等级: `低`
- 重点关注副作用:
  - 某些“大底面但确实易翘边”的材料/模型在 `Auto` 下可能不再自动加 brim
  - 与 Bambu 对齐后，`Auto` 的结果更偏向几何稳定性（细高）而非大平面热收缩风险

## 10. 后续建议
- 将 `Auto Brim` 的热长度项开关做成可配置策略（例如“Bambu兼容模式”或高级开关）。
- 增加回归样例库：
  - 大底面低矮方块
  - 细高塔模型
  - 大平面高收缩材料（ABS/PC）
- 若后续需要更细粒度控制，可区分“防倾倒风险”和“防翘边风险”两类自动 brim 策略。
