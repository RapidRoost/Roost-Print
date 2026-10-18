# 需求 0439：低速打印恢复正常速度后的外观缺料缓解（AUE 加速度/速度恢复缓冲）

## 基本信息

- 需求编号：`layer_improvement_0439`
- 代码基线：分支 `feature/layer_lmprovement`
- 关联提交：`90bfacc9724f5aec24212da93d08edec4fb7635b`（2026-02-06，lihaoxian）`feat:GUI高亮 +  开放三个参数 +  缺料段降低加速度。`
- 适用固件：**仅 Klipper**（依赖 `SET_VELOCITY_LIMIT`）
- 生效范围：仅在识别到 ROI（Trigger/Defect）且 Defect 角色属于外观敏感路径时，对对应路径插入/重写 G-code
- 默认状态：关闭（高级参数）
- 互斥功能：`Extrusion rate smoothing`（`max_volumetric_extrusion_rate_slope`）

## 问题现象

- 典型场景：悬垂/过桥等区域触发“悬垂降速/冷却降速”，**连续低速挤出**持续一段时间；当悬垂结束后恢复到外墙正常（相对更高）速度时，外观出现**短距离缺料/发白/层纹突变**。
- 缺陷位置：往往集中在“低速结束后的若干毫米路径”，尤其是外墙、悬垂外墙，以及拐角后速度段较短的区域。
- 机理假设：长时间低速后，固件 planner 在“低速→高速”的恢复阶段产生瞬态速度/加速度变化，挤出系统（供料、压力建立/释放、PA smooth 等）难以瞬时匹配，导致短时欠挤出外观。

## 修改方案

本改动分为两部分：
1) **ROI 检测**：在切片端定位 Trigger（低速触发段）与 Defect（恢复阶段外观敏感段）。  
2) **G-code 后处理缓解**：在 ROI 周围插入 Klipper 控制命令，并在 Defect 前段清理遗留设速，形成“安全缓冲距离”，最后恢复真实速度/加速度。

### 1. ROI（Trigger/Defect）检测（`InterestRegion`）

该 ROI 检测逻辑被设计为“可复用”：  
- GUI 预览高亮：在切片导出时基于 `GCodeProcessorResult::moves` 做 ROI 检测并缓存；  
- G-code 后处理：在单层 G-code 解析得到的 `motions[]` 上复用同一套检测函数，避免两处默认值漂移。

#### 架构抽象：感兴趣对象（`InterestObject`）与外观缺料对象（`AppearanceUnderExtrusionInterestObject`）

结合代码实现（`src/libslic3r/GCode/InterestRegion.hpp`），ROI 的程序结构不是直接暴露“Trigger/Defect 两段”，而是抽象为“感兴趣对象（InterestObject）”体系：

- 抽象基类：`InterestRegion::InterestObject`
  - 设计为 **抽象类**，通过虚函数暴露：
    - `type_name()`：兴趣对象的类型名（用于后续消费端按类型筛选）
    - `spans()`：返回该兴趣对象包含的路径段集合（`std::vector<TaggedSpan>`）
- 统一的 span/tag 表达：
  - `SegmentSpan`：用 **end-ssid**（端点序号）表达的段范围，表示从 `(end_ssid-1) -> end_ssid` 的那条“段”，因此 `first_end_ssid >= 1` 才有意义
  - `SegmentTag`：段的语义标签（`None/Trigger/Defect`）
  - `TaggedSpan`：`{ SegmentSpan, SegmentTag }` 绑定，用于表达一个兴趣对象由哪些段组成、每段是什么语义
- 容器：`InterestRegion::InterestRegion`
  - 保存 `std::vector<std::unique_ptr<InterestObject>> objects`，即“一个 ROI 区域可以由多个兴趣对象组成”
  - 提供 `apply_to_mask()` 将 spans 映射为一个按 ssid 索引的 mask（GUI 高亮/缓存时会用到）
  - 消费端（GUI/后处理）只需要依赖 `type_name()` + `spans()` 即可完成渲染/规划，达到“检测逻辑”和“应用逻辑”解耦

在上述抽象之上，本需求对应的具体实现是一个派生类：

