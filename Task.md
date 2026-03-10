基于 BIT-Orange/ndnSIM 实现 iRoute 实验设计的代码复用与改造方案
范围与关键结论
本报告聚焦 
1
 上的 BIT-Orange/ndnSIM 仓库，从“能直接复用什么、必须补齐什么、应当如何落地到代码文件与集成点”三个层面，给出面向 iRoute 论文实验设计（语义路由 + 网络架构 + 语法悬崖数据构造）的工程化实现路径。

结论可以概括为三点：

第一，这个 ndnSIM 代码库在网络拓扑与代价（Cost）建模、NDN 基础分组语义（Interest/Data）、以及最关键的 ApplicationParameters ↔ ParametersSha256DigestComponent 机制上，已经具备实现 iRoute discovery Interest 格式的“底座能力”，无需改动底层协议栈即可实现 iRoute 的发现面分组格式。仓库自带 ndnSIM v2.8、NFD 0.7.0 与 ndn-cxx 0.7.0（及其 Interest 实现）是这一点成立的基础。

第二，iRoute 的核心创新点（Semantic-LSA、Ingress 评分 Eq.(2)、kmax 并发探测、EWMA 可靠性反馈、域服务的 discovery reply 语义）在现有 apps 目录中没有现成实现，需要通过新增应用（App）与数据结构实现；但可以大量复用 ndnSIM 的 App/Consumer/Producer 骨架、GlobalRoutingHelper、Rocketfuel 拓扑读取器、以及 Forwarder/FIB API 来完成集成。

第三，用户需求中特别提到的 “scratch 目录”在该仓库的模块结构里并非核心承载点：该仓库更像 ns-3 的一个 module（wscript 的 top='../..' 与 module_dirs 指向 apps/helper/model/utils 等），实验场景更适合以 examples/ 方式落地（examples/wscript 会自动编译 *.cpp 及其子目录源码）。因此“把 iRoute 实验跑起来”的推荐落点是新增 examples/iroute-experiment.cpp（或 examples/iroute-* 系列），而不是依赖外部 ns-3 总仓库的 scratch。

仓库中与 iRoute 直接相关的已有能力
Interest ApplicationParameters 与 ParametersSha256DigestComponent
iRoute discovery Interest 的关键要求之一是：把查询负载放在 ApplicationParameters 里，并在 Interest 名字末尾带上 ParametersSha256DigestComponent（参数摘要组件），用于正确的 PIT/CS 匹配与缓存/聚合语义。该仓库的 ndn-cxx Interest 实现已经内置了“设置 ApplicationParameters 时自动（重）计算并追加 ParametersSha256DigestComponent”的逻辑：当调用 Interest::setApplicationParameters(...) 时，如果名字里没有 ParametersSha256DigestComponent，会自动 append；若已有则重算。

这一机制与 NDN packet format 规范对 ApplicationParameters(TLV-TYPE=36) 与 ParametersSha256DigestComponent(TLV-TYPE=2) 的定义一致。
2
 同时，ndn-cxx 官方文档也明确指出 setApplicationParameters 会重算/追加该 digest 组件。
3
 IETF 的 RFC 9508（ICN ping）同样说明：当 Interest 携带 ApplicationParameters 时，会在 Name 最后添加 ParametersSha256DigestComponent。
4

此外，NameComponent 层也提供了识别 ParametersSha256DigestComponent 的接口（isParametersSha256Digest()），且规定该类 digest 组件 TLV-LENGTH 必须是 32 字节。

对 iRoute 的落地含义是：无需修改 ndn-cxx/NFD，只要在应用侧按顺序构造 Interest（先 setName，再 setApplicationParameters），即可自然得到符合论文要求的 discovery Interest 名字格式与 digest 组件行为。

网络拓扑、域间代价与路由安装
iRoute 实验需要 ISP 级拓扑与链路代价（如 latency、hop、OSPF metric）来计算 Cost_D 并纳入 Eq.(2)。该仓库在“读 Rocketfuel/Annotated 拓扑 → 赋 OSPF metric → 用 Dijkstra 计算并安装路由”链路上是完整的：

