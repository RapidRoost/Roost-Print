# Bug 修复记录模板

## 1. 基本信息
- Bug ID: `14897`
- 标题: `【用户反馈】登录账号后无响应`
- 禅道链接: `https://zentao.creality.com/zentao/bug-view-14897.html`
- 提交日期: `2026-01-29`
- 修复日期: `2026-02-25`
- 提交人: `康美樱`
- 处理人: `贺淼`
- 所属计划: `CP 7.1.0 Release`
- 分支/提交:

## 2. 问题现象
- 用户点击登录后，软件表现为无响应。
- 关闭应用后，进程可能未完全退出。
- 退出不彻底时会残留异常进程，继续占用锁文件。
- 后续再次启动或登录流程可能被影响。

## 3. 影响范围
- 模块: `后台同步`、`远程打印上传`、`云端通信`
- 关键文件:
  - `src/slic3r/GUI/print_manage/RemotePrinterManager.cpp`
  - `src/slic3r/GUI/print_manage/RemotePrinterManager.hpp`
  - `src/slic3r/GUI/SyncUserPresets.cpp`
  - `src/slic3r/GUI/SyncUserPresets.hpp`
  - `src/slic3r/GUI/CommunicateWithCXCloud.cpp`

## 4. 复现步骤（修复前）
1. 启动软件并登录账号，触发后台同步或上传任务。
2. 在任务进行中关闭软件。
3. 观察任务管理器，存在进程未退出或退出耗时很长。
4. 再次启动后可能出现登录无响应或资源锁定相关现象。

## 5. 根因分析
- `RemotePrinterManager` 与 `SyncUserPresets` 关键线程使用 `detach`，生命周期不可控。
- 退出阶段等待线程结束时，部分任务可能长时间阻塞，导致退出挂起。
- `CommunicateWithCXCloud` 部分请求缺少超时设置，网络异常时会放大阻塞问题。

## 6. 修复方案
- 线程生命周期治理:
  - 分离线程改为可 `join` 的受控线程。
  - 退出时统一执行: 置退出标志、停止接收任务、清队列、唤醒等待、`join` 收敛。
- 退出响应优化:
  - 长等待循环增加退出检查与取消逻辑。
  - 上传等待在退出态下设置最大等待窗口，避免无限等待。
- 网络稳定性增强:
  - 为缺失请求补齐统一超时策略 `timeout_connect(5)` + `timeout_max(15)`。

## 7. 代码改动摘要
### 7.1 `RemotePrinterManager`
- 上传线程由 `detach` 改为成员线程 `m_uploadThread`，析构时 `join`。
- `m_bExit` 改为原子变量，退出可见性更可靠。
- 析构流程增加任务队列清空与 `notify_all`，避免等待线程悬挂。
- 退出态下禁止继续入队。
- 上传等待循环中，退出时触发取消并设置最多 5 秒等待上限。

### 7.2 `SyncUserPresets`
- 移除 `m_thread.detach()`，改为 `startup/shutdown` 管理线程。
- `startup()` 重置停止状态并在启动前设置运行态。
- `shutdown()` 统一停止标志，等待退出信号并 `join`。
- 在关键循环（含 `isLoadingGCode` 等待）增加退出检查，提升退出响应。

### 7.3 `CommunicateWithCXCloud`
- 以下 5 处请求补齐超时:
  - `preUpdateProfile_create` 主请求
  - `preUpdateProfile_create` 上传 Host 请求
  - `preUpdateProfile_update` 主请求
  - `preUpdateProfile_update` 上传 Host 请求
  - `deleteProfile` 主请求

## 8. 验证清单
- [ ] 登录后触发同步任务，关闭应用，进程可在合理时间内完全退出。
- [ ] 上传任务进行中关闭应用，无残留进程。
- [ ] 弱网/断网场景下关闭应用，不出现长时间卡退出。
- [ ] 再次启动后登录流程正常，无“无响应”复现。
- [ ] 云端请求超时路径可在预期时间内返回。
- [ ] 正常登录/同步/上传流程无功能回归。

## 9. 风险与回退
- 风险等级: `中低`
- 可能影响:
  - 退出时进行中的任务可能更早被取消。
  - 退出开始后，未消费任务可能被丢弃。
- 回退方案:
  - 回退上述 5 个文件至本次修改前版本。

## 10. 关联说明
- 禅道标题: `BUG #14897 【用户反馈】登录账号后无响应`。
- 历史备注中存在“异常进程未正常退出，锁住文件”的记录，与本次修复方向一致。
