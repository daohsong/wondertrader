# ParserZMQ 行情解析器

`ParserZMQ` 是 WonderTrader 的行情解析适配器，用于通过 ZMQ 接收 Windows 端桥接服务转发的 miniQMT / XtQuant 实时 tick，并通过 WT 标准 parser 接口向策略层推送行情。

插件导出工厂为 `createParser()`，C++ 类名为 `ParserZMQ`，动态库名称为 `libParserZMQ.so`。

## 运行拓扑

```text
miniQMT xtdata
  -> Windows 端行情桥接服务
  -> ParserZMQ SUB socket
  -> WT parser sink
  -> WT 策略 tick 回调

WT subscribe / unsubscribe
  -> ParserZMQ control REQ socket
  -> Windows 端行情控制 REP socket
```

行情通道使用两类 ZMQ 连接：

| 通道 | 方向 | 说明 |
| --- | --- | --- |
| 行情控制 REQ/REP | `ParserZMQ -> Windows 端桥接服务` | 发送订阅、退订、清空订阅请求 |
| 行情 PUB/SUB | `Windows 端桥接服务 -> ParserZMQ` | 接收实时 tick JSON payload |

端口由 WT 配置显式指定或使用源码默认值。示例环境常用 `15555` 作为控制端口、`15556` 作为行情端口；生产部署时应按实际环境配置。

## 构建与部署

在项目根目录执行：

```bash
cmake --build wondertrader/src/build_all --target ParserZMQ -j2
```

构建产物通常位于：

```text
wondertrader/src/build_all/build_x64/Release/bin/libParserZMQ.so
```

WT Python 包运行时通常从 Linux wrapper 的 parser 插件目录加载：

```text
wtpy/wtpy/wrapper/linux/parsers/libParserZMQ.so
```

## 配置字段

`ParserZMQ::init()` 从 WT 配置节点读取以下字段：

| 字段 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `quote_host` | 否 | `host` 字段；若未配置则使用源码内置默认主机 | Windows 端行情 PUB 主机，建议显式配置为 `<Windows桥接主机>` |
| `quote_port` | 否 | `port` 字段或 `5556` | 行情 PUB/SUB 端口 |
| `ctrl_host` | 否 | `quote_host` | 行情控制 REP 主机 |
| `ctrl_port` | 否 | `5555` | 行情控制 REQ/REP 端口 |
| `ctrl_timeout_ms` | 否 | `5000` | 控制请求等待 REP 的超时时间 |
| `stats_enable` | 否 | `false` | 是否输出 JSONL 运行遥测 |
| `stats_path` | 否 | `/tmp/<运行标识>_parser.jsonl` | 遥测输出文件路径 |
| `case_id` | 否 | 内置默认值 | 运行标识，建议生产和测试都显式配置 |
| `latency_warn_ms` | 否 | `300` | 行情延迟告警阈值 |
| `latency_error_ms` | 否 | `1000` | 行情延迟错误阈值 |

示例配置：

```json
{
  "quote_host": "<Windows桥接主机>",
  "quote_port": 15556,
  "ctrl_host": "<Windows桥接主机>",
  "ctrl_port": 15555,
  "ctrl_timeout_ms": 5000,
  "stats_enable": true,
  "stats_path": "<统计输出目录>/<运行标识>_parser.jsonl",
  "case_id": "<运行标识>",
  "latency_warn_ms": 300,
  "latency_error_ms": 1000
}
```

## 订阅行为

`ParserZMQ` 在 ZMQ socket 层订阅全部 topic，实际的证券订阅集合由 Windows 端桥接服务通过控制通道维护。这样可以避免 WT 多线程调用 `subscribe()` / `unsubscribe()` 时频繁修改 SUB socket 选项。

WT `subscribe()` 会发送：

```json
{"action":"add","codes":["SSE.STK.600000"]}
```

WT `unsubscribe()` 会发送：

```json
{"action":"remove","codes":["SSE.STK.600000"]}
```

`disconnect()` 会发送：

```json
{"action":"clear","codes":[]}
```