RocketfuelMapReader 能从 Rocketfuel maps 文件生成拓扑，并在创建链路时随机化带宽/延迟，同时计算并写入 OSPF metric（比如按 referenceOspfRate/带宽比值取整）；这些信息会成为后续路由计算的权重来源。
AnnotatedTopologyReader 读入 annotated topo 后，会在 ApplyOspfMetric() 中把 “OSPF” 字段设置到对应的 ndn::Face metric 上（face->setMetric(metric)），确保转发平面与路由计算用的权重一致。
GlobalRoutingHelper::CalculateRoutes() 使用 boost::graph 运行 Dijkstra，从每个节点计算到所有 GlobalRouter 节点（及其导出前缀）的最短路，并用 FibHelper::AddRoute(..., metric=distance) 将“距离（累积权重）”作为路由 cost 写入 FIB。
其中 edge weight 直接读取 face 的 metric（face->getMetric()），与上面的 ApplyOspfMetric 完整对齐。
这套机制非常贴合你前述实验设计里的 Cost_D 输入：你既可以把链路 metric 当 hop/OSPF cost，也可以把链路 delay 作为外部度量（delay 在 GlobalRoutingHelper 中也被累积到 DistancesMap 的第三个字段，但默认不写入 FIB）。

应用骨架：Consumer/Producer 与 App 面向 iRoute 的可复用点
iRoute 落地需要两个关键应用角色：Ingress（发送 discovery Interest、评分与并发探测、触发 stage-2 fetch）与 Domain Service（响应 discovery Interest 并返回 canonical name/manifest）。现有 apps 中的两个基础类几乎可以直接借鉴其“收发与计时框架”：

ns3::ndn::App 在 StartApplication 时创建一个 appFace（AppLinkService + NullTransport）并加入 L3Protocol 的 FaceTable，为应用与 NFD forwarder 提供互通面；这是所有 iRoute 应用的统一入口。
ns3::ndn::Producer::OnInterest 的最关键可复用点是“Data 名字直接使用 Interest 的完整 Name”（包含最后的 digest 组件时仍然原样回填），完全符合 iRoute discovery reply 必须匹配请求 Name 的需求。
ns3::ndn::Consumer 提供了完整的超时重传检查框架（RetxTimer、SeqTimeouts、多路 trace source），但它默认把 sequence number append 到 Name 末尾（按前缀 + seq 的模式发 Interest），不适合直接拿来发“多域、多名字”的 discovery Interest；更适合作为参考实现。
ConsumerCbr 的频率调度方式（按 Frequency 或随机化分布调度下一包）适合作为“Query Workload 注入器”的可复用模式：IngressApp 可以同样暴露 Frequency、QueryFile、StartTime 等属性并按节律发起查询。
额外值得关注的是 AppHelper 与 FactoryCallbackApp：后者允许用回调安装一个“复杂构造参数”的应用实例，适合把离线生成的 embedding/centroids/ground truth 映射加载为 C++ 对象并注入模拟，而不受 ns-3 Attribute 系统对类型的限制。

iRoute 实验组件对照表：可复用模块与缺口
下表按你要求的 iRoute 核心组件逐项映射到仓库现状，并标注“复用/需新增或修改”的边界（表后各项在下一节给出具体改造建议与文件落点）。

