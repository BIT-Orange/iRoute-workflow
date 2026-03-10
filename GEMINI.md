# Gemini Context: ndnSIM (ns-3 based NDN Simulator)

## Project Overview
This project is **ndnSIM**, a Named Data Networking (NDN) simulator based on the **ns-3** network simulator (version 3.35). It allows for simulation of NDN experiments using implementations of basic NDN primitives (ndn-cxx) and the NFD (Named Data Networking Forwarding Daemon).

The current workspace focuses on **iRoute v2**, a semantic routing extension involving semantic vectors, discovery, and fetch workflows.

### Key Directories
*   `ns-3/`: The main simulator directory containing the `waf` build system and source code.
*   `ns-3/src/ndnSIM/`: The core ndnSIM module source code.
*   `ns-3/scratch/`: Contains user simulation scripts. Currently populated with **iRoute v2** experiments (e.g., `iroute-v2-demo.cc`, `iroute-v2-exp-*.cc`).
*   `pybindgen/`: Python bindings generator (dependency).

## Building and Running

**Important:** All build and run commands must be executed from the `ns-3/` directory.

### Prerequisites
*   Ensure you are in the `ns-3/` directory:
    ```bash
    cd ns-3
    ```

### Configuration and Build
To configure the project (usually required only once or after adding new files):
```bash
./waf configure --enable-examples
```

To build the project:
```bash
./waf
```

### Running Simulations
To run a simulation (e.g., the iRoute v2 demo):
```bash
./waf --run iroute-v2-demo
```
*Note: The `--run` argument takes the program name (filename without extension), not the path.*

To run with verbose logging enabled (if supported by the script):
```bash
./waf --run "iroute-v2-demo --verbose=1"
```

## Development Conventions

*   **Coding Style:** Follows ns-3 coding conventions (snake_case for files, specific indentation).
*   **Logging:** Uses `NS_LOG_COMPONENT_DEFINE` for component-based logging. Enable specific logs via `NS_LOG="ndn.IRouteV2Demo=level_all|prefix_func" ./waf --run ...` or script-specific arguments.
*   **New Simulations:** Place new simulation scripts in `ns-3/scratch/`. They are automatically detected by `waf`.

## Current Context: iRoute v2
You are currently working on **iRoute v2**, which implements a two-stage discovery + fetch workflow.
*   **Topology:** Consumer <-> Ingress <-> Core <-> Producer
*   **Key Components:** `IRouteDiscoveryConsumer`, `SemanticStrategy`, `IRouteApp`, `SemanticProducer`.
*   **Experiments:** Various scaling and performance experiments are located in `ns-3/scratch/`.

本文件用于指导 Gemini 在本仓库内实现/改造 iRoute 相关实验代码。目标是：最大化复用 ndnSIM 现有能力（拓扑、路由安装、App 框架、Interest/Data 语义），把 iRoute 的“发现面（discovery）+ 评分 + Top-K 探测 + 回退 + stage-2 fetch + EWMA 反馈 + 指标采集”落在应用层（apps/ 与 examples/），尽量不改 ndn-cxx/NFD 栈。

---

## 1. 目标与边界

### 1.1 目标（必须完成）
1) 实现 discovery Interest：Name 可路由（/DomainID 前缀），语义负载放 ApplicationParameters，并正确产生 ParametersSha256DigestComponent。  
2) 实现 Domain Service：解析 ApplicationParameters，返回 discovery reply（Found/NotFound + canonical name/manifest + 可选置信度等），且 DataName 必须等于 InterestName（包含 digest）。  
3) 实现 Semantic-LSA：可 wireEncode/wireDecode 的 TLV 格式（含 semver/seq/origin/centroids 等）。  
4) 实现 Ingress 评分与选域：实现论文 Eq.(2) 的结构化评分（相似度 + gating + 成本项 + 权重/可靠性项），支持阈值 τ 与 kmax Top-K 并发探测。  
5) 实现 per-domain EWMA 反馈：以 stage-2 fetch 的成功/失败更新可靠度，并参与后续评分/排序。  
6) 指标采集：输出可画图的 CSV/TSV（准确率 proxy、探测次数、时延、控制面开销字节数、失败率等）。  
7) 实验入口：新增 examples/ 下可直接运行的实验脚本（单文件或多文件）。