- 外观缺料感兴趣对象：`InterestRegion::AppearanceUnderExtrusionInterestObject final : public InterestObject`
  - 一个实例由 **两类路径段**组成：
    1) `Trigger`：诱发外观缺料风险的“连续低速挤出段”
    2) `Defect`：紧随 Trigger 之后的“恢复阶段外观敏感段”
  - 在检测实现中，Defect 的起点按语义定义为：`effect_first_end = trigger.last_end_ssid + 1`，即 **Defect 必须从 Trigger 的下一段开始**（若不满足 Defect 约束条件则该 trigger 不产出兴趣对象）。

#### Trigger 段（低速触发段）

触发目的：不直接找“哪里缺料”，而是找“**连续低速挤出持续了一段时间**”这一类状态前提；只要出现该前提，就认为后续恢复阶段存在瞬态缺料风险。

默认判定（按连续挤出段扫描）：
- 只看挤出运动段（`extruding == true`）。
- 速度 `v <= max_trigger_speed_mm_s` 视为“低速段”，默认 `max_trigger_speed_mm_s = 35 mm/s`。
- 连续低速段内累计时间 `dt ≈ length / v`，当累计 `time >= min_trigger_time_s` 则该连续段判定为 Trigger span，默认 `min_trigger_time_s = 0.2 s`。

#### Defect 段（恢复阶段外观敏感段）

Defect 目的：抓住“低速结束后开始恢复速度”的敏感路径，作为后处理的主要作用区间。

默认判定（从 Trigger 末端的下一段开始向后扩展）：
- 起点：Trigger 的末端之后的第一段挤出运动（`effect_first_end = trigger.last_end + 1`）。
- 约束条件（持续满足才继续扩展）：
  1. 仍为挤出段；
  2. 速度已脱离低速区间：`v >= max_trigger_speed_mm_s`（避免与 Trigger 重叠）；
  3. 角色 `role` 属于 `defect_roles`，默认仅包含：
     - 外墙：`erExternalPerimeter`
     - 悬垂外墙：`erOverhangPerimeter`
  4. 默认限制在同一层（`restrict_to_same_layer = true`，以 Z 作为约束）。
- 终止：任一条件不满足即停止。
- Span 长度上限（cap）：
  - 默认 `defect_span_cap = 20mm`；
  - 本改动引入**动态 cap**：当可获取到本对象的 `accel_safe` / `velocity_safe` 时，cap 基于本次安全段长度推导：
    - `L_safe_accel = (v_safe^2 - v_low^2) / (2 * accel_safe)`（仅当 `v_safe > v_low`）
    - `L_safe_total = L_safe_transition + L_safe_accel + L_safe_cruise`
    - `cap = L_safe_total + margin`（默认 `margin = 1mm`）
  - 意义：确保 Defect span **足够覆盖后处理需要生效的距离**，同时尽量缩小影响范围。

### 2. 缓解动作（G-code 后处理：`AppearanceUnderExtrusionAccelRecoveryFilter`）

该模块是单层 G-code 字符串级别的后处理：解析→ROI 检测→生成计划→重写/插入命令。

#### 2.1 核心思路

- 在 Trigger 起点 **提前降低加速度上限**，让后续从低速恢复时的加速过程更可控；
- 在 Defect 的前段走过一段“安全缓冲距离”（`L_safe_total`）期间：
  - 维持 `accel_safe`
  - 先保持原低速 `v_low` 走 `L_safe_transition`，随后将巡航速度 cap 到 `velocity_safe`
  - 清理该区间内的所有遗留设速（F）指令，保证 safe 段速度只受我们插入的指令控制
- 在 boundary（恢复边界）处恢复真实加速度/真实速度，并保证最迟在 Defect 结束时恢复回来。

#### 2.2 安全段长度模型与恢复边界

后处理在 Defect span 内构造 Plan：
- `v_low`：Trigger 末端 motion 的速度（`trigger_last.feedrate_mm_s`）
- `v_safe` / `a_safe`：用户参数（`msao_safe_velocity` / `msao_safe_accel`）
- `v_recovered`：Defect 末端“真实要恢复到的速度”（`defect_last.feedrate_mm_s`，即该段最后一次生效的设速）
- `L_safe_transition_mm`：默认 1mm（内部常量，未开放 UI）
- `L_safe_cruise_mm`：默认 1mm（内部常量，未开放 UI）
- `L_safe_accel`：按常加速度理论距离估算（用于确定“理想需要多长缓冲路径”）
- `L_safe_total = L_transition + L_accel + L_cruise`
- boundary：沿 Defect 路径累计几何长度，首次达到 `L_safe_total` 的位置
  - 若落在单条运动内，记录 fraction 并对该条 G1/G2/G3 做 split；
  - 若 Defect 距离不足，则 boundary 夹到 Defect 末端（保证恢复一定发生）。