代码会尽量使用 WT 基础数据把合约代码规范化。桥接服务通常使用 WT 风格完整代码，例如 `SSE.STK.600000`、`SZSE.STK.000001`、`SSE.CB.113000`。

## 行情 payload

解析器期望收到 JSON payload，核心字段包括：

- `type: "quote"`
- `case_id`
- `event_id`
- `code`、`exchg`，以及可选的 `full_code` / ZMQ topic
- `price`、`open`、`high`、`low`
- `volume`、`turnover`、`total_volume`、`total_turnover`
- `bid_price_1..5`、`bid_qty_1..5`、`ask_price_1..5`、`ask_qty_1..5`
- `trading_date`、`action_date`、`action_time`
- `win_callback_ts_ns`、`win_pub_ts_ns`、`bridge_seq`

如果 WT 基础数据中找不到合约或品种信息，该 tick 会被跳过。相关 warning 做了采样限频，避免全市场行情下日志快速膨胀。

## 日志标记

常见 `ParserZMQ` 日志标记如下：

| 标记 | 级别 | 含义 |
| --- | --- | --- |
| `connect quote_endpoint=... ctrl_endpoint=...` | INFO | 行情和控制 socket 已按配置初始化 |
| `control action ... ok` | INFO | 桥接服务接受订阅、退订或清空订阅请求 |
| `control action ... timeout` | WARN | 控制 REP 未在超时时间内返回 |
| `first msg` | INFO | 已收到第一帧行情 |
| `quote progress` | INFO | 周期性行情接收和分发统计 |
| `Instrument ... not exists` | WARN sampled | WT 基础数据缺少合约信息 |
| `Instrument ... has no commodity info` | WARN sampled | WT 基础数据缺少品种信息 |
| `recv error` | ERROR | ZMQ 接收异常 |

Windows 端行情日志通常包含控制请求、订阅结果、行情发布进度和发布错误。生产环境不建议打开逐 tick 普通日志；需要完整统计时应使用 JSONL 遥测。

## 遥测

当 `stats_enable=true` 时，JSONL 中会记录以下事件：

- `quote_rx`
- `parser_sink_dispatch`
- `parser_sink_dispatched`
- `parser_disconnect`
- `parser_stop_worker`

关键延迟字段：

- `cross_machine_latency_ms`：Windows 端发布行情到 Linux 端收到行情的耗时。
- `parser_dispatch_latency_ms`：Linux 端收到行情到派发给 WT parser sink 的耗时。
- `bridge_seq`：Windows 端桥接服务生成的序列号。
- `event_id`：行情事件稳定标识。

## 运行前检查

全市场或实盘行情测试前建议确认：

1. Windows 端行情桥接服务已监听配置的 `ctrl_port` 和 `quote_port`。
2. WT 合约文件包含测试标的或订阅股票池。
3. Parser、桥接服务和遥测文件使用一致的 `case_id`。
4. 先用小范围标的验证订阅和 tick 推送，再扩大到全市场。
5. 对齐 Windows 端发布数、Linux 端接收数、parser 分发数和策略 tick 数。

连通性检查示例：

```bash
nc -zv -w 3 <Windows桥接主机> 15555
nc -zv -w 3 <Windows桥接主机> 15556
```

## 排障指引

| 现象 | 优先检查点 |
| --- | --- |
| WT 没有 tick | Windows 端是否持续发布行情，`ParserZMQ` 是否出现 `first msg` |
| 订阅超时 | 控制端口、控制请求接收和控制响应发送 |
| 大量 tick 被跳过 | WT 合约和品种基础数据是否覆盖订阅标的 |
| Windows 发布数量多于 Linux 接收数量 | PUB/SUB topic、网络、高水位配置、Parser socket 状态 |
| Parser 已分发但策略没收到 | WT parser sink 和策略回调路径 |

解析器应保持轻量适配层定位。行情代码规范化、订阅控制和遥测问题应优先在 `ParserZMQ` 或 Windows 端桥接服务中修复，不建议通过修改 WonderTrader 核心行为绕过问题。