### 1.2 边界（尽量不要做）
- 不要引入重型依赖（Faiss/PyTorch/ANN 库等）到 C++ 仿真侧。向量生成与聚类在 Python 离线完成；ndnSIM 侧只做“加载 + 轻量计算（dot/cos）+ 逻辑控制”。
- 不要修改 ndn-cxx/NFD 栈行为（除非确实无法实现）。本实现假设 ndn-cxx 已支持 setApplicationParameters 自动追加/重算 digest。

---

## 2. 仓库事实（实现时必须遵守）

1) ndn-cxx Interest::setApplicationParameters(...) 会自动追加/重算 ParametersSha256DigestComponent（若 Name 中不存在则 append，存在则更新）。  
   **约束**：构造 discovery Interest 必须遵循顺序：`setName(...)` → `setApplicationParameters(...)`，避免 digest 不一致。

2) Producer 侧的典型逻辑是：`DataName = InterestName`（这对 PIT 匹配非常关键，尤其当 Interest Name 包含 params digest）。  
   **约束**：DomainService 的回复 Data 必须 setName(interest.getName())，否则 consumer 侧可能无法正确匹配。

3) 拓扑与 cost：Rocketfuel/AnnotatedTopologyReader 可设置 face metric；GlobalRoutingHelper 使用 Dijkstra，写入 FIB 的 cost 就是累计 metric（可作为 Cost_D）。  
   **约束**：Ingress 取 Cost_D 时，优先从 Forwarder 的 FIB 查 /<DomainID> 的 nextHop 最小 cost。

4) 工程落点推荐：该仓库更像 ns-3 module，实验入口适合放在 `examples/`（examples/wscript 会自动编译 examples/*.cpp）。

---

## 3. 目录与文件规划（建议严格按此创建）

### 3.1 新增目录
- `apps/iroute/`：iRoute 相关 app 与消息/TLV/数据加载组件
- `examples/iroute-experiment.cpp`：实验入口（可拆成多个 examples/iroute-*.cpp）

### 3.2 建议新增文件清单
1) `apps/iroute/iroute-tlv.hpp`  
   - 统一管理 TLV Type 常量、编码辅助函数（写入 uint、string、blob、vector<float> 等）
2) `apps/iroute/semantic-lsa.hpp/.cpp`  
   - SemanticLsa 结构体 + wireEncode/wireDecode
3) `apps/iroute/discovery-messages.hpp/.cpp`  
   - DiscoveryRequest/DiscoveryReply 的 TLV 编码与解析
4) `apps/iroute/iroute-dataset-loader.hpp/.cpp`  
   - 读取离线生成的：domain centroids、query workload、ground truth 映射、可选 domain 内检索表
5) `apps/iroute/iroute-domain-service.hpp/.cpp`  
   - DomainServiceApp：OnInterest 解析 params → 查询（真实/模拟）→ 生成 reply Data
6) `apps/iroute/iroute-ingress.hpp/.cpp`  
   - IngressApp：加载 LSA、评分 Eq.(2)、kmax 并发探测、阈值 τ、stage-2 fetch、EWMA 更新、日志/统计

> 编译说明：该仓库的 wscript 通常会用 `ant_glob('**/*.cpp')` 自动纳入 apps 下新增 cpp；但仍需确保文件路径在 module_dirs 覆盖范围内。

---

## 4. 协议与数据格式（必须对齐）

### 4.1 Discovery Interest 命名
- Name 形如：`/<DomainID>/iroute/disc/<SemVerID>/...`  
- ApplicationParameters：承载 query 负载（queryId 或向量）  
- 关键：setApplicationParameters 后，Name 末尾应包含 ParametersSha256DigestComponent

