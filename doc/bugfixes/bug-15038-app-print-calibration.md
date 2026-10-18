# Bug 修复记录

## 1. 基本信息
- Bug ID: `15038`
- 标题: `App 打印校准选项状态异常（未持久化）`
- 日期: `2026-03-11`
- 提交人:
- 处理人:
- 分支/提交:
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-15038.html`

## 2. 现象
- 发送打印页面中的 `print_calibration`（打印校准）用户操作状态在关闭/重开后不能稳定保留。
- 用户期望上次选择可被记忆，实际存在重置或不一致。
- 影响: 增加重复操作，且可能导致与用户预期不一致的打印行为。

## 3. 影响范围
- 模块: `发送打印（SendToPrinter）`
- 关键文件:
  - `src/slic3r/GUI/print_manage/App/SendToPrinter.cpp`
  - `src/slic3r/GUI/print_manage/App/SendToPrinter.hpp`

## 4. 修复前复现步骤
1. 打开发送打印页面，调整 `print_calibration` 状态。
2. 关闭发送打印窗口或重进该流程。
3. 观察 `print_calibration` 状态。
4. 结果: 状态可能未按上次操作保留。

## 5. 根因分析
- 原有前后端命令通道未提供用户操作状态的持久化读写能力。
- `print_calibration` 缺少本地缓存落盘与恢复逻辑，页面重建时无法恢复用户最近选择。

## 6. 修复策略
- 新增两条命令处理链路:
  - `save_user_operation_state`
  - `request_user_operation_state`
- 在 `SendToPrinter` 中引入本地状态文件持久化:
  - 路径: `data_dir()/cache/send_to_printer/user_operation_state.json`
- 读写时对 `print_calibration` 做容错与归一化:
  - 支持 `bool/int` 输入
  - 统一收敛为 `0/1`
  - 默认值为 `1`
- 通过 `window.handleStudioCmd(...)` 回传保存结果与当前状态，保证前端可同步状态。

## 7. 代码变更摘要
- 文件: `src/slic3r/GUI/print_manage/App/SendToPrinter.cpp`
  - 新增命令注册与反注册:
    - `save_user_operation_state`
    - `request_user_operation_state`
  - 新增状态文件路径函数:
    - `get_user_operation_state_file_path()`
  - 新增持久化函数:
    - `save_user_operation_state(const nlohmann::json&)`
    - `load_user_operation_state() const`
  - 新增命令处理函数:
    - `handle_save_user_operation_state(...)`
    - `handle_request_user_operation_state(...)`
- 文件: `src/slic3r/GUI/print_manage/App/SendToPrinter.hpp`
  - 补充以上新增函数声明。

## 8. 验证清单
- [ ] 首次进入发送打印页，`print_calibration` 默认值为 `1`。
- [ ] 修改 `print_calibration` 后触发 `save_user_operation_state`，返回 `success=true`。
- [ ] 关闭并重开发送打印页，触发 `request_user_operation_state`，状态与上次一致。
- [ ] 手工将状态写成 `bool/int` 混合值，读取后均可归一化为 `0/1`。
- [ ] 删除 `user_operation_state.json` 后，系统能自动回退默认值且不崩溃。

## 9. 禅道信息快照
- 页面地址: `https://zentao.creality.com/zentao/bug-view-15038.html`
- 当前环境读取结果: 返回登录页（`用户登录 - 禅道`），未获取到可用 bug 详情字段。
- 待补充字段（登录后补齐）:
  - Bug 标题原文
  - 严重程度 / 优先级
  - 指派给 / 解决方案 / 影响版本
  - 复现步骤（禅道原文）

## 10. 回滚与风险
- 回滚方式: 回退 `SendToPrinter.cpp/.hpp` 中新增的用户状态命令与本地文件持久化逻辑。
- 风险等级: 低（改动集中在发送打印状态同步逻辑）。
- 关注点:
  - 状态文件损坏或非法内容时的容错是否符合预期。
  - 多次快速开关窗口下的状态覆盖顺序。