#### 2.3 G-code 注入与重写规则（Klipper）

在一个 ROI 计划内，后处理会插入/重写：
- Trigger 开始处（插入到对应行之前）：
  - `SET_VELOCITY_LIMIT ACCEL=<accel_safe>`
- transition_end 处（插入 `G1 F...`，仅设速不带 XYZ/E）：
  - `G1 F(<velocity_safe_mm_s>*60)`
- safe 段内（从 Defect 起点到 boundary，含必要的向前扩展行）：
  - **移除**所有 `G0/G1/G2/G3` 行上的 `F` token（含 `G1F...`）
  - 对纯设速行 `G1 F...`：在 safe 段内会被清理（必要时整行删除，保留注释行）
  - 原因：避免 CoolingBuffer 等模块在同一区间注入设速，破坏 safe 段“只受 `accel_safe` + `v_safe` 控制”的约束
- boundary 处恢复（可能在行后插入，也可能在 split 点插入）：
  - `SET_VELOCITY_LIMIT ACCEL=<accel_recovered>`
  - `G1 F(<velocity_recovered_mm_s>*60)`

split 细节（实现已覆盖）：
- 支持 `G1` 直线与 `G2/G3`（I/J）圆弧的按比例切分；
- E 轴按几何比例切分（相对/绝对 E 均支持）；
- 分段输出的运动指令**不携带 F**，速度控制完全由插入的 `G1 F...` 生效。

#### 2.4 Tag 标签（便于排查）

默认会在插入命令与分段 move 上追加注释（以 `AUE` 开头），例如：
- `; AUE accel_safe / AUE velocity_safe / AUE accel_recovered / AUE velocity_recovered`
- `; AUE transition(part|arc)`、`; AUE safe(part|arc)`
并在 Defect span 末尾追加 `; defect span end`（仅注释）。

### 3. 参数开放与互斥规则

#### 3.1 UI/配置参数（新增 3 个）

位置：打印设置 → 高级（Advanced）

- `msao_recovery_enable`（bool，默认 `false`）：启用 AUE 加速度/速度恢复缓冲
- `msao_safe_accel`（float，单位 `mm/s²`，默认 `200`，最小 `1`）：安全加速度上限
- `msao_safe_velocity`（float，单位 `mm/s`，默认 `50`，最小 `1`）：安全速度上限

说明：
- 参数为 **per-object overrideable**（按对象/实例 `label_object_id` 生效）；后处理通过 `; OBJECT_ID: <id>` 注释标记识别对象归属。
- UI 交互：仅当 `msao_recovery_enable == true` 时，才在面板中显示 `msao_safe_accel` / `msao_safe_velocity` 两个参数（参考“悬垂速度-经典模式”的参数展开方式）。

#### 3.2 与 Extrusion rate smoothing 的互斥

为避免两个“平滑类算法”叠加产生不可控效果，本改动在三处做了互斥约束：
- UI（`Tab.cpp`）：遵循“最后一次修改优先”的原则联动关闭另一项
- 预设归一化（`Preset::normalize`）
- 配置应用（`Print::apply`）

规则：
- 当 `max_volumetric_extrusion_rate_slope != 0` 时：强制 `msao_recovery_enable = false`
- 当 `msao_recovery_enable == true` 时：强制 `max_volumetric_extrusion_rate_slope = 0`

### 4. GUI 高亮（ROI 预览）

目的：让算法“尽量缩小影响范围”的原则可视化，便于用户/开发确认被影响的路径。

实现要点：
- 切片导出时在 `GCode::do_export()` 基于 `GCodeProcessorResult::moves` 检测 ROI，并将每个 move 的 tag 缓存到：
  - `GCodeProcessorResult::custom_interest_by_move_id`
- 预览窗口新增颜色模式 `Custom/自定义`：
  - 非 ROI 段：保持原耗材/工具颜色
  - ROI Trigger/Defect 段：使用与耗材颜色对比度更高的两种高亮色（自动挑选）
  - legend 中展示每个挤出头的“耗材色/ROI Trigger/ROI Defect”说明
- ROI 缓存仅在“切片生成预览（3mf workflow）”存在；直接打开 `.gcode` 文件时不会在线计算 ROI，因此不显示 `Custom` 模式。

