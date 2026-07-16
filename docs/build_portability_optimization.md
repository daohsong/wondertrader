# WonderTrader 构建与头文件跨平台优化方案

## 目标

当前 `compile_warnings` 分支已经补入 macOS arm64 构建支持，但项目的 CMake 组织、第三方依赖发现、运行时插件部署、公共头文件封装仍偏向单一 Linux x86_64/GCC 环境。本文档的目标是让 WonderTrader 更稳定地适配以下组合：

- Linux x86_64 / Linux aarch64
- macOS arm64 / macOS x86_64，后续可扩展到 universal build
- Windows MSVC / MinGW 或 clang-cl
- C++17、C++20、C++23 多标准矩阵
- 系统依赖、Homebrew、Conan/vcpkg、自定义 `thirdparty` 前缀

本文只描述可验证的技术目标、实施边界和验收口径，不把分析过程或工具使用方式作为方案依据。

## 官方依据

这些资料是本文方案的主要 best practice 来源：

- CMake 推荐用 target usage requirements 表达依赖：[`target_compile_features`](https://cmake.org/cmake/help/latest/command/target_compile_features.html)、[`target_include_directories`](https://cmake.org/cmake/help/latest/command/target_include_directories.html)、[`target_link_libraries`](https://cmake.org/cmake/help/latest/command/target_link_libraries.html)。
- CMake imported/interface target 可表达外部库位置和传递属性：[`add_library(IMPORTED|INTERFACE)`](https://cmake.org/cmake/help/latest/command/add_library.html)。
- Apple 架构应由 `CMAKE_OSX_ARCHITECTURES` 控制：[`CMAKE_OSX_ARCHITECTURES`](https://cmake.org/cmake/help/latest/variable/CMAKE_OSX_ARCHITECTURES.html)。
- 线程和动态加载库应使用 CMake 提供的跨平台变量/target：[`FindThreads` / `Threads::Threads`](https://cmake.org/cmake/help/latest/module/FindThreads.html)、[`CMAKE_DL_LIBS`](https://cmake.org/cmake/help/latest/variable/CMAKE_DL_LIBS.html)。
- 构建树复制应使用 generator expressions 和 target 产物路径，并建立正确的 producer/consumer 依赖：[`$<TARGET_FILE:...>`](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html)、[`add_custom_command`](https://cmake.org/cmake/help/latest/command/add_custom_command.html)、[`cmake -E copy_if_different`](https://cmake.org/cmake/help/latest/manual/cmake.1.html)。
- RPATH 和运行时依赖收集应由 CMake 安装/打包阶段统一处理：[`BUILD_RPATH`](https://cmake.org/cmake/help/latest/prop_tgt/BUILD_RPATH.html)、[`install(RUNTIME_DEPENDENCY_SET)`](https://cmake.org/cmake/help/latest/command/install.html)。
- 自动化验证应接入 CTest，并通过 preset 固化常用配置：[`enable_testing`](https://cmake.org/cmake/help/latest/command/enable_testing.html)、[`add_test`](https://cmake.org/cmake/help/latest/command/add_test.html)、[`cmake-presets`](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)。
- Boost.Asio 新旧 executor API 差异适合收敛到兼容层：[`executor_work_guard`](https://www.boost.org/doc/libs/1_89_0/doc/html/boost_asio/reference/executor_work_guard.html)、[`post`](https://www.boost.org/doc/libs/1_89_0/doc/html/boost_asio/reference/post.html)。
- `std::atomic_ref` 是 C++20 设施，且有对齐和 lock-free 约束，应由统一兼容层处理：[`atomic_ref` draft wording](https://eel.is/c++draft/atomics.ref.generic)。

## 总体结论

WonderTrader 可以把公共 API 和核心源码的最低标准设为 C++17，但“最低标准”和“本次构建选择的标准”必须分开表达：

- 各 target 用 `target_compile_features(... cxx_std_17)` 声明最低要求；公共头使用 C++17 时以 `PUBLIC/INTERFACE` 传播，只有实现使用时用 `PRIVATE`。
- `WT_CXX_STANDARD` cache 变量只负责在项目构建和 CI 中选择 `17/20/23` 的精确验证模式，必须校验输入值；确认全量默认组件通过后，默认值应从 `23` 调整为 `17`。
- C++20/23 能力只通过 feature-test macro 或 target 级 feature gate 开启，不允许因为单个实现文件抬高全项目标准。
- `atomic_ref` 等 C++20 优化必须明确两种策略之一：不满足 lock-free/对齐约束时回退到受支持的平台原语，或在 configure 阶段明确拒绝该平台；不能在文档中承诺 fallback、实现中却直接 `static_assert` 失败。

当前阻碍跨平台/跨标准适配的核心不是单个 warning，而是“构建状态全局化”和“平台差异分散到业务代码”。应优先建立 CMake target 依赖模型和少量公共兼容头，再逐步迁移各模块。

## 方案评审结论与实施基线决策

### 评审结论

方案的总体方向合理，但当前版本不能按原顺序直接继续扩大改动。必须先修正以下实施约束：

- 先把测试改成可重复、非交互、可由 CTest 驱动，再把“本机曾通过”升级为验收证据。
- 依赖 target 化必须消除目录级和递归全局注入，不能只是给原来的全局变量套一层 alias。
- output copy 只作为构建树开发便利；install/package 才是部署的唯一权威入口。
- `ShmWireLayout` 与 `AtomicCompat` 必须作为同一个高风险切片设计和验证，不能先改原子访问、以后再补 wire layout 对齐契约。
- 正式支持、仅编译验证和未来目标必须分层，不能把目标列表直接等同于已支持矩阵。

### 当前 Git 状态

审计时当前分支为 `compile_warnings`，`HEAD` 已经是 `9763b70c69b162f0cc9d8970fc40993f00c7c167`。工作区相对该提交有超过 150 个 tracked 文件变更，另有兼容头、测试、策略脚本和构建目录等 untracked 内容；这些变更混有 portability、compile-warnings、测试和可能的其他本地工作，不能假定全部属于同一方案或都可以丢弃。

因此：

- 直接执行 `git reset --hard 9763b70...` 并不是“退回后重新开始”，而是删除当前所有 tracked 工作区成果；如果再清理 untracked 文件，还会删除新增兼容头和测试。
- 在当前脏工作区上继续追加新切片也不合理：变更边界过大，无法可靠 review、bisect、回滚或证明每个切片独立通过。
- 推荐策略是保留当前工作区作为只读 donor/reference，从 `9763b70...` 创建新的干净 worktree 和 `codex/portability-rebuild` 分支，再按本文修正后的顺序选择性重放已经验证的代码。这样使用干净基线，但不浪费现有成果。

建议操作方式（执行前先确认目标目录不存在）：

```bash
git worktree add ../wondertrader-portability-clean \
  -b codex/portability-rebuild \
  9763b70c69b162f0cc9d8970fc40993f00c7c167
```

当前工作区在重建期间不得 reset/clean。每个新切片从当前 donor 选择文件或局部 diff，经过格式检查、configure/build、聚焦测试和 CTest 后单独提交。只有当所有需要保留的切片都已进入新分支并核对完毕，才考虑删除旧 worktree 中的未提交内容。

本文件当前也是 untracked 内容，不会自动出现在新 worktree。创建新 worktree 后，应先把本文件复制到新 worktree，作为方案/验收基线单独提交，再开始代码切片。

## 干净重建实施结果（2026-07-14）

本方案已在从 `9763b70c69b162f0cc9d8970fc40993f00c7c167` 创建的
`codex/portability-rebuild` 分支和独立 worktree 中按切片执行。原
`compile_warnings` 工作区只作为 donor/reference，未执行 reset、clean 或覆盖。

已完成的实现包括：

- 可重复、非交互的 CTest 基线，测试沙箱、label、timeout、TestUnits 多进程 IPC 回归和安装树 plugin smoke。
- `WT_SYSTEM/WT_ARCH` 平台归一化、C++17 最低契约与 `WT_CXX_STANDARD=17/20/23` 精确矩阵。
- fmt、RapidJSON、Boost、nanomsg、ZMQ、线程、动态加载和 filesystem 的 target/probe 模型；生产源码不再直接使用 Boost.Filesystem。
- 所有活跃 target 的显式源码清单、target 级 PIC、统一 `$<CONFIG>` 输出策略；普通 Release 不再自动 `-s`。
- WtRunner、WtPorter、WtDtPorter、QuoteFactory、WtUftRunner 的 target-file 开发部署，以及 WtRunner 的权威 install tree。
- CTP 主版本、CTP Mini/Opt、Femas、XTP、YD、OES、ATP、HuaX、XTP XAlgo、DD/FixApi、HTS 的 `AUTO/ON/OFF + *_ROOT` gate；可用组件使用 interface/imported target，缺失组件 `AUTO` 结构化跳过、`ON` configure fatal。
- 显式 vendor runtime 再分发 allowlist、同名文件内容冲突检查、Linux `$ORIGIN`/macOS `@loader_path`/Windows 同目录布局。无 SONAME 的 OES 库使用 `IMPORTED_NO_SONAME`，避免把开发机绝对 SDK 路径写入 `DT_NEEDED`。
- `TraderXTPXAlgo`、`ParserOES`、`ParserYD`、HuaX、ATP、DD、HTS 等历史/孤儿目录的 target 名、SDK include、输出和 gate 修复；DD/HTS SDK 缺失不会阻塞默认构建。
- 全仓 Filesystem/Asio/ModuleName/PlatformPause 兼容迁移，以及版本化 `ShmWireLayout + AtomicCompat`、lock-free configure probe、编译期布局断言、旧映射拒绝和多进程重启测试。
- configure-time 策略测试会阻止目录级依赖、旧输出变量、`file(GLOB)`、Release 自动 strip、硬编码开发机路径、源码内 SDK include 和 `#pragma comment(lib)` 回流。

本机验证结果如下；这些结果不替代尚未运行的平台 CI：

| 环境 | 验证结果 |
| --- | --- |
| Linux x86_64 / GCC 13.3 / C++17 | 全目标构建通过；CTest 31/31；install/plugin smoke 通过 |
| Linux x86_64 / GCC 13.3 / C++20 + `-Werror` | 全目标构建通过；CTest 31/31；install/plugin smoke 通过 |
| Linux x86_64 / GCC 13.3 / C++23 + `-Werror` | `linux-x64-cxx23` 全目标构建通过；CTest 33/33；install/plugin smoke 通过 |
| Linux x86_64 / ASan+UBSan+LSan | `WonderTrader.TestUnits` 与 `WonderTrader.WtBtCoreTests` 两个测试进程 2/2 通过，启用 leak detection |
| 共享内存聚焦测试 | TestUnits 覆盖 layout/version/alignment、三轮环形复用、四类混合载荷和不同生产者 PID 重启；不再保留 BenchTools ABI manifest 与 benchmark harness |
| vendor 三态 | available/missing AUTO/missing ON/wrong-arch fixture 全通过；ATP/DD/HTS 缺 SDK 的显式 ON 诊断已单独验证 |
| 安装树 | OES Parser/Trader 的 `DT_NEEDED=liboes_api.so`、`RUNPATH=$ORIGIN/..`，`ldd` 从 install root 解析；所有已选插件可 `dlopen` |

当前环境没有 `ninja`/`ninja-build`，因此无法实际运行 Ninja Multi-Config。仓库在 `src/CMakePresets.json` 中保留多标准和 sanitizer 本地配置，但 GitHub Actions 收敛为单一 `.github/workflows/portability.yml`：只运行 macOS arm64（固定 GA `macos-26`）、Linux x86_64（`ubuntu-latest`）、Windows amd64（`windows-latest`）三个 Release/C++23 job，不再运行 Debug、C++20 或 ASan job。workflow 使用基于 Node.js 24 的 `actions/checkout@v6` 与 `actions/upload-artifact@v7`，并对经 `tee`/`Tee-Object` 的构建显式传播失败码。Linux 三套本地 preset 已在 GCC 13.3 下完成全目标构建并分别通过 CTest 33/33；macOS 与 Windows runner 上的 AppleClang/MSVC、Homebrew/vcpkg、Mach-O 和 CRT 组合仍必须以提交后的 workflow 结果为最终依据。Linux aarch64、Clang/libc++、Conan、sanitizer CI 和闭源 SDK 的全部 ABI/CRT 组合不再由该精简 workflow 覆盖，不能由本机 Linux x86_64 结果替代。ATP 仓库内只有 Windows runtime/import library，因此 Linux `AUTO` 跳过是预期行为；DD/HTS SDK 未随仓库提供，同样只验证了 gate 和缺失诊断。

### 为什么不建议两个极端选项

| 选项 | 结论 | 原因 |
| --- | --- | --- |
| 在当前工作区继续堆叠 | 不建议 | 已有 diff 过大且混合了构建系统、业务源码、ABI、测试和策略脚本，继续扩大会进一步降低可验证性。 |
| reset 后完全从零重写 | 不建议 | 会丢失已经有价值的平台归一化、兼容头、测试和策略脚本，也会重复已经完成的排查。 |
| 干净 worktree 分片重建并复用当前成果 | 推荐 | 同时获得干净提交历史、独立验收和现有实现参考，风险最低。 |

## 原 donor 工作区审计快照（历史）

本节只记录当前 donor 工作区中“已经存在的实现”，不代表这些变更已经达到可提交或跨平台验收状态。当前有 GCC 15/Linux x86_64 的构建产物以及 C++17/C++20 聚焦测试结果，但缺少可重复的 CTest 入口和完整平台证据；因此禁止把“存在产物”“曾在某个目录运行成功”和“正式支持”混为一谈。

审计复现结果：`ctest --test-dir build/codex_cxx17 -N` 未发现已注册测试；从仓库根目录直接运行现有 C++17/C++20 `TestUnits` 时，前面的 Atomic/Asio/共享内存聚焦用例通过，但两次全量运行都在 LMDB 使用相对路径 `./testdb` 打开失败后崩溃。该现象不能直接归因于 portability 改动，却证明当前测试依赖工作目录/残留状态，尚不具备可重复验收条件。

- 平台架构归一化：新增 `src/cmake/WtPlatform.cmake`，修复 Linux aarch64 被归到 `x64` 的问题，并保留 macOS arm64/x64 单架构校验；新增 `WT_RESOLVE_PLATFORM_VARS`，输出 `WT_SYSTEM`/`WT_SHARED_SUFFIX`/`WT_RUNTIME_SUBDIR`，CTP SDK 搜索已从 `${PLATFORM}` 迁到 `${WT_ARCH}`。这部分可以作为重建分支的早期候选切片，但仍缺真实 Linux aarch64/macOS/Windows configure 证据和 SDK 二进制架构校验。
- C++ 标准策略：已有 `WT_CXX_STANDARD`、`CMAKE_CXX_STANDARD_REQUIRED ON`、`CMAKE_CXX_EXTENSIONS OFF` 以及 C++17/C++20/C++23 本机构建目录；ATP demo 也删除了手写 `-std=c++11`。尚未完成 cache 值校验、target 级 `cxx_std_17` usage requirement 和默认标准下调，不能标记为完成。
- 构建产物复制、目录创建与输出目录：已有 `src/cmake/WtCopyTarget.cmake`、target output properties、`$<CONFIG>`、`$<TARGET_FILE:...>` 和 `copy_if_different`。output properties 可复用；consumer `POST_BUILD` copy 只能作为过渡实现，因为 producer 单独更新时 consumer 不一定重新链接和触发复制，最终必须由 deployment target 或 install/package 规则接管。
- 测试输出目录：将 TestUnits/TestParser/TestTrader/TestPorter/TestDtPorter/TestBtPorter/TestExecPorter 迁移到 `wt_set_target_output_directory()`，并加入局部策略检查脚本。历史 BenchTools 已在最终收尾中删除。
- Boost/fmt/nanomsg/ZMQ 基础 target 化：已有 `WT::fmt`、`WT::Nanomsg`、`WT::ZMQ`、`WT::RapidJSON` 等 target，但顶层仍递归向全部 target 注入 fmt，子目录仍普遍使用目录级 include/link 状态；Boost 发现还强制覆盖 cache 并关闭系统/config package 路径。因此只能标记为“target 外壳已建立，依赖模型未完成”。
- `std::filesystem` 链接策略：当前使用“GNU 编译器版本低于 9 时链接 `stdc++fs`”的启发式判断。这不等价于探测实际使用的标准库，必须改为先无额外库做 link probe，失败后再带 `stdc++fs` probe。
- 编译器/平台判断：核心 `ELSE(GNUCC)` 和 PIC 状态有所收敛；当前递归给所有 GNU Release 二进制添加 `-s` 会让测试和崩溃诊断丢失符号，应在重建时删除，改为可选的 install/package strip。
- CMake GLOB 策略：补 `CONFIGURE_DEPENDS` 只能作为迁移期止血，不能视为最终完成；活跃核心 target 应逐步改成显式源码列表。
- MSVC CRT warning suppression：`WT::ProjectOptions` 的 MSVC-only generator expression 可以复用，但必须由目标显式链接或通过明确的项目 options target 传播；不能继续使用目录级 `LINK_LIBRARIES` 全局注入。
- 公共头封装：补齐若干自包含 include，移除公共头里的 `using namespace boost::asio`，删除 `ParserShm.h` 对 Boost.Asio 的无效公共依赖。
- 平台兼容头：新增 `ModuleNameCompat.hpp` 与 `PlatformPause.hpp`，收敛动态库命名、CPU affinity、spin pause 的平台差异；`ModuleNameCompat.hpp` 已扩展到 `WtBtPorter`、`WtDtPorter`、`WtExecMon`、`WtPorter`、`WtDtServo` 的 MSVC minidumper module basename 提取用法，移除这些入口对 direct `<filesystem>` / Boost.Filesystem 的依赖。
- 当前目录兼容头限定切片：新增 `Share/CurrentDirCompat.hpp`，将 `WtCore`、`WtBtCore`、`WtDtCore`、`WtUftCore`、`TraderDumper`、`WtDtServo` 六个 `WtHelper.cpp` 以及 `WtRunner`、`WtUftRunner`、`QuoteFactory` 可执行入口中的当前工作目录获取迁移到统一兼容函数；当前目录查询失败时显式抛出异常，不再静默回退到 `"."`。该切片只覆盖上述 helper 的 `getcwd/_getcwd` 返回值检查、上述入口的当前目录查询与平台头收敛，不代表全仓路径/文件系统相关代码已完成迁移。
- Asio 兼容头限定切片：新增 `Share/AsioCompat.hpp`，先迁移 `ParserUDP` 与 `ParserXeleSkt` 两个 UDP parser，并已按小切片扩展到 `WtDtCore/UDPCaster`、`WtDtPorter/WtDtRunner` 以及 `WtCore/WtUftCore EventNotifier`；这些公共头不再直接 include Boost.Asio，相关 `.cpp` 不再直接写 `boost::asio` / `boost::system::error_code`。本地验证仍限定在上述模块和聚焦测试，不代表全仓 Asio 迁移已经完成。
- Filesystem 兼容头限定切片：新增 `Share/FilesystemCompat.hpp`，先迁移 `StdUtils.hpp` 公共头中的 `std::filesystem` 依赖到 `wt::fs` alias，并已将切片扩展到 `WtCore`、`WtBtCore`、`WtUftCore` 三个 `WtHelper.cpp` 的目录创建用法、`WTSTools/WTSLogger.cpp` 的日志目录创建用法、`WtCore`/`WtUftCore` 四个 StrategyMgr 的策略工厂目录遍历用法、`WtDtCore/IndexWorkerMgr.cpp` 和 `WtCore/WtExecuterFactory.cpp` 的核心工厂目录遍历用法、`WtBtCore` 回测 mocker/replayer 的输出目录、缓存目录创建和增量回测文件存在性检查入口、`ParserCTP`、`ParserCTPMini`、`ParserCTPOpt`、`ParserFemas`、`ParserXTP` 五个已构建行情 Parser 的 flow 目录创建用法、`CTPLoader`/`CTPOptLoader` flow 目录创建用法、`TraderCTP`、`TraderCTPOpt`、`TraderCTPMini`、`TraderFemas`、`TraderXTP`、`TraderYD` 六个已构建 vendor Trader 的 flow/cache 或 flow/local 目录创建用法、`TraderMocker` 本地 mocker 持仓目录创建与持仓文件存在性检查用法、`WtCore/TraderAdapter.cpp` 交易通道 save-data 目录创建用法、`WtCore`/`WtUftCore` 策略上下文输出目录、rtchart 目录和本地数据目录创建用法、`WtDtHelper` CSV/bin 转换辅助函数中的目录创建与目录遍历用法，以及 `WtCore/WtFilterMgr` 过滤器文件时间戳检查用法并移除其公共头对 Boost.Filesystem 的直接暴露；当前通过聚焦策略脚本约束已迁移文件，通过目标编译验证这些切片可构建，并由 TestUnits 覆盖兼容头自包含、WtFilterMgr 公共头自包含与基础真实文件操作，但仍不是全仓 filesystem 迁移或所有业务路径行为测试。

- AtomicCompat 兼容头限定切片：已有统一 load/store/fetch-add API、聚焦测试和三处调用迁移，但当前实现仅在“不提供 `atomic_ref`”时才进入 builtin/Interlocked；如果提供 `atomic_ref` 但不 always-lock-free，会直接编译失败。该切片不得单独重放，必须与 `ShmWireLayout`、release 模式地址校验、明确的平台支持策略和多进程 ABI 测试一起进入重建分支。

尚未完成的高风险项包括 `ShmWireLayout.hpp`、完整 vendor SDK imported target 模型、RPATH/install 打包模型和真实跨平台验证。这些项目涉及 ABI、共享内存 wire layout、闭源 SDK 或部署环境，必须各自建立验收矩阵；不得仅凭 Linux x86_64 本机构建结果合并。

`TraderDD` 与 `TraderHTS` 仍保留旧式 `LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../build/bin`，本轮暂不迁移。原因是二者不在顶层 CMake/xmake 默认构建中，当前仓库与 `/home/daohang/prod_0624/thirdparty` 下缺少 FixApi/HTS SDK；`TraderDD` 隔离构建会因缺 `src/TraderDD/FixApi/libfixapi.so` 与 standalone include 路径失败，`TraderHTS` CMake 还错误沿用 `TraderDD` 的 project、source 和 target 名，configure 阶段即失败。按照“每优化一个问题需要编译成功后再优化下一个”的约束，这两个历史孤儿模块应先补齐或显式 gate vendor SDK、修正 target/source 命名并建立基线编译，再单独迁移输出目录。

## 原始剩余切片 backlog（已在重建分支执行）

以下列表保留原始执行顺序和验收设计，实际完成状态以“干净重建实施结果”为准。donor 工作区中的已有实现只作为参考，没有用于替代对应验收。

1. 可重复测试基线（新增 P0）：移除 `TestUnits` 的 `getchar()`，每个会写文件的测试使用独立临时目录，接入 `enable_testing()`/`add_test()`，为 `unit`、`ipc`、`plugin-smoke` 添加 label 和超时。验收：全新临时目录中连续运行两次结果一致，失败返回非零且不崩溃，`ctest --test-dir <build> --output-on-failure` 可直接执行。
2. WtPlatform 平台变量收尾（P0#1 剩余）：收敛遗留 `PLATFORM`/`PREFIX`/`SUFFIX`，SDK 发现后校验 ELF、Mach-O/fat Mach-O、PE/COFF 的目标架构，把 `WT_RUNTIME_SUBDIR` 接入部署布局。验收：假 SDK 双架构布局不误选；Linux x86_64/Linux aarch64/macOS arm64 configure 输出一致；交叉编译时使用 toolchain 描述的 target，而不是 host 架构。
3. 依赖 target 模型（P0#3 剩余）：移除递归 `WT_LINK_FMT_IN_DIRECTORY`、目录级 `LINK_LIBRARIES/INCLUDE_DIRECTORIES/LINK_DIRECTORIES` 和 `FORCE` 覆盖包查找 cache；优先消费依赖提供的 config target，再以 `WT_DEPS_ROOT`/`CMAKE_PREFIX_PATH` hint 和 module fallback 补充。验收：系统依赖、自定义 prefix、Conan/vcpkg 三种模式至少各有一条 configure/build 证据，compile commands 中不存在无关 target 被注入依赖的情况。
4. filesystem 收尾（P1#6 剩余）：当前生产代码剩余重点是 `Share/BoostMappingFile.hpp` 以及未进入默认构建的 ATP/OES/XTPXAlgo/HuaX/DD/HTS 等模块；测试代码可直接使用 `std::filesystem`。链接策略改成“两次 link probe”：先不加额外库，通过即不链接；失败后用 `stdc++fs` 重试，仍失败则给出 toolchain 诊断。验收：GCC 8/9/15、Clang+libstdc++、Clang+libc++ 的代表性组合通过 configure/link。
5. vendor SDK imported target 三态模型（P1#7）：把 CTP 之外的 XTP/Femas/YD/OES/ATP 收敛为 `AUTO/ON/OFF + *_ROOT + imported target`，并表达 include、按配置 library/import library、runtime、架构和 ABI/CRT 约束。验收：移走 SDK 后 AUTO 跳过并打印原因、ON configure fatal；假错误架构 SDK 在 configure 阶段失败；干净目录 `dlopen`/`LoadLibrary` smoke test。
6. 输出、install、RPATH/runtime 部署（P1#8）：保留 target output properties；以 `install(TARGETS)` 显式安装主程序和所有动态加载插件，以 `install(IMPORTED_RUNTIME_ARTIFACTS)` 或明确文件规则安装允许再分发的 vendor runtime，再用 runtime dependency set 收集链接依赖。CMake 最低版本须提升到 3.21，或为 3.20 提供不使用这些命令的兼容路径。Linux 使用 `$ORIGIN`，macOS 使用 `@loader_path`，Windows 使用 DLL 同目录/明确 runtime 目录，不把 RPATH 方案套到 Windows。验收：install tree 在无开发依赖环境中加载插件；同时验证 producer 单独重建后部署树不会保留旧插件。
7. `ShmWireLayout.hpp + AtomicCompat`（独立高风险切片）：集中定义共享内存 wire layout、版本号/魔数、端序、`alignas`、定宽整数、padding 和 `sizeof/alignof/offsetof`；明确 atomic 不 lock-free 时是 fallback 还是 unsupported，并保证 release 构建也不会对未对齐地址执行 `atomic_ref`。验收：多进程 producer/consumer、wrap-around、满/空队列、异常退出恢复，以及 GCC/Clang/MSVC 和 x86_64/arm64 ABI 常量对比。
8. 孤儿模块修复（阶段 6）：`TraderHTS/CMakeLists.txt` 当前错误复用 `TraderDD` 的 project/source/target 名；应先修正命名、补齐或显式 gate FixApi/HTS SDK 建立基线编译，再迁移输出和安装规则。孤儿模块不得阻塞默认核心构建。

## P0 问题

### 1. Linux aarch64 被归类为 x64

证据：

- `src/CMakeLists.txt` 只在 `APPLE AND WT_DARWIN_ARCH STREQUAL "arm64"` 时把 `PLATFORM` 设置为 `arm64`，其他非 MSVC 平台都落到 `x64`。
- 这会影响输出目录 `build_${PLATFORM}`，也会影响 CTP 等 vendor SDK 的 `${PLATFORM}` 搜索路径。

风险：

- Linux arm64 构建会写入 `build_x64`。
- 可能在 configure 阶段误发现 x86_64 SDK，到 link 阶段才失败，甚至在动态加载模式下延迟到运行期失败。

优化方案：

- 新增 `cmake/WtPlatform.cmake`，提供 `wt_normalize_platform()`：
  - `WT_SYSTEM`: `windows/linux/macos`
  - `WT_ARCH`: `x86/x64/arm64`
  - `WT_SHARED_SUFFIX`: `.dll/.so/.dylib`
  - `WT_RUNTIME_SUBDIR`: `win/x64`、`linux/x64`、`linux/arm64`、`darwin/arm64` 等。
- 基于 `CMAKE_SYSTEM_NAME`、toolchain 提供的目标处理器、`CMAKE_SYSTEM_PROCESSOR`、`CMAKE_OSX_ARCHITECTURES` 映射架构；交叉编译时不得退回 host 处理器猜测 target。
- 禁止不同含义的变量共用 `PLATFORM`。输出目录、第三方库目录、wrapper 目录分别使用明确变量。
- CTP/XTP/Femas/YD 等 SDK 搜索必须使用 `WT_ARCH`，并在发现库后校验 ELF、Mach-O/fat Mach-O 或 PE/COFF 架构；目录名命中不能视为校验成功。

> 现状（donor 工作区已有）：`src/cmake/WtPlatform.cmake` 已实现架构归一化（`WT_NORMALIZE_PLATFORM_ARCH` → `WT_ARCH`，含 Darwin 单架构校验，修复 Linux aarch64 被归到 `x64`）以及语义平台变量解析（`WT_RESOLVE_PLATFORM_VARS` → `WT_SYSTEM`/`WT_SHARED_SUFFIX`/`WT_RUNTIME_SUBDIR`）；顶层在平台分支后统一解析并打印这三个变量，CTP SDK 搜索已从 `${PLATFORM}` 改用 `${WT_ARCH}`。尚未完成真实跨平台验证、交叉编译 target 解析、SDK 二进制架构校验、遗留变量收敛和 runtime 布局接入（见“剩余切片 backlog”第 2 项）。

验证：

- 在 Linux x86_64、Linux aarch64、macOS arm64、Windows x64 分别 configure，检查 `WT_ARCH` 和输出目录；另用交叉 toolchain 验证 host/target 不同时选择 target。
- 用一个假 SDK 布局同时放置 `x64` 与 `arm64` 目录，确认不会误选。

### 2. 输出目录和 post-build copy 不兼容多配置生成器

证据：

- `src/WtRunner/CMakeLists.txt`、`src/WtPorter/CMakeLists.txt` 等直接设置 `EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/build_${PLATFORM}/${CMAKE_BUILD_TYPE}/...`。
- post-build copy 手写 `${PREFIX}${target}${SUFFIX}` 和 `${CMAKE_BUILD_TYPE}`。

风险：

- Xcode、Visual Studio、Ninja Multi-Config 下 `CMAKE_BUILD_TYPE` 可能为空。
- `OUTPUT_NAME`、debug postfix、Windows DLL、macOS dylib、路径空格、target 重命名都会让 copy 失效。
- 可选组件关闭后，copy 命令如果没有 `TARGET` guard 会破坏构建。

优化方案：

- 用 `RUNTIME_OUTPUT_DIRECTORY`、`LIBRARY_OUTPUT_DIRECTORY`、`ARCHIVE_OUTPUT_DIRECTORY` 设置 target 输出。
- 构建树中的开发态部署只使用 `$<TARGET_FILE:Target>`、`$<TARGET_FILE_DIR:Consumer>` 和 `copy_if_different`，但不得把命令只绑定到 consumer 的链接事件；否则 producer 单独更新时可能留下旧副本。
- 可封装一个始终参与默认构建、显式依赖 consumer 和 producer 的 deployment target；`copy_if_different` 可降低重复复制成本。示意：

```cmake
add_custom_target(WtRunner_deploy ALL DEPENDS WtRunner)

if(TARGET ParserUDP)
  add_dependencies(WtRunner_deploy ParserUDP)
  add_custom_command(TARGET WtRunner_deploy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:WtRunner>/parsers"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ParserUDP>"
            "$<TARGET_FILE_DIR:WtRunner>/parsers/"
    VERBATIM)
endif()
```

- 该 deployment target 只服务开发态运行。正式部署必须以 `install(TARGETS)` 明确列出 consumer、插件和 vendor runtime；install tree 是打包和 smoke test 的唯一权威输入。

验证：

- 用 `Ninja`、`Ninja Multi-Config`、Xcode/Visual Studio 至少各跑一次 configure。
- 分别关闭 CTP/ZMQ/某个 vendor SDK，确认 `WtRunner` 和各 Porter 仍能构建。
- 只修改并重建一个 producer，不重链 consumer，确认 development/install tree 中的插件仍被更新。

### 3. 全局 include/link 状态污染 target

证据：

- 顶层设置 `INCS/LNKS`，多数子目录使用 `INCLUDE_DIRECTORIES(${INCS})` 和 `LINK_DIRECTORIES(${LNKS})`。
- Boost、nanomsg、ZMQ、fmt 等多处以裸库名链接。

风险：

- 目标依赖不可追踪，依赖顺序、传递链接、静态/动态选择都不明确。
- macOS arm64 Homebrew 与 `/usr/local`/`/opt/homebrew` 混用时，容易混入错误架构库。
- Conan/vcpkg 或自定义前缀下库名、依赖传播和 RPATH 不稳定。

优化方案：

- 新增 `cmake/WtDependencies.cmake`，所有外部依赖转换成 imported/interface target：
  - `WT::Boost`
  - `WT::fmt`
  - `WT::RapidJSON`
  - `WT::Nanomsg`
  - `WT::ZMQ`
  - `WT::CTP`
  - `WT::Vendor::XTP` 等。
- 各业务 target 只使用 `target_link_libraries(target PRIVATE WT::...)`。
- include 目录通过 `target_include_directories` 的 `PRIVATE/PUBLIC/INTERFACE` 表达传播边界。
- 优先消费依赖自身提供的 config target；没有 config package 时才由项目创建 imported target。`WT_DEPS_ROOT` 只能作为 hint，不能用 `FORCE` 覆盖用户 cache，也不能无条件关闭 `CMAKE_PREFIX_PATH` 和系统搜索。
- 删除目录级 `INCLUDE_DIRECTORIES`、`LINK_DIRECTORIES`、`LINK_LIBRARIES`、递归“给所有 target 链接某依赖”的函数和全局 `ADD_DEFINITIONS`。迁移期如必须保留，需有策略检查和明确清零条件。

验证：

- `cmake --graphviz` 或生成 compile commands 检查目标依赖是否可追踪。
- 在只提供 `CMAKE_PREFIX_PATH`、只提供 `WT_DEPS_ROOT`、使用 Conan/vcpkg toolchain 的环境下分别 configure，避免依赖本机硬编码目录。
- 从 `compile_commands.json` 和 link command 抽查：不使用 fmt/ZMQ/vendor SDK 的 target 不得被注入相应依赖。

## P1 问题

### 4. C++ 标准策略不一致

证据：

- 顶层和大量子目录重复 `SET(CMAKE_CXX_STANDARD 23)`。
- 未设置 `CMAKE_CXX_STANDARD_REQUIRED ON` 和 `CMAKE_CXX_EXTENSIONS OFF`。
- `src/xmake.lua` 使用 C++17，而 CMake 强制 C++23。

风险：

- 编译器可能 silent decay 到较低标准。
- 源码真实依赖以 C++17 的 `std::filesystem` 为主，强制 C++23 会抬高用户门槛。
- C++20/23 warning 和行为差异会被误认为业务逻辑问题。

优化方案：

- 顶层提供：

```cmake
set(WT_CXX_STANDARD "17" CACHE STRING "C++ standard: 17, 20, or 23")
set_property(CACHE WT_CXX_STANDARD PROPERTY STRINGS 17 20 23)

if(NOT WT_CXX_STANDARD MATCHES "^(17|20|23)$")
  message(FATAL_ERROR "WT_CXX_STANDARD must be 17, 20, or 23")
endif()

set(CMAKE_CXX_STANDARD ${WT_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

- 公共头要求 C++17 的库使用 `target_compile_features(target PUBLIC cxx_std_17)`；仅实现使用 C++17 的目标使用 `PRIVATE`。公共 interface target 可以复用共同策略，但不能代替正确的传播范围。
- 只有确实使用 C++20/23 API 的 target 才局部提升标准。
- 源码中使用 feature-test macro，例如 `__cpp_lib_atomic_ref`，但 fallback 放在公共兼容层。

验证：

- CI/build matrix：最低受支持 GCC/Clang/MSVC 的 C++17、主力 GCC/Clang 的 C++17/20/23、AppleClang arm64；编译器最低版本必须在 README/CMake configure 诊断中明确。
- 编译 `TestUnits` 和策略回测最小集合。

### 5. 编译器/平台判断使用方式不稳

证据：

- 多处 `ELSE(GNUCC)` 实际被当作“非 MSVC”分支。
- `LINK_FLAGS_RELEASE -s` 和 `-fPIC` 分散在子项目中。

风险：

- AppleClang、Clang、Intel、MinGW 都可能进入错误分支。
- strip 在链接期执行会破坏调试符号保留策略。
- PIC 是否启用不再是 target 属性。

优化方案：

- 使用 `CMAKE_CXX_COMPILER_ID`、`CMAKE_CXX_SIMULATE_ID`、`CMAKE_SYSTEM_NAME`、toolchain 信息或 generator expression，区分 GNU、Clang、AppleClang、MSVC、clang-cl 和 MinGW，避免把“非 MSVC”等同于 GNU。
- 使用 `POSITION_INDEPENDENT_CODE ON` 或顶层 `CMAKE_POSITION_INDEPENDENT_CODE ON`。
- 所有公共编译选项通过 `wt_apply_common_options(target)` 统一设置。
- 普通 Release 构建保留符号；strip 由显式的 install/package 选项控制，并在需要时生成/保留独立调试符号。测试、benchmark 和 sanitizer 构建禁止 strip。

### 6. `std::filesystem` 链接策略需要 probe

证据：

- 基线版本对所有非 Apple、非 MSVC 环境固定链接 `stdc++fs`；当前 donor 工作区改成了 `GNU && compiler_version < 9` 的启发式判断。
- 是否需要 `stdc++fs` 实际取决于标准库及其版本，而不只取决于编译器 frontend；Clang 可以搭配 libstdc++ 或 libc++。

风险：

- GCC 8 C++17 可能需要 `stdc++fs`，新 GCC 不需要。
- Clang + libc++ 或非 GNU libstdc++ 环境可能没有 `stdc++fs`。

优化方案：

- 新增 `src/Share/FilesystemCompat.hpp`：

```cpp
namespace wt {
namespace fs = std::filesystem;
}
```

- CMake 做包含真实 `std::filesystem` 调用的 link probe：先不设置额外库；失败后以 `stdc++fs` 重试。第一次通过则 `FS_LIB` 为空，第二次通过才使用 `stdc++fs`，两次都失败则报告 compiler、标准库和 probe 日志。
- 业务代码逐步改为 `wt::fs`，禁止直接散落 `<filesystem>`。

验证：

- GCC 8/9/15、Clang+libstdc++、Clang+libc++ 分别 configure/link。

### 7. 第三方 SDK 缺少统一可选组件模型

证据：

- CTP 已有 `WT_ENABLE_CTP=AUTO/ON/OFF`，但 CTPMini/CTPOpt 仍跟随主 CTP。
- XTP/Femas/YD/OES/ATP/HuaX 等 vendor 组件多处默认加入或硬编码版本路径。
- ZMQ 顶层和子目录重复探测，构建后 wrapper 插件目录也不一定收到产物。

风险：

- macOS arm64 或 Linux arm64 没有对应 SDK 时，默认构建失败。
- AUTO 模式下 configure 成功，但运行期缺 vendor runtime。
- 显式 ON 时缺失项不够 fail-fast。

优化方案：

- 所有 vendor SDK 统一成三态：
  - `AUTO`: 找到可用 SDK 则构建，否则跳过并打印原因。
  - `ON`: 找不到或架构不匹配则 configure fatal。
  - `OFF`: 不构建。
- 每个 SDK 提供 `*_ROOT` cache 变量，优先级高于系统路径。
- 每个 SDK 通过 imported target 表达 include、按配置的 library/import library、runtime、架构、编译器 ABI 和 Windows CRT 约束；仅检查目录名或 CPU 架构不够。
- 运行时库由 install/package 规则显式处理，并维护可再分发 allowlist；不能把开发机上扫描到的所有动态库无条件打包。

验证：

- 移走 CTPMini/CTPOpt/XTP/Femas/YD SDK，AUTO 应跳过、ON 应明确报错。
- 在干净 install tree 做 `dlopen`/`LoadLibrary` smoke test，确认错误诊断和成功加载路径。

### 8. RPATH 与运行时依赖部署缺口

证据：

- 除少数 DD/HTS 目标外，项目基本没有统一 RPATH 设置。
- ZMQ、nanomsg、Boost 等运行时依赖没有统一收集。
- Linux/macOS wrapper copy 脚本约定不对称，Linux 脚本硬编码 x64。

风险：

- 构建机可以运行，部署到干净机器后找不到动态库。
- Homebrew dylib install name、Linux `$ORIGIN`、vendor SDK runtime 路径无法统一治理。

优化方案：

- 构建树运行使用 target `BUILD_RPATH`。
- install/package 阶段设置：
  - Linux: `$ORIGIN`、`$ORIGIN/..`、`$ORIGIN/parsers` 等。
  - macOS: `@loader_path`、`@loader_path/..`。
  - Windows: 将允许再分发的 DLL 安装到可执行文件同目录或明确的 runtime 目录，不使用 RPATH 概念。
- 主程序和动态加载插件必须先通过 `install(TARGETS)` 显式列出；imported vendor runtime 使用 `install(IMPORTED_RUNTIME_ARTIFACTS)` 或明确文件规则。`install(RUNTIME_DEPENDENCY_SET)` 只补充这些已知产物的链接依赖，不能发现业务代码字符串传给 `dlopen`/`LoadLibrary` 的插件。
- `install(RUNTIME_DEPENDENCY_SET)` 和 `install(IMPORTED_RUNTIME_ARTIFACTS)` 需要 CMake 3.21；项目要么把最低版本从 3.20 提升到 3.21，要么提供 3.20 兼容安装路径。
- 对自动收集结果设置系统库排除规则和可再分发 allowlist，防止把 glibc、系统 framework 或许可证不允许的开发机依赖打入包。
- 参数化 copy shell 只能作为兼容入口，内部应调用 `cmake --install`，避免 Linux/macOS/Windows 各维护一套产物清单。

验证：

- 对 wrapper 产物运行 `ldd`、`otool -L` 或 Windows 依赖检查工具。
- 在无开发依赖的容器/临时环境做插件加载测试。
- 只重建一个插件后重新安装，确认 install tree 不保留旧二进制。

## 头文件兼容层问题

### 9. `DLLHelper.hpp` 不够自包含，模块名包装不幂等

证据：

- `DLLHelper.hpp` 使用 `printf`、`isalpha` 但没有直接 include `<cstdio>`、`<cctype>`。
- Windows 分支 include `<wtypes.h>`，但调用 `LoadLibrary`/`GetProcAddress`。
- `wrap_module()` 扫描首个字母并强行追加 `lib` 和后缀。

风险：

- include 顺序变化、MSVC Unicode 设置、clang-cl 严格编译都可能失败。
- 输入 `/opt/sdk/libfoo.so`、`./foo`、`libfoo.dylib` 可能产生双前缀/双后缀或错误路径。

优化方案：

- 新增 `ModuleNameCompat.hpp`：
  - 保留目录部分。
  - 已有当前平台后缀则不重复追加，并覆盖版本化 ELF 名称（如 `libfoo.so.1`）。
  - 遇到其他平台后缀时明确保持原样或报“不兼容模块名”，不能静默把 Linux 上的 `foo.dylib` 当作可加载模块。
  - 已有 `lib` 前缀则不重复追加。
  - 只对纯模块 basename 做平台前后缀转换。
- `DLLHelper` 改为 typed API：

```cpp
template <class Fn>
Fn get_symbol(DllHandle handle, const char* name);
```

- Windows 显式使用 `LoadLibraryA` 或封装宽字符版本。
- POSIX 加入 `dlerror()` 信息返回，不直接在公共工具里 `printf`。

验证：

- 单测覆盖 `foo`、`libfoo.so`、`libfoo.so.1`、`foo.dylib`、`./foo`、`/opt/x/libfoo.so`、Windows 路径、空串、尾随分隔符和无字母字符串，并按当前平台断言跨平台后缀策略。

### 10. CPU affinity 与 spin pause 平台封装过散

证据：

- `CpuHelper.hpp` 非 Windows/Apple 平台都走 `pthread_setaffinity_np`。
- `pthread_setaffinity_np` 当前用 `>= 0` 判断成功，失败返回正 errno 时会误报成功。
- `SpinMutex.hpp` 公共头中定义 `WIN32_LEAN_AND_MEAN` 并 include `windows.h`。

风险：

- 非 Linux POSIX 平台编译失败。
- Linux affinity 失败被误判为成功。
- 公共头宏污染和编译成本上升。

优化方案：

- `CpuHelper` 仅在 `__linux__` 下使用 `pthread_setaffinity_np`，返回值必须 `== 0`。
- 检查 `CPU_SETSIZE`，更大 CPU 编号用 `CPU_ALLOC` 或直接返回 unsupported。
- 新增 `PlatformPause.hpp`，仅暴露 `wt_platform_pause()`；Windows 细节只放实现头或 central platform header。
- C++17 保留有界 spin/backoff。`atomic_flag::wait/notify` 会改变阻塞和唤醒语义，只能作为经过延迟/吞吐测试的独立实现选项；共享内存场景还必须验证标准库底层等待原语是否支持跨进程，不能仅按 C++ 标准版本自动切换。

### 11. `atomic_ref` / `__atomic` / `Interlocked` 逻辑重复

证据：

- `ShareBlocks.cpp`、`ShmCaster.cpp`、`ParserShm.cpp` 都有类似的 `atomic_ref`、MSVC `Interlocked`、GCC/Clang `__atomic` 分支。

风险：

- `std::atomic_ref::is_always_lock_free` 在某些架构失败时没有 fallback。
- memory order、对齐要求和运行时地址检查容易在不同文件漂移。
- C++17 下依赖 builtin fallback，C++20/23 下依赖 `atomic_ref`，行为差异不集中。

优化方案：

- 新增 `src/Share/AtomicCompat.hpp`：
  - `wt_atomic_load_u32/u64`
  - `wt_atomic_store_u32/u64`
  - `wt_atomic_fetch_add_u32/u64`
  - `wt_atomic_required_alignment<T>()`
  - debug 下保留地址对齐断言；release 下也必须在创建/映射 wire object 时验证基地址和成员地址，失败时拒绝初始化，不能继续执行未对齐原子访问。
- 在 configure 阶段探测 32/64 位操作是否 lock-free，并把支持策略写清楚：
  - 若项目要求共享队列严格 lock-free，则不满足时 configure fatal，并列出不受支持的平台；
  - 若允许 fallback，则 `atomic_ref` 不可用或不 lock-free 时使用已验证的 GCC/Clang `__atomic`、MSVC `Interlocked` 或显式链接的原子运行库，同时保证其跨进程语义满足要求。
- 统一记录每个 API 接受的 memory order；平台 fallback 可以提供更强语义，但不能弱于调用方请求，也不能静默忽略非法 load/store order。
- 所有共享内存队列和命令索引只调用该兼容层。

验证：

- 编译矩阵覆盖 C++17/20/23。
- 用 TestUnits 的 IPC 回归覆盖单进程、多进程、生产者/消费者场景。
- 校验 32/64 位索引在 wrap-around、满队列、空队列下的正确性。
- 与 `ShmWireLayout` 同一切片验证，任何一个未完成都不得合并。

### 12. 共享内存 ABI 需要集中定义

证据：

- `ShareBlocks.h`、`ShmCaster.h`、`ParserShm.h` 分别定义 packed 共享结构。
- `ShmCaster` 与 `ParserShm` 共享队列结构存在复制关系。

风险：

- MSVC/GCC/Clang、x86_64/arm64 对 pack、匿名 union、对齐诊断存在差异。
- 未来修改一处结构后，生产者/消费者 ABI 可能不一致。

优化方案：

- 新增 `src/Share/ShmWireLayout.hpp`，集中定义共享内存 wire layout。
- 使用显式 `alignas`、固定宽度整数、必要 padding；避免让需要原子访问的对象处于 `pack(1)` 类型中，仅检查 member offset 不能保证完整对象基地址对齐。
- 增加 wire magic、layout version、endianness、total size 和能力标志；打开旧共享文件时先验证版本，禁止用新结构静默解释旧布局。
- 为每个跨进程结构增加：
  - `static_assert(sizeof(...))`
  - `static_assert(alignof(...))`
  - `static_assert(offsetof(...))`
- 与 `AtomicCompat.hpp` 共享对齐要求。

验证：

- 多进程 producer/consumer correctness test。
- 在不同 C++ 标准、编译器和架构下编译同一组 `sizeof/alignof/offsetof/version` 断言；如果业务数据结构本身不是稳定 wire type，应先转换为独立 wire DTO，不能直接嵌入编译器相关类型。

### 13. 公共头自包含性不足

证据：

- `ShareBlocks.h`、`ShmCaster.h` 使用 `memset` 但未直接 include `<cstring>`。
- `StdUtils.hpp` 使用 `std::runtime_error` 但未 include `<stdexcept>`。
- `WtKVCache.hpp` 使用 `std::function` 但未 include `<functional>`。
- `ParserShm.h` 在公共头里 `using namespace boost::asio`。

风险：

- include 顺序、预编译头、不同 STL 实现、C++20 header units 都会放大随机编译失败。
- 公共头 namespace 污染会影响调用方。

优化方案：

- 先维护明确的 public-header 清单；为清单中的每个头生成只 include 自身的 `.cpp`，并通过拥有该头的 target usage requirements 编译。不能用读取源码文本的运行时断言代替真实编译。
- 公共头直接 include 自己使用的标准库头。
- 移除公共头中的 `using namespace`。
- Boost.Asio 入口统一到 `AsioCompat.hpp`，只暴露项目自己的别名和 helper。

### 14. `fmtlib.h` 和旧 threadpool 需要隔离

证据：

- `fmtlib.h` 在公共头里定义 `FMT_HEADER_ONLY`，并为所有 enum 做泛化 formatter。
- 旧 threadpool 仍有 `boost::result_of`、`xtime`、volatile 风格代码。

风险：

- 外部已编译 fmt/spdlog 与 header-only fmt 配置可能 ABI 或 ODR 不一致。
- 新 Boost 和 C++20/23 会持续触发 deprecated warning。

优化方案：

- fmt provider 只由 CMake 决定，公共头不再无条件定义 `FMT_HEADER_ONLY`。
- 枚举格式化移到业务 wrapper，避免全局偏特化所有 enum。
- threadpool 短期隔离 warning，入口收敛；中期替换为 `std::invoke_result_t`、`std::condition_variable`、`std::chrono` 或 Boost.Asio thread_pool。

## 推荐落地路线

### 阶段 0：建立可重复基线

目标是让后续每个切片都有可信的红/绿反馈。

- 移除 `TestUnits` 交互等待，接入 CTest、label、timeout 和 `--output-on-failure`。
- 文件系统、LMDB、共享内存测试全部使用测试专属临时目录，运行前后清理自己的资源，不依赖仓库根目录残留状态。
- 用 `CMakePresets.json` 固化 Linux GCC 的 Debug/Release、C++17/20/23 和最小核心构建配置。
- 记录 `9763b70...` 基线的已知通过、失败和跳过项；已有失败不得被 portability 切片顺手掩盖。

验收：同一构建目录连续执行两次 CTest 结果一致；失败测试不阻塞等待、不访问共享固定路径、不在断言失败后继续解引用无效资源。

### 阶段 1：修正构建坐标系和标准契约

目标是让 configure 结果可解释、可复现。

- 接入 `WtPlatform.cmake`，替换 `PLATFORM/PREFIX/SUFFIX` 散落逻辑，修复 Linux arm64。
- 正确处理 native/cross compile、Apple 单架构与未来 universal 的差异；当前不支持 universal 时明确 fatal，而不是暗示已经支持。
- 引入已校验的 `WT_CXX_STANDARD`，默认 C++17；各 target 用 compile features 声明最低标准。
- `Threads::Threads` 和 `${CMAKE_DL_LIBS}` 进入语义明确的 `WT::Platform` link interface，只由实际需要的 target 链接，不放入名为 compile options 的全局容器。

验收：Linux x86_64/Linux aarch64/macOS arm64/Windows x64 的 configure fixture 输出系统、目标架构、后缀和 runtime 子目录；非法标准和不支持架构在 configure 阶段给出明确错误。

### 阶段 2：建立真正的 target 依赖模型

目标是消除全局 include/link 状态污染，并让包管理器可接入。

- 建立 `WtDependencies.cmake`，优先使用上游 config target，必要时创建 imported/interface target。
- 按低耦合到高耦合的顺序迁移：RapidJSON/fmt → nanomsg/ZMQ → Boost → CTP；每次只迁一个 provider 或一组紧密相关 target。
- 删除 `INCS/LNKS`、目录级 link/include、递归全 target 注入和 cache `FORCE`。
- vendor SDK 三态、根目录、配置库、runtime、架构和 ABI 信息统一建模。

验收：系统、自定义 prefix、Conan/vcpkg 三种依赖提供方式有代表性构建；缺可选依赖时 AUTO skip、显式 ON fatal；目标依赖图不再包含无关依赖。

### 阶段 3：统一输出、开发态部署和 install tree

目标是让构建产物和正式部署都可预测。

- 所有 target 使用 target output properties，不再使用 `EXECUTABLE_OUTPUT_PATH/LIBRARY_OUTPUT_PATH`。
- development deployment target 使用 target file generator expression，并显式依赖 producer/consumer；不再只依赖 consumer `POST_BUILD`。
- 以 `install(TARGETS)` 建立唯一正式产物清单，补齐 Linux/macOS RPATH 与 Windows DLL 规则；shell copy 脚本只调用 `cmake --install`。
- 将 strip 变成显式 packaging 选项，普通 Release 和所有测试保留符号。

验收：Ninja、Ninja Multi-Config 和至少一个 IDE 多配置生成器通过；只重建插件后 development/install tree 都更新；干净环境能加载核心插件。

### 阶段 4：低风险兼容层和公共头

目标是把普通平台差异从业务代码中抽离，不触碰共享内存 ABI。

- `FilesystemCompat.hpp` 与真实 link probe。
- `AsioCompat.hpp`，按模块迁移并保持异步生命周期/取消语义测试。
- `ModuleNameCompat.hpp`、`PlatformPause.hpp`、`CurrentDirCompat.hpp`。
- 明确 public-header 清单，生成真实 self-contained compile tests。

验收：相关 target 在 C++17/20/23 下编译；兼容头聚焦测试和公共头自包含测试通过；每个切片不混入共享内存布局修改。

### 阶段 5：共享内存 ABI 与原子访问

目标是以一个完整契约处理 wire layout 和跨进程原子语义。

- 同时实现 `ShmWireLayout.hpp` 与 `AtomicCompat.hpp`。
- 加入 layout version/magic/endian/size、显式对齐、编译期布局断言和旧文件拒绝/迁移策略。
- 明确 lock-free 要求、fallback 支持范围和 memory order 映射。

验收：跨进程、边界、恢复、跨标准、跨编译器和 x86_64/arm64 测试全部通过；性能回归在预先定义的统计阈值内。

### 阶段 6：历史模块与构建卫生

- 修正 `TraderXTPXAlgo`、`TraderHTS` 等孤儿目录和 SDK gate。
- ATP demo 明确 unsupported 平台或接入正式 imported target。
- 活跃 target 改成显式源码列表；非活跃模块至少保留 `CONFIGURE_DEPENDS` 作为过渡。
- 删除迁移期间的策略脚本重复项，把稳定规则收敛到 configure/test/lint 入口。

## 支持等级与验证矩阵

目标列表不等同于当前支持状态。每个平台组合应标记为以下等级之一：

- Tier 1：每次合并必须 configure、build、CTest、install 和 plugin smoke 全部通过。
- Tier 2：定期验证，至少通过 configure、核心 build 和聚焦测试；失败不应被描述为正式支持。
- Future：只保留设计兼容性，不作为当前发布阻塞项。

### 计划验收矩阵

| 等级 | 平台 | 编译器 | 标准 | 最低验收目标 |
| --- | --- | --- | --- | --- |
| Tier 1 | Linux x86_64 | GCC 15 | C++17/20/23 | 默认组件 + CTest + install + plugin smoke |
| Tier 1 | macOS arm64 | AppleClang | C++17/20/23 | 默认可用组件 + CTest + install + plugin smoke |
| Tier 1 | Windows x64 | MSVC | C++17/20 | 核心库 + Loader/Runner + CTest + install/DLL smoke |
| Tier 2 | Linux x86_64 | Clang + libstdc++/libc++ | C++17/20/23 | 核心库 + CTest |
| Tier 2 | Linux aarch64 | GCC/Clang | C++17/20 | 核心库 + IPC 聚焦测试 + install smoke |
| Tier 2 | macOS x86_64 | AppleClang | C++17/20 | 核心库 + CTest |
| Tier 2 | Windows x64 | clang-cl/MinGW | C++17/20 | configure + 核心库 + 聚焦测试 |
| Future | macOS universal | AppleClang | C++17/20 | universal 依赖齐备后的双架构 install smoke |

具体编译器最低版本必须另行固定；在最低版本没有被 CI 证明前，不使用“GCC 9+”等开放式承诺。

### 正确性测试

- 所有自动化测试通过 CTest 注册，禁止交互输入。
- `TestUnits` 在独立临时目录运行并连续执行两次。
- TestUnits IPC 回归覆盖单/多进程、生产者/消费者、超时和生产者重启。
- ShmCaster/ParserShm 覆盖空、满、wrap-around、恢复、layout version 不匹配和未对齐映射拒绝。
- `WtKVCache` 覆盖写入、读取、扩容、并发和跨进程 reopen。
- 完整策略回测使用固定数据与配置，比较事件序列、订单、成交、资金曲线和统计项；浮点结果定义明确容差。

### 性能测试

- 区分 microbenchmark、IPC benchmark 和完整回测，不混用阈值。
- 固定 CPU governor、CPU affinity、线程数、构建类型、输入数据和预热次数，记录硬件、内核、编译器及依赖版本。
- 每组至少 20 次有效样本后再报告 median、p95 和离散度；5 次样本不足以稳定解释 p95。
- 以预先保存的同机基线比较；小于 3% 的变化先视为噪声并复测，超过阈值也必须结合置信区间和 profile 判断，不能仅凭单次结果拒绝或接受变更。
- C++17/20/23 分别运行核心 benchmark，确认兼容层或 fallback 没有不可接受的开销。

### 打包/运行时测试

- `WT_ENABLE_CTP/ZMQ/XTP/FEMAS/YD=AUTO`：缺依赖时跳过并打印结构化原因。
- `WT_ENABLE_*=ON`：缺依赖、错误架构或不兼容 ABI 时 configure fatal。
- 对 install tree 运行 `ldd`、`otool -L` 或 Windows 依赖检查，并校验 RPATH/install name/DLL 布局。
- 在无开发依赖的容器、虚拟机或临时用户环境中做 `dlopen`/`LoadLibrary` smoke test。
- 对可再分发 runtime 使用 allowlist，打包测试同时检查不应出现的系统库和开发机绝对路径。

## 优先级清单

| 优先级 | 项目 | 影响 |
| --- | --- | --- |
| P0 | 可重复、非交互 CTest 基线 | 使所有后续验收可信 |
| P0 | 平台/目标架构归一化 | 防止输出错位和误链 SDK |
| P0 | 真正的 imported/interface target 模型 | 消除全局状态并支持包管理器 |
| P0 | C++17 最低标准契约与标准矩阵 | 防止全局 C++23 抬高门槛 |
| P1 | target output + development/install 部署模型 | 支持多配置和干净环境运行 |
| P1 | vendor SDK 三态、架构和 ABI 检查 | 支持无 SDK/多架构环境 |
| P1 | RPATH/Windows runtime dependency | 解决构建机可运行、部署机不可运行 |
| P1 | `ShmWireLayout + AtomicCompat` 独立切片 | 控制共享内存 ABI 和跨进程正确性风险 |
| P2 | Filesystem/Asio/ModuleName/PlatformPause | 收敛普通平台差异 |
| P2 | public header self-contained CI | 防止 include 顺序随机失败 |
| P3 | threadpool/fmt 历史封装、GLOB、孤儿模块 | 降低长期维护成本 |

## 建议的首批提交边界

在新的干净 worktree 中按以下顺序重建；每项必须是独立提交候选，上一项通过验收后再开始下一项：

1. 仅修 TestUnits/CTest：移除交互、临时目录隔离、注册测试；不改业务和 CMake 依赖模型。
2. 仅接入 `WtPlatform.cmake`：平台变量和测试 fixture；不同时迁移 vendor SDK。
3. 仅建立 C++ 标准契约：cache 校验、默认 17、target compile features 和 C++17/20/23 核心构建。
4. 逐依赖 target 化：先 RapidJSON/fmt，再 nanomsg/ZMQ，再 Boost；每个 provider 可继续拆分。
5. 迁移 target output properties，并建立一个 Runner 的 development deployment + install 垂直样板；样板通过后再扩到其他 wrapper。
6. 依次重放 `ModuleNameCompat`、`PlatformPause`、`CurrentDirCompat`、Filesystem 和 Asio 小切片，每个兼容层带自己的真实测试。
7. vendor SDK、完整 runtime packaging 和 `ShmWireLayout + AtomicCompat` 分别作为后续独立专题，不进入首批低风险提交。

首批提交的目标不是一次覆盖所有平台，而是建立可信测试、干净依赖边界和可复制的迁移样板；这比继续维护一个跨 150 多个文件的大 diff 更容易 review、回滚和定位问题。