### 4.2 DiscoveryRequest / DiscoveryReply（TLV 建议）
建议 minimal 可跑版本：

DiscoveryRequest（ApplicationParameters 内 TLV）：
- QueryId（uint64 或 string）
- SemVerId（uint32 或 string，可选）
- QueryVector（可选：float32[N]；若使用 queryId-only 模式则省略）

DiscoveryReply（Data content 内 TLV）：
- Status：FOUND / NOT_FOUND / ERROR
- CanonicalName：string（FOUND 时必带，例如 `/netflix/movies/manifest/v1`）
- Confidence：float（可选）
- Extra：可选字段（比如 manifest size、top-k score、domainId echo 等）

### 4.3 Semantic-LSA（TLV 建议字段）
SemanticLsa：
- semVer（必须）
- seqNo（必须）
- originId / domainId（必须）
- centroidEntries（重复块）：
  - centroidId
  - vector（float32[N]）
  - r_i（radius / gating 参数）
  - w_i（权重）

> TLV Type 号：使用“实验私有范围”的 type 值，集中定义在 iroute-tlv.hpp，避免与标准 TLV 冲突。

---

## 5. iRoute 运行时逻辑（Ingress / DomainService）

### 5.1 启动阶段（建议简化实现）
- Ingress 在仿真开始时加载所有域的 Semantic-LSA（可用“静态 LSA + 启动拉取”两种之一）：
  1) 静态加载（推荐最先做）：从本地文件直接读入 LSA（绕开控制面同步复杂性）。
  2) 启动拉取：每个域运行 LsaProducerApp，Ingress 在 t=0 拉取 `/iroute/lsa/<DomainID>/<SemVerID>` 数据。

### 5.2 Eq.(2) 评分拆分实现（便于消融/画图）
对域 D 的评分：
- 相似度项：Sim(q, C_i)（cosine 或 dot；向量需离线归一化一致）
- gating 项：Gate(q, r_i)（sigmoid 或分段函数）
- 成本项：CostTerm(Cost_D / Cost_max)（从 FIB 取 Cost_D；Cost_max 可启动扫描求 max）
- 权重项：w_i（来自 LSA）
- 可选可靠性项：EWMA(D)

建议把 Score 写成结构化函数，输出中间项便于 debug：
- sim, gate, costNorm, costPenalty, weight, ewma, finalScore

### 5.3 kmax 并发探测与阈值 τ
- 先对所有域算 S_D 并排序
- 若 top1 >= τ：只探测 top1
- 否则：并发探测 top-k（k = min(kmax, #domains)）
- “首个 FOUND 胜出”：收到任一 FOUND 即触发 stage-2 fetch，其他迟到 reply 仅记为 late reply 统计
- 超时：每个 probe 有独立 timeout，超时计为失败样本（用于 EWMA）

### 5.4 stage-2 fetch 与 EWMA 更新
- 收到 FOUND → 取 canonical name/manifest prefix → 发送普通 Interest 获取内容（或 manifest）
- 成功/失败定义建议以 stage-2 在窗口内完成为准
- EWMA 更新：`ewma = alpha*new + (1-alpha)*old`（new=1/0）
- EWMA 的使用：
  - 乘法：S'_D = S_D * ewma[D]
  - 或加法惩罚：S'_D = S_D - λ*(1-ewma[D])

---

## 6. 离线数据输入（DatasetLoader 约定）

为了让实验可复现、且避免把语义计算塞进 ns-3，建议用以下离线文件：

1) `domains.tsv`：域列表  
   - domainId, routablePrefix（如 `/att`）, lsaFile

2) `lsa_<domain>.bin/tsv`：每域的 centroids  
   - centroidId, r_i, w_i, vector(float32[N])  
   - 若用二进制更紧凑，优先二进制；否则 TSV 也可以（但解析慢）

3) `queries.tsv`：查询工作负载  
   - queryId, queryText(optional), queryVector(optional), groundTruthDomain(optional)