#### 4.1 2026-03-15 补充（B 方案：默认屏蔽 `Custom/自定义` 视图）

目标调整为：
- **保留** `msao_recovery_enable` / `msao_safe_accel` / `msao_safe_velocity` 三个参数给用户使用；
- 在默认用户构建中，**不在 G-code 预览的颜色模式下拉框中暴露** AUE 的 `Custom/自定义` 视图（避免将开发诊断 UI 暴露给普通用户）。

实现方式：
- 顶层 `CMakeLists.txt` 新增编译开关：`SLIC3R_DEV_AUE_CUSTOM_PREVIEW`（默认 `OFF`）。
- `src/slic3r/CMakeLists.txt` 将该开关转换为 `libslic3r_gui` 的编译宏：`ENABLE_AUE_CUSTOM_PREVIEW=0/1`。
- 在 `src/slic3r/GUI/GCodeViewer.cpp` 中用 `#if ENABLE_AUE_CUSTOM_PREVIEW` 包裹 `Custom` 视图相关 GUI 逻辑（下拉项、ROI 高亮绘制、Legend 的 Custom 分支等）。

行为说明：
- `SLIC3R_DEV_AUE_CUSTOM_PREVIEW=OFF`（默认）：用户看不到 `Custom/自定义` 下拉项，AUE 后处理算法与三个参数功能不受影响。
- `SLIC3R_DEV_AUE_CUSTOM_PREVIEW=ON`（开发构建）：恢复 `Custom/自定义` 视图，便于算法调试与可视化排查。

## 代码改动摘要

- `src/libslic3r/GCode/InterestRegion.hpp`：新增 ROI 抽象（`InterestObject`）及其派生 `AppearanceUnderExtrusionInterestObject`；提供 span/tag 数据结构与 MoveVertex/Motion 两种视图的统一检测入口；提供 `L_safe_total` 计算函数
- `src/libslic3r/GCode/AppearanceUnderExtrusionAccelRecoveryFilter.hpp` / `.cpp`：新增 AUE 单层 G-code 后处理（解析 motion、检测 ROI、生成 Plan、插入/重写/分割）
- `src/libslic3r/GCode.cpp`：
  - 在 `process_layers()` pipeline 中插入 `aue_accel_recovery` stage（`write_gcode` 之后、`fan_mover` 之前）
  - 在 `do_export()` 末尾缓存 ROI 到 `GCodeProcessorResult::custom_interest_by_move_id`，供 GUI 高亮使用
- `src/libslic3r/GCode/GCodeProcessor.cpp` / `.hpp`：
  - 解析 `; OBJECT_ID:` 注释标记，记录每个 move 的 `object_id_by_move_id`
  - 增加 `custom_interest_by_move_id` / `object_id_by_move_id` 字段并随结果传递
- `src/slic3r/GUI/GCodeViewer.cpp` / `.hpp`：
  - 新增 `EViewType::Custom` 与 ROI 高亮绘制逻辑（按 segment tag 分块上色）
  - 新增 legend 说明（耗材色/Trigger/Defect）
- `src/libslic3r/PrintConfig.cpp` / `.hpp`：新增 3 个配置项并挂到 Advanced
- `src/slic3r/GUI/Tab.cpp`、`src/libslic3r/Preset.cpp`、`src/libslic3r/PrintApply.cpp`：实现与 `max_volumetric_extrusion_rate_slope` 的互斥联动
- `src/libslic3r/CMakeLists.txt`：加入新源文件编译
- `CMakeLists.txt`（顶层，2026-03-15）：新增 `SLIC3R_DEV_AUE_CUSTOM_PREVIEW` 编译开关（默认 `OFF`）
- `src/slic3r/CMakeLists.txt`（2026-03-15）：消费顶层开关并下发 `ENABLE_AUE_CUSTOM_PREVIEW=0/1` 到 `libslic3r_gui`
- `src/slic3r/GUI/GCodeViewer.cpp`（2026-03-15）：以编译宏屏蔽/开放 `Custom` 预览相关 GUI 代码路径

## 验证清单

### 切片/G-code 验证

