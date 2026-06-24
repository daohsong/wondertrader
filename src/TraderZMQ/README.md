# TraderZMQ 交易适配器

`TraderZMQ` 是 WonderTrader 的交易通道适配器，用于通过 ZMQ 与 Windows 端桥接服务通信。它负责把 WT 引擎中的委托、撤单和查询请求发送到桥接服务，并接收来自 miniQMT / XtQuant 的异步委托回报、成交回报和错误回报。

插件导出工厂为 `createTrader()`，C++ 类名为 `TraderZMQ`，动态库名称为 `libTraderZMQ.so`。

## 运行拓扑

```text
WonderTrader 引擎
  -> TraderZMQ REQ socket
  -> Windows 端交易桥接服务
  -> XtQuant / miniQMT

XtQuant 回调
  -> Windows 端交易桥接服务
  -> TraderZMQ SUB socket
  -> WT onPushOrder / onPushTrade / onTraderError
```

交易通道使用两类 ZMQ 连接：

| 通道 | 方向 | 说明 |
| --- | --- | --- |
| 交易请求 REQ/REP | `TraderZMQ -> Windows 端桥接服务` | 发送委托、撤单和查询请求，并等待 ACK |
| 交易回报 PUB/SUB | `Windows 端桥接服务 -> TraderZMQ` | 接收委托状态、成交和错误回报 |

端口由 WT 配置显式指定。示例环境常用 `15557` 作为交易请求端口、`15558` 作为交易回报端口；生产部署时应按实际环境配置，避免与其它桥接服务冲突。

## 构建与部署

在项目根目录执行：

```bash
cmake --build wondertrader/src/build_gcc15 --target TraderZMQ -j2
```

构建产物通常位于：

```text
wondertrader/src/build_gcc15/build_x64/Release/bin/libTraderZMQ.so
```

WT Python 包运行时通常从 Linux wrapper 的 trader 插件目录加载：

```text
wtpy/wtpy/wrapper/linux/traders/libTraderZMQ.so
```

## 配置字段

`TraderZMQ::init()` 从 WT 配置节点读取以下字段：

| 字段 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `host` | 是 | 无 | Windows 端桥接服务主机，例如 `<Windows桥接主机>` |
| `req_port` | 是 | 可使用旧别名 `port` | 交易请求 REQ/REP 端口 |
| `pub_port` | 是 | 无 | 交易回报 PUB/SUB 端口 |
| `account_id` | 是 | 无 | QMT 资金账号，文档示例中统一写作 `<QMT账号>` |
| `ack_timeout_ms` | 否 | `5000` | 交易请求等待 ACK 的超时时间 |
| `sub_timeout_ms` | 否 | `1000` | 回报 SUB socket 接收超时时间 |
| `probe_login` | 否 | `false` | 登录阶段是否做只读账号探测 |
| `stats_enable` | 否 | `false` | 是否输出 JSONL 运行遥测 |
| `stats_path` | 否 | `/tmp/<运行标识>_trader.jsonl` | 遥测输出文件路径 |
| `case_id` | 否 | 内置默认值 | 运行标识，建议生产和测试都显式配置 |
| `order_ack_warn_ms` | 否 | `5000` | 委托/撤单 ACK 往返耗时告警阈值 |
| `trade_update_warn_ms` | 否 | `10000` | 回报延迟告警阈值 |

示例配置：

```json
{
  "host": "<Windows桥接主机>",
  "req_port": 15557,
  "pub_port": 15558,
  "account_id": "<QMT账号>",
  "ack_timeout_ms": 5000,
  "sub_timeout_ms": 1000,
  "probe_login": true,
  "stats_enable": true,
  "stats_path": "<统计输出目录>/<运行标识>_trader.jsonl",
  "case_id": "<运行标识>"
}
```

## 请求与回报映射

`TraderZMQ` 会向 Windows 端桥接服务发送以下请求：