4) `truth.tsv`：ground truth（用于 accuracy/recall proxy）  
   - queryId, trueDomainId, canonicalName（可选）

5) `domain_local_index.tsv`（可选）：DomainService 的“模拟检索表”  
   - queryId -> FOUND/NOT_FOUND + canonicalName + confidence  
   - 这样 DomainService 不需要真正做 ANN，只做表查找。

---

## 7. 指标采集（必须可画图）

IngressApp 输出建议：
- 每 query：
  - queryId
  - selectedDomain（最终 FOUND 的域）
  - isHit（是否命中 ground truth）
  - numProbes（探测域数量）
  - discoveryRtt / firstReplyRtt
  - stage2Rtt / stage2Success
  - top1Score, tau, kmax
  - costSelected, costMax
  - ewmaSelectedBefore/After

控制面/数据面字节开销（非常关键）：
- 统计 discovery Interest / reply Data / LSA Data / stage2 Interest+Data 的字节数
- 注意：AppLinkService 不一定自动 wireEncode Interest；为了精确统计，发送前显式调用 `interest.wireEncode()` 并计 size。Data 侧一般会 wireEncode。

输出文件建议：
- `results/iroute-per-query.csv`
- `results/iroute-summary.csv`（汇总：hit rate、avg probes、p95 latency、bytes 等）

---

## 8. 工程实现规范（写代码时必须遵守）

1) 使用 ns-3 TypeId/Attributes 暴露参数（tau、kmax、alpha/beta/λ、timeouts、文件路径等）。  
2) 使用 NS_LOG_* 输出关键 debug：  
   - digest 是否存在、params size、解析成功与否  
   - 每域评分分解项  
   - probe 发送/超时/收到 reply  
3) 避免继承 ConsumerWindow 做并发探测（其语义偏 seq pipeline，不适配多域多 name）。  
4) Ingress 内维护 QueryContext（queryId、pending probes、bestReply、timers、状态机）。  
5) 数据结构优先用简单容器（vector/unordered_map），避免复杂模板和重依赖。

---

## 9. 最小可用验证（必须先跑通再扩展）

### 9.1 协议正确性
- 2 节点拓扑：Ingress → DomainService  
  - Interest 发出后检查：Name 末尾是 ParametersSha256DigestComponent  
  - DomainService 回复 Data：DataName == InterestName（包含 digest）  
  - Consumer 能正确收到 Data

### 9.2 LSA 编解码
- 单域单 centroid：producer wireEncode → consumer wireDecode → 字段逐项一致

### 9.3 cost 获取
- 安装 GlobalRouting 后，Ingress 对 `/<DomainID>` 查 FIB，能拿到 nextHop cost；链路 metric 改变后 cost 会变化

---

## 10. 任务拆解（Gemini 执行顺序建议）

1) 搭框架：新增 apps/iroute 目录与空壳 TypeId App（Ingress/DomainService）  
2) 实现 DiscoveryRequest/Reply TLV（先用 queryId-only）  
3) 实现 DomainService：按 queryId 查表返回 FOUND/NOT_FOUND  
4) 实现 Ingress：单域探测（无 kmax、无 Eq(2)）跑通 end-to-end  
5) 加入 LSA 加载 + 评分 Eq(2)（先只用 sim 项）  
6) 加入 τ 与 kmax 并发探测 + 超时  
7) 加入 stage-2 fetch + EWMA 更新  
8) 加入完整指标采集（含 wireEncode bytes）  
9) 新增 examples/iroute-experiment.cpp：构建拓扑、部署域、加载数据、运行 workload、输出结果

---

## 11. 输出要求（提交 PR/补丁时）
- 所有新增文件必须可编译、可运行 examples
- examples 运行后能产生 results/*.csv
- 代码风格与现有 ndnSIM apps 接近（TypeId/Attributes/NS_LOG、避免过度抽象）
- 不修改 ndn-cxx/NFD 栈（除非明确写出原因与最小补丁）

---