- Klipper 模式下启用 `msao_recovery_enable`，切片包含悬垂外墙的模型：
  - G-code 中能检索到 `SET_VELOCITY_LIMIT ACCEL=` 与 `AUE` 注释
  - Trigger 起点插入 `ACCEL=accel_safe`
  - Defect 前段 safe 区间内无遗留 `F` token（含 `G1F...` 与独立 `G1 F...`）
  - boundary 处存在 `ACCEL=accel_recovered` 与 `G1 F(v_recovered)`
  - Defect 不足长度时，恢复发生在 Defect 末端（不会“忘记恢复”）
- 非 Klipper（如 Marlin）：
  - 后处理应 no-op（不插入 `SET_VELOCITY_LIMIT`，G-code 不变）
- 关闭 `msao_recovery_enable`：
  - G-code 不应产生 AUE 相关插入/重写

### 多对象/按对象参数验证

- 多对象场景为不同对象设置不同 `msao_safe_accel` / `msao_safe_velocity`：
  - G-code 中在不同 `; OBJECT_ID:` 区间触发时使用对应对象的 safe 参数（尤其是 defect_cap 与插入指令的值）

### GUI 预览验证

- 默认构建（`SLIC3R_DEV_AUE_CUSTOM_PREVIEW=OFF`）：
  - G-code 预览颜色模式下拉不显示 `Custom/自定义`
  - `msao_recovery_enable` / `msao_safe_accel` / `msao_safe_velocity` 仍可在参数面板正常配置
- 开发构建（`SLIC3R_DEV_AUE_CUSTOM_PREVIEW=ON`）下，切片生成预览后在 G-code 预览颜色模式选择 `Custom/自定义`：
  - Trigger/Defect 段正确高亮且 legend 显示三类颜色说明
  - 仅顶层/层范围过滤时，高亮不应“穿透”到被中和的层
- 直接打开 `.gcode` 文件：
  - 不显示/不可选 `Custom` 模式（ROI 不在线计算）

### 互斥验证

- 打开 `msao_recovery_enable` 后，`max_volumetric_extrusion_rate_slope` 自动置 0
- 将 `max_volumetric_extrusion_rate_slope` 设为非 0 后，`msao_recovery_enable` 自动关闭
- 导入/切换预设、应用配置时互斥仍成立（非仅 UI 侧）

## 风险与回退

### 风险

- safe 段内的 `F` token 清理会覆盖同区间内其他模块的设速意图（该行为是设计使然，但需要关注与 CoolingBuffer 等策略叠加时的边界表现）。
- ROI 检测依赖 extrusion role 与对象 ID 的注释标记；若上游未输出或被第三方后处理破坏，可能导致 ROI 识别不准或按对象参数不生效。
- `Seam` 相关：ROI 检测/GUI 高亮使用的 ssid 视图会显式过滤 `EMoveType::Seam`（与预览的 “seams removed” 语义一致），因此 seam 点本身不会被标记为 Trigger/Defect；若后续 seam 的生成/过滤逻辑变化导致 `ssid_to_moveid_map` 与缓存 mask 不一致，`Custom` 高亮可能出现错位/缺失。另外 Defect 段可能从外墙起点（seam 附近）开始，AUE 的限速/限加速度与设速清理可能改变 seam 附近的起挤外观，需要关注是否引入/放大 seam 处的可见瑕疵。
- 对 `G2/G3` 的 split 依赖 I/J 圆心；若某些固件/后处理链对圆弧指令兼容性较弱，需要额外验证。
- 加速度/速度上限会改变局部打印时间，可能影响局部冷却效果（需配合实际打印验证）。

### 回退

- 推荐回退（无需改代码）：关闭 `msao_recovery_enable`
- 降低影响（仍启用但弱化）：将 `msao_safe_accel` 提高、`msao_safe_velocity` 提高，使 safe 段更接近原设定
- 代码级回退：回滚提交 `90bfacc9724f5aec24212da93d08edec4fb7635b` 或移除 `GCode::process_layers()` 中的 `aue_accel_recovery` pipeline stage

## 备注

- ROI 检测参数（`max_trigger_speed_mm_s`、`min_trigger_time_s`、`defect_roles`、cap margin 等）当前为内部默认值，未开放 UI；若后续需要适配更多机型/材料，可考虑参数化。
- 代码中存在若干 TODO（如 overlap plan 的去重逻辑、Defect 起点判定边界等），可作为后续优化点。
- 当前实现默认开启 `AUE` 注释，便于排查；若后续担心 G-code 体积/可读性，可考虑通过开关控制 `emit_aue_comments`。