iRoute 实验组件	现有实现/候选复用点	复用判断	关键说明
语义 embedding 处理（加载向量、相似度计算、KMeans 质心）	无现成模块（apps/utils 中未发现 embedding/ANN/KMeans）	需新增	ndnSIM/C++侧更适合“加载离线文件 + 轻量相似度计算”，embedding 生成与聚类应在 Python 离线做
Domain 拓扑与 cost 建模	RocketfuelMapReader、RocketfuelWeightsReader、AnnotatedTopologyReader、GlobalRoutingHelper	可直接复用	已支持 OSPF metric/Delay/队列/丢包等；GlobalRoutingHelper Dijkstra 将累积距离写入 FIB cost
Ingress 评分（Eq.(2)、soft gating、cost 归一化、权重 wi）	无	需新增	可在新 IngressApp 内实现；cost 可从 FIB 查询或离线导入
discovery Interest：ApplicationParameters + ParametersSha256DigestComponent	ndn-cxx Interest::setApplicationParameters；NameComponent::isParametersSha256Digest	可直接复用	应用侧按顺序 setName→setApplicationParameters；可检查 digest validity
Semantic-LSA 生成、传播、存储	无 iRoute 专用 LSA；但有通用 Data/Producer/GlobalRouting 基础	需新增（可复用 Data/Producer）	建议用“LSAProducer + LSASync/Flood（简化版）”实现；或用离线预加载替代传播并计量开销
kmax 并发探测（并发发 Interest、阈值 τ、首个有效响应胜出）	ConsumerWindow 提供“多 outstanding”但按 seq；不适配多域 probe	需新增	在 IngressApp 维护 pending 探测集合与回调路由；并发用 ScheduleNow 发送多个 Interest
EWMA 可靠性反馈（失败域惩罚、影响评分或候选排序）	无（NFD 另有链路层可靠性，不是同一问题）	需新增	在 IngressApp 内维护 per-domain EWMA；stage2 fetch 成功/失败触发更新
discovery reply：域服务返回 canonical name/manifest	Producer OnInterest 可复用“DataName=InterestName”模式	部分复用 + 需新增解析/编码	DomainServiceApp 需解析 ApplicationParameters 里的查询并返回 TLV 内容

需要新增或改造的组件与具体工程建议
本节按你点名的关键点（分组格式、Semantic-LSA 字段、评分 Eq.(2)、kmax 与 EWMA、域服务语义、cost 融合）给出最落地的改造建议。涉及 iRoute 论文细节的地方以你上传的 iRoute PDF 为准。

discovery Interest 与 Data 处理：名字、ApplicationParameters 与 digest
现状与可复用点