| WT 动作 | ZMQ 请求类型 | 预期 ACK 类型 |
| --- | --- | --- |
| `orderInsert()` | `order_req` | `order_ack` |
| `orderAction()` | `cancel_req` | `cancel_ack` |
| `queryAccount()` | `account_req` | `account_ack` |
| `queryPositions()` | `positions_req` | `positions_ack` |
| `queryOrders()` | `orders_req` | `orders_ack` |
| `queryTrades()` | `trades_req` | `trades_ack` |
| `querySettlement()` | `settlement_req` | `settlement_ack` |

Windows 端桥接服务会发布以下回报：

| 回报类型 | WT 回调 |
| --- | --- |
| `order_update` | `onPushOrder()` |
| `trade_update` | `onPushTrade()` |
| `order_error` | 必要时生成错误委托，并触发 `onTraderError()` |

适配器使用 `req_id`、`user_tag` 和 `order_id` 维护请求与回报之间的映射。即使回报晚于 ACK 到达，也会尽量还原到对应的 WT 委托上下文。

## 日志标记

常见 `TraderZMQ` 日志标记如下：

| 标记 | 级别 | 含义 |
| --- | --- | --- |
| `req_send` | INFO | WT 已序列化并发送 ZMQ 请求 |
| `req_ack` | INFO/WARN | 桥接服务已返回 ACK；超过阈值时为 WARN |
| `req_timeout` | ERROR | `ack_timeout_ms` 内未收到 REP |
| `req_zmq_error` | ERROR | ZMQ 发送或接收异常 |
| `order_update` | INFO | 收到并分发委托回报 |
| `trade_update` | INFO | 收到并分发成交回报 |
| `order_error` | ERROR | 桥接服务或 XtQuant 返回委托/撤单失败 |

排查跨端问题时，优先使用 `req_id`、`event_id`、`order_id`、`user_tag`、`bridge_seq` 对齐 Linux 端和 Windows 端日志。

## 遥测

当 `stats_enable=true` 时，JSONL 中会记录以下事件：

- `request_ack`
- `request_timeout`
- `order_update`
- `trade_update`
- `order_error`

关键延迟字段：

- `round_trip_ms`：Linux 端发送请求到收到 ACK 的耗时。
- `cross_machine_latency_ms`：Windows 端发布事件到 Linux 端收到事件的耗时。
- `win_req_rx_ts_ns`、`win_ack_pub_ts_ns`、`win_event_pub_ts_ns`：Windows 端采集的时间戳。

## 运行前检查

上线或实盘测试前建议确认：

1. Windows 端交易桥接服务已监听配置的 `req_port` 和 `pub_port`。
2. 只读账号和持仓查询能正常返回。
3. WT 配置、桥接服务和遥测文件使用一致的 `case_id`。
4. 压测或实盘前确认没有遗留未完成委托。
5. 实盘委托测试期间保持 miniQMT 可见，并确保可以人工介入。

连通性检查示例：

```bash
nc -zv -w 3 <Windows桥接主机> 15557
nc -zv -w 3 <Windows桥接主机> 15558
```

只读探测脚本如在当前环境保留，可按实际路径和参数执行：

```bash
<python解释器> <只读探测脚本> \
  --windows-host <Windows桥接主机> \
  --account-id <QMT账号> \
  --case-id <运行标识> \
  --timeout-ms 5000 \
  --out <输出报告路径>
```

## 排障指引

| 现象 | 优先检查点 |
| --- | --- |
| `TraderZMQ` 出现 `req_timeout` | Windows 端是否收到对应 `REQ` |
| Windows 端收到请求但没有返回 REP | 桥接服务处理逻辑或 XtQuant 调用是否阻塞 |
| Windows 端已返回 REP 但 Linux 超时 | 网络、ZMQ REQ socket 状态、端口配置 |
| `order_ack` 成功但没有委托回报 | 回报 PUB/SUB 通道、`order_update`、WT 回调映射 |
| 已成交但没有成交回报 | Windows 端 `trade_update` 发布和 `TraderZMQ` 回报日志 |
| 市价单或撤单被拒 | miniQMT / 交易所拒单原因，通常应体现在 ACK 拒绝或 `order_error` |

原则上不要通过修改 WonderTrader 核心来掩盖适配器错误。临时核心日志可以用于诊断，但生产修复应落在 `TraderZMQ`、行情适配器或 Windows 端桥接服务中。
