# Bug 修复记录

## 1. 基本信息
- Bug 标识: `videoInfo.videoEncryption`
- 标题: `视频信息通道增加 videoEncryption 分支并修复回传参数处理`
- 日期: `2026-03-05`
- 提交人: 王昭
- 负责人: 王昭

## 2. 现象描述
- 设备管理页/发送打印流程请求 `get_webrtc_local_param` 时，原逻辑固定将本地 SDP 做 Base64 后直接回传。
- 对 HTTPS 视频服务场景，缺少明确的加密分支与证书配置，导致无法按预期完成服务端交换或返回信息不完整。
- 回传到前端的 JSON 字符串未统一编码，包含特殊字符时存在脚本参数解析风险。

## 3. 影响范围
- 模块: `远程打印 / 设备管理 / WebRTC 参数交换`
- 关键文件:
  - `src/slic3r/GUI/print_manage/App/SendToPrinter.cpp`
  - `src/slic3r/GUI/print_manage/Routes/DeviceMgrRoutes.cpp`
  - `src/slic3r/Utils/Http.cpp`
  - `src/slic3r/Utils/Http.hpp`
  - `resources/cert/ca.crt`
- 影响流程:
  - `get_webrtc_local_param` 路由处理
  - 发送打印弹窗内 WebRTC 本地参数处理

## 4. 修复前复现路径（Before Fix）
1. 前端发起 `get_webrtc_local_param`，携带 `url`、`sdp`（可选 `token`）。
2. 客户端仅构造 `offer` 并做 Base64，直接回传 `sdp=e`。
3. 当目标视频服务要求 HTTPS/TLS 通道交换时，客户端没有显式 TLS 控制与 CA 配置分支。
4. 出现协商失败或错误信息不足（前端难以区分是本地编码路径还是远端交换失败）。

## 5. 根因分析
- 缺少 `videoEncryption` 语义分支：原实现未区分“仅本地编码回传”与“发起 HTTPS 交换后回传结果”两种模式。
- `Http` 封装缺少可控 TLS 校验开关接口，调用侧无法按业务需要配置 `CURLOPT_SSL_VERIFYPEER` / `CURLOPT_SSL_VERIFYHOST`。
- JS 桥接调用 `window.handleStudioCmd(...)` 时未统一 URL 编码，特殊字符可能影响前端解析稳定性。

## 6. 修复策略
- 在两个入口（`SendToPrinter` 与 `DeviceMgrRoutes`）新增 `videoEncryption` 参数读取。
- 兼容策略：若请求未显式给出 `videoEncryption=true`，但 `url` 以 `https://` 开头，自动启用加密分支。
- 加密分支下：
  - 通过 `Http::post(url)` 同步请求远端。
  - 设置 `Content-Type: plain/text`，请求体为 Base64 后的 offer。
  - 指定 CA 文件 `resources/cert/ca.crt`。
  - 开启 `ssl_verify_peer(true)`，关闭 `ssl_verify_host(false)`。
  - 统一收集 `responseBody`、`responseError`、`responseStatus`。
- 非加密分支下保持兼容：继续回传本地 Base64 SDP。
- 回传前端时统一对 JSON 字符串做 `RemotePrint::Utils::url_encode(...)`。
- 在 `Http` 类中新增 TLS 校验控制 API：
  - `ssl_verify_peer(bool)`
  - `ssl_verify_host(bool)`

## 7. 代码变更摘要
- 文件: `src/slic3r/GUI/print_manage/App/SendToPrinter.cpp`
  - 读取 `videoEncryption`、`token`。
  - 按加密/非加密分支组织 `out_data`。
  - 加密分支增加 HTTPS 请求与证书配置。
  - JS 回传由原始 JSON 改为 URL 编码后注入。

- 文件: `src/slic3r/GUI/print_manage/Routes/DeviceMgrRoutes.cpp`
  - 同步引入 `videoEncryption` 分支逻辑与 HTTPS 请求流程。
  - 增加状态与错误信息回传字段（`status`、`error`）。
  - JS 回传改为 URL 编码字符串。

- 文件: `src/slic3r/Utils/Http.hpp`
  - 新增方法声明：`ssl_verify_peer(bool)`、`ssl_verify_host(bool)`。

- 文件: `src/slic3r/Utils/Http.cpp`
  - 新增方法实现并映射到 libcurl:
    - `CURLOPT_SSL_VERIFYPEER`
    - `CURLOPT_SSL_VERIFYHOST`

- 文件: `resources/cert/ca.crt`
  - 新增 CA 证书资源，供视频加密请求校验证书链。

## 8. 验证清单
- [ ] `videoEncryption=false` 且 `http://` URL：返回 `sdp` 为本地 Base64，流程与旧逻辑兼容。
- [ ] `videoEncryption=true` 且 HTTPS 可达：返回远端响应 SDP。
- [ ] 未传 `videoEncryption` 但 URL 为 `https://`：自动走加密分支。
- [ ] HTTPS 证书异常时：`status/error` 能正确回传并可用于前端提示。
- [ ] `token` 存在时正确透传到 offer JSON。
- [ ] 包含特殊字符的响应场景下，`window.handleStudioCmd(...)` 解析正常（URL 编码生效）。

## 9. 兼容性与风险
- 风险等级: `中低`（网络与证书策略相关，改动点集中在视频参数通道）。
- 主要风险:
  - `ssl_verify_host(false)` 放宽了主机名校验，需要确认是否符合产品安全基线。
  - 新增 CA 文件路径依赖 `resources_dir()`，需确保打包产物包含证书。
  - 同步网络请求可能受网络抖动影响响应时延。

## 10. 回滚方案
- 回滚 `SendToPrinter.cpp` 与 `DeviceMgrRoutes.cpp` 的 `videoEncryption` 分支逻辑。
- 回滚 `Http` 中新增的 TLS 控制接口。
- 移除或忽略 `resources/cert/ca.crt` 引用。

## 11. 后续建议
- 增加自动化用例覆盖以下场景：HTTPS 成功、证书失败、超时、无 `videoEncryption` 自动判定。
- 评估是否将 `ssl_verify_host(false)` 改为可配置项，按环境（开发/生产）区分策略。
- 对加密分支增加更细粒度错误码，提升前端可观测性与故障定位效率。