现有 Consumer/Producer 并不使用 ApplicationParameters（仓库搜索未发现 apps 内对 setApplicationParameters( 的调用），因此需要你在新应用中显式构造。
但 ndn-cxx 的 Interest 已经实现 setApplicationParameters 自动追加/重算 ParametersSha256DigestComponent。
建议的实现方式

IngressApp 构造 discovery Interest 时，严格按下面顺序：
interest->setName(Name("/<DomainID>/iroute/disc/<SemVerID>"))
interest->setCanBePrefix(false)（避免误匹配，保持 discovery Interest 的精确性）
interest->setInterestLifetime(...)
interest->setApplicationParameters(payloadBytes)：此调用会确保名字末尾存在 ParametersSha256DigestComponent。
DomainServiceApp 收到 Interest 时：
用 interest->getName() 直接作为 Data name 回填（Producer 已这么做；这是最稳妥满足 PIT 匹配的方式）。
用 interest->getApplicationParameters() 解析查询负载。你可以选择两种工程模式：
向量直传模式：ApplicationParameters = query embedding（float32 数组）+ semver + queryId；DomainService 做局部 ANN/暴力检索。
轻量仿真模式：ApplicationParameters = queryId；DomainService 查离线预计算表（queryId→canonical name 或 NotFound）。这更适合把 iRoute 的实验重点放在“语义到拓扑映射”和“发现面路由”，避免把计算瓶颈引入 ns-3 单线程仿真。
digest 校验：
NameComponent 已提供 isParametersSha256Digest 判断；也可使用 Interest 的 digest validity 检查（ndn-cxx/ndnSIM 文档指出 Interest 支持自动校验 digest 的开关与接口）。
5
 
3
实验中建议开启校验并记录异常计数，这能在你后续做“恶意域/错误实现注入”时作为健壮性指标。
iRoute Semantic-LSA：结构、字段与传播方式
现状与缺口

仓库目前没有 iRoute 的 Semantic-LSA 数据结构或同步机制（也未包含 NLSR/PSync 的直接实现），因此需要新增。

推荐的数据结构做法

新建 apps/iroute/iroute-tlv.hpp：定义 iRoute 私有 TLV 类型号（建议使用实验范围 TLV），并定义 SemanticLsa、CentroidEntry、CostMetric 等子块。
新建 apps/iroute/semantic-lsa.hpp/.cpp：提供
SemanticLsa::wireEncode() / wireDecode()（基于 ndn-cxx EncodingBuffer/Block）
字段：semVer、seqNo、originId(domain)、{(centroidId, vector, ri, wi)}*、可选 routingCostPolicy 等（字段名与论文一致，便于对照实现与写论文）。
传播/分发的工程折中

你需要在“忠于论文”与“仿真可跑”之间做一个明确取舍：

若你重点在发现面机制验证（语法悬崖、kmax、EWMA、成本项），建议采用 “静态 LSA + 启动阶段拉取”：
每个域 gateway 运行 IrouteLsaProducerApp，对固定名字前缀（如 /iroute/lsa/<OriginID>/<SemVerID>）返回 Data（内容是 Semantic-LSA Block）。
每个 ingress 运行 IrouteLsaConsumerApp，在仿真开始时拉取全部域的 LSA 并缓存到内存结构；控制面开销可用“实际传输字节数/包数”或“LSA 大小 × 更新次数”计量。
若你需要展示控制面收敛过程，可以进一步实现 “周期性更新 + 简化同步”：每隔 T 秒更新 SeqNo 并让 ingress 重新拉取（无需真正 PSync，只要保持更新频率与总字节量可控即可）。
在 ndnSIM 中，把 LSA 当作普通 Data 传播不需要动转发平面；只要保证这些 LSA 名字能被路由到对应域节点即可（GlobalRoutingHelper + AddOrigin 即可）。

Ingress 评分与候选域选择：Eq.(2)、soft gating、cost 归一化
现状与缺口

现有 apps 中没有任何“向量评分、多域候选排序”的逻辑；必须新增 IngressApp。你可以复用 ndnSIM App 的收发机制与 ns-3 的 Scheduler。

cost 获取的最佳复用路径：直接读 Forwarder FIB

为了把 Cost_D 融入 Eq.(2)，你需要从拓扑推导“从 ingress 到 domain D 的网络代价”。最工程化、且完全复用现有栈的方法是：

用 GlobalRoutingHelper 计算并安装路由，且把每个域的 routable 前缀（如 /<DomainID>）作为 origin 发布。
在 IngressApp 内通过 L3Protocol->getForwarder()->getFib() 获取 FIB，并对 /<DomainID> 执行 findExactMatch 或 LPM，得到 NextHop 列表与它们的 cost。
从 NextHop 中取最小 cost 作为 Cost_D（或按论文定义选择）。NextHop 的 getCost() 已提供。
6
Cost_max 可在启动时扫描全部域 cost 取最大值（或按固定上界归一化），并缓存。
这一方案的优势是：你无需重复实现 Dijkstra，也无需改 GlobalRoutingHelper 暴露内部 distances；完全站在 NFD/ndnSIM 原生 API 上实现，实验可解释性也更强（“cost 就是路由安装时写进 FIB 的代价值”）。

soft gating 与权重 wi 的落地建议

在代码实现上，把每个域 D 的 LSA 解析为：vector<CentroidEntry>，每个 entry 里至少有 {C_i, r_i, w_i}。
对每个域计算 S_D(q) = max_i Score(q, C_i, r_i, w_i, Cost_D)（论文 Eq.(2)），并显式拆成三个子项便于调参/画图：
Sim(q, C_i)（cosine 或 dot，需与离线 embedding 归一化一致）
Gate(q, r_i)（soft gating，常见实现是 sigmoid 或分段函数；建议把实现写成可插拔策略，方便做 ablation）
CostTerm(Cost_D/Cost_max)（归一化后对 Score 施加惩罚项）
把 w_i 与 EWMA 可靠性（下一小节）都作为“可加权项”，并提供 ns-3 Attributes 配置权重系数，方便你复现实验中“参数 sweep”。
kmax 并发探测与阈值 τ：并发控制、首个有效响应、失败回退
现状与缺口

ConsumerWindow 能维持多 outstanding Interests，但其语义是“同一前缀下按 seq 递增流水线”，不适用于“对不同域前缀同时发探测”。因此建议在 IngressApp 中自行管理并发探测集合，而不是继承 ConsumerWindow。

实现建议

为每次查询生成一个 QueryContext，包含：queryId / qVector / semVerId / pendingProbes(map<Name, DomainID>) / bestReply 等。
先计算所有域的 S_D(q) 并排序；若 top1 ≥ τ，则只向 top1 域发送一个 discovery Interest；否则向 top-k 发送并发 Interests，k = min(kmax, #domains)。
并发发送可用 Simulator::ScheduleNow 立即发送多条 Interest；每条 interest 的 name 包含域前缀，因此转发面会自然把它们送往不同域。
当收到任一 domain 的 discovery reply Data：
若是 Found（能解析出 canonical name/manifest），立即将 QueryContext 标记完成，并触发 stage2 fetch；其他未完成 probe 即便返回也直接忽略（或只记录为“late reply”用于统计）。
若是 NotFound/错误，则把该域标记为“本轮失败”，等待其他 probe；若全部失败再按策略回退（例如扩大 k、或直接宣告失败）。
超时：每条 probe 都应有自己的 probeTimeout；超时触发也算“失败样本”，用于 EWMA 更新。
EWMA 可靠性反馈：更新规则与与评分的耦合
现状与缺口

仓库里唯一出现 “EWMA” 的地方主要是 tracer 文档与 L3RateTracer 的统计平滑（alpha=0.8），并不是 iRoute 的域可靠性机制。 因此你需要在 IngressApp 内实现 per-domain EWMA。

工程化建议

在 IngressApp 维护 unordered_map<DomainID, double> ewmaSuccess，初值可设为 1.0 或全局均值。
“成功/失败”的定义应与论文一致：通常是 stage2 fetch 在超时窗口内成功拿到内容（或者拿到 manifest 并完成后续获取）。
更新函数写成独立方法：UpdateReliability(domain, successBool)，并允许设置 alpha（ns-3 Attribute）。
在评分 Eq.(2) 中加入可靠性惩罚可用两种常见策略（便于做 ablation）：
乘法：S'_D = S_D * ewmaSuccess[D]
加法惩罚：S'_D = S_D - λ*(1-ewmaSuccess[D])
对“不可靠域”的 penalty 还可以影响 kmax 探测的排序（例如将 ewma 作为 tie-breaker）。
Domain Service 行为：正确响应 discovery Interest 并返回 canonical name/manifest
可复用点与必须改造点

Producer 的 OnInterest 已满足“DataName = InterestName”的核心约束。 但 iRoute Domain Service 还必须做三件事：解析 parameters、执行（或模拟）域内检索、编码 discovery reply。

建议实现

新建 IrouteDomainServiceApp（继承 App），暴露属性：
DomainId、SemVerId、LocalIndexFile（queryId→canonicalName/score 或 doc embeddings 文件）、ReplyFreshness 等。
OnInterest 里：
校验 Interest name 前缀是否为 /<DomainID>/iroute/disc/<SemVerID>，并检查最后一段是否为 ParametersSha256DigestComponent（可选但推荐），异常则返回 Nack 或直接 ignore。
解析 ApplicationParameters：要么取 queryId，要么取 qVector。
依据本域数据决定 Found/NotFound，并构造 DiscoveryReply TLV（至少包含 status、canonical name、可选 confidence/manifest info）。
data->setName(interest->getName())，设置 freshness，设置 content 为 wireEncode 后的 TLV buffer，并 data->wireEncode() 后发送。
工程改动清单与集成点
推荐新增文件与目录
建议把 iRoute 实现收敛到 apps/iroute/ 子目录（便于和原生 consumer/producer 并存）：

apps/iroute/iroute-tlv.hpp：TLV 类型号与通用编码辅助
apps/iroute/semantic-lsa.hpp/.cpp：Semantic-LSA 结构体与 wireEncode/wireDecode
apps/iroute/discovery-messages.hpp/.cpp：DiscoveryRequest/DiscoveryReply 的序列化
apps/iroute/iroute-ingress.hpp/.cpp：IngressApp（评分 Eq.(2)、kmax 探测、EWMA 更新、stage2 fetch）
apps/iroute/iroute-domain-service.hpp/.cpp：DomainServiceApp（解析 parameters、返回 canonical name/manifest）
apps/iroute/iroute-dataset-loader.hpp/.cpp：读取离线生成的域语义数据、query workload、ground truth 映射（CSV/TSV/JSON 皆可；建议优先 TSV/CSV 以减轻依赖）
这些文件无需改动根 wscript：模块会用 ant_glob('**/*.cpp') 自动编译 apps 下的所有 cpp。

推荐新增 examples 作为“可运行入口”
由于仓库 examples/wscript 会自动编译每个 examples/*.cpp 并包含同名子目录的辅助 cpp，你可以新增：

examples/iroute-experiment.cpp
可选的 examples/iroute-experiment/ 子目录放一些实验脚本/帮助类
examples/wscript 的这一行为意味着你几乎不需要改 build scripts，即可把实验场景编成可执行程序。

场景里拓扑可以来自：

RocketfuelMapReader 动态生成并 SaveTopology；或
直接使用 annotated topo 文件（格式参照 examples/topologies/*.txt，如 topo-load-balancer）。
面向 Claude Code 的详细提示词
text
复制
你正在修改 BIT-Orange/ndnSIM（ndnSIM v2.8，内置 NFD 0.7.0 与 ndn-cxx 0.7.0）以实现 iRoute 论文的实验协议与数据面/控制面逻辑。

现有能力概览：
- ndn-cxx Interest 已支持 setApplicationParameters()，会自动（重）计算并追加 ParametersSha256DigestComponent 到 Interest 名字末尾；NameComponent 也能识别 params digest。
- ndnSIM apps 提供 App/Consumer/Producer 基础骨架；Producer::OnInterest 直接用 InterestName 作为 DataName。
- 拓扑/代价：RocketfuelMapReader/AnnotatedTopologyReader 可设置 OSPF metric（face->setMetric），GlobalRoutingHelper::CalculateRoutes 用 Dijkstra 计算最短路并把累积距离写进 FIB cost（FibHelper::AddRoute setCost(metric)）。
- Forwarder API 可从 L3Protocol->getForwarder()->getFib() 读取 FIB，fib::Entry->getNextHops() 中每个 NextHop 有 getCost()，可作为 Cost_D。

目标：实现 iRoute 实验所需的以下模块（重点在 apps 与 examples）：
1) Discovery Interest 格式与处理：
   - Ingress 发送 discovery Interest：Name = /<DomainID>/iroute/disc/<SemVerID>，payload 放在 ApplicationParameters，确保最终 Name 末尾携带 ParametersSha256DigestComponent。
   - DomainService 收到 Interest 后解析 ApplicationParameters（queryId 或 query vector），返回 DataName=InterestName，Data content 用自定义 TLV 编码 DiscoveryReply（Found/NotFound、canonical name 或 manifest、confidence 等）。
2) Semantic-LSA：
   - 定义 Semantic-LSA 的 TLV 编码与结构（字段含 semVer、SeqNo、OriginId、centroid 列表，每个 centroid 有 vector、ri、wi、centroidId；必要时带 cost/策略字段）。
   - 实现一个 LsaProducerApp（按固定名字前缀返回 LSA Data），以及 Ingress 侧加载/缓存 LSA 的组件（可简化为启动阶段拉取）。
3) Ingress 评分与候选域选择（实现论文 Eq.(2)）：
   - 对每个域 D，基于其 LSA centroids 计算 S_D(q)=max_i Score(q,C_i,ri,wi,Cost_D)。
   - 实现 soft gating、Cost_D/Cost_max 归一化、权重项，并允许通过 ns-3 Attributes 配置权重系数与阈值 τ。
   - Cost_D 获取：优先从 Forwarder FIB 查询 /<DomainID> 的 fib::Entry 并取最小 nextHop cost。
4) kmax 并发探测与回退：
   - 若 top1 分数 < τ，则并发向 top-k 域发送 discovery Interests（k<=kmax），采用“首个 Found 响应胜出”，其余响应忽略；并实现 per-probe timeout。
5) EWMA 可靠性反馈：
   - 实现 per-domain ewmaSuccess，stage2 fetch 成功/失败触发更新；并把可靠性作为评分惩罚项或排序因子。
6) stage2 fetch：
   - Ingress 在收到 discovery reply 的 canonical name 后发起普通 Interest 获取内容；成功/失败用于 EWMA 更新与实验统计。
7) 统计与验证：
   - 在 IngressApp 中统计：每个 query 的选域、探测次数、Discovery 成功率、stage2 成功率（Recall proxy）、控制面开销（Interest/Data wire size 总和，注意需要显式 wireEncode Interest/Data 才能准确计量）。
   - 输出 CSV/TSV 日志，便于画图（Accuracy vs M；Accuracy vs Entropy；Overhead vs M 等）。

要修改/新增的重点文件建议：
- 新增：apps/iroute/iroute-tlv.hpp、apps/iroute/semantic-lsa.hpp/.cpp、apps/iroute/discovery-messages.hpp/.cpp
- 新增：apps/iroute/iroute-ingress.hpp/.cpp、apps/iroute/iroute-domain-service.hpp/.cpp、apps/iroute/iroute-dataset-loader.hpp/.cpp
- 新增：examples/iroute-experiment.cpp（构建拓扑、安装 NDN stack、安装 GlobalRouting、部署域与应用、加载离线数据、运行 query workload、输出指标）
- 尽量不改动 ndn-cxx/NFD 核心；在应用层完成协议与逻辑。

实现细节注意：
- 构造 discovery Interest 时，先 setName 再 setApplicationParameters，避免 digest 组件不一致。
- DomainService 返回 Data 时必须 setName(interest->getName())，确保包含 params digest。
- 开销统计不要依赖 L3RateTracer 的 Interest size（因为 Interest 可能没有 wire），建议在发送前显式调用 interest->wireEncode() 并记录 size。
- 代码风格保持与 ndnSIM apps 一致（TypeId 注册、Attributes 配置、NS_LOG_* 日志）。
测试与验证建议
协议正确性与编码一致性测试
ApplicationParameters 与 digest 组件一致性
在一个最小拓扑（2 个节点）里，让 Ingress 发送带 ApplicationParameters 的 discovery Interest，并在 DomainService 侧断言：Interest name 最后一个组件 isParametersSha256Digest()==true，同时 DataName 必须等于 InterestName。

Semantic-LSA wireEncode/wireDecode 互操作
对同一份 LSA 对象：producer 端 wireEncode 写入 Data content，consumer 端 wireDecode 还原后逐字段对比（semVer、seqNo、centroids 数、每个 centroid 的向量长度/数值范围/ri/wi）。

cost 读取路径可用性
在安装 GlobalRoutingHelper 路由后，IngressApp 调用 forwarder->getFib()->findExactMatch(/DomainID)，检查能拿到 nextHop cost 且随拓扑变化而变化（例如切换 OSPF metric 或链路失效）。

实验场景验证
语义对齐场景（Ideal Alignment）
把同一语义簇内容映射到拓扑相邻域，并让域内 LSA centroids 对应集中；验证：iRoute 的 top1 命中率高、且与“简单前缀/就近域”基线差距小（不退化）。

语法悬崖场景（Syntactic Cliff）
按你给出的构造：同一语义簇被打散到拓扑距离远的多个域，且命名前缀强异构；验证：

iRoute：Ingress 仍能在 top-k（k<=kmax）探测中稳定命中包含相关内容的域；
baseline（若你实现）：需要维护更多前缀或召回显著下降。
M（广告预算/质心数）与开销/准确率曲线
离线用 KMeans 生成不同 M，LSA 中 centroids 数随 M 变化；在线记录：
Retrieval accuracy/成功率随 M 增加快速上升后趋缓；
控制面开销随 M 近似线性增加（LSA 体积与 discovery scoring 开销均增加）。
指标采集实现注意点（非常关键）
如果你要精确计量 iRoute 的控制面开销（含 discovery Interest 的 ApplicationParameters 字节），不要完全依赖 L3RateTracer 对 Interest 的字节统计，因为该 tracer 只有在 interest.hasWire() 时才累加 interest.wireEncode().size()；而 AppLinkService 并不会自动 wireEncode Interest。

更稳妥的做法是：在 IngressApp 发送 discovery Interest 前显式调用 interest->wireEncode() 并累加 size；DomainService 侧对 Data 已经可参考 Producer 的做法（它显式 data->wireEncode()）。

这样，你可以在应用日志中分别输出：

discovery Interests 发送字节数/包数
discovery Replies Data 字节数/包数
LSA 拉取/更新字节数/包数
stage2 fetch Interests/Data 字节数/包数
以及每 query 的探测次数、命中域、stage2 成功与否（用于 recall/accuracy 计算）
这些日志配合你前述的离线 ground truth（Top-K objects 及其域分布）即可完成论文实验指标闭环。