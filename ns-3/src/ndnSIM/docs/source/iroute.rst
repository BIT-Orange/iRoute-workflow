.. _iroute:

iRoute Semantic Routing Protocol
================================

iRoute is a semantic routing protocol for Named Data Networking that enables content-based 
routing using semantic embeddings. It implements a two-stage discovery and fetch workflow
as described in the iRoute paper.

Overview
++++++++

The iRoute protocol introduces semantic-aware routing to NDN, allowing consumers to find
content based on semantic similarity rather than exact name matching. Key features include:

- **Semantic Vector Embeddings**: Content and queries are represented as high-dimensional vectors
- **Two-Stage Workflow**: Discovery phase finds the best content, Fetch phase retrieves it
- **Multi-Centroid Advertisements**: Domains advertise semantic centroids via LSAs
- **Gated Scoring Formula**: Combines semantic similarity with network cost
- **EWMA Success Tracking**: Adaptive penalty for unreliable domains

Architecture
++++++++++++

The iRoute implementation consists of the following components:

.. code-block:: text

   ┌─────────────────────────────────────────────────────────────────┐
   │                     iRoute Architecture                         │
   ├─────────────────────────────────────────────────────────────────┤
   │                                                                 │
   │  Applications Layer                                             │
   │  ├── IRouteDiscoveryConsumer  (Two-stage consumer)              │
   │  ├── IRouteApp                (LSA control plane)               │
   │  ├── SemanticProducer         (Content producer)                │
   │  └── SemanticConsumer         (Basic semantic consumer)         │
   │                                                                 │
   │  Extensions Layer                                               │
   │  ├── SemanticStrategy         (NFD forwarding strategy)         │
   │  ├── RouteManager             (Semantic RIB per node)           │
   │  ├── RouteManagerRegistry     (Global RIB registry)             │
   │  └── SemanticVector           (Vector encoding/decoding)        │
   │                                                                 │
   └─────────────────────────────────────────────────────────────────┘


Core Components
+++++++++++++++

SemanticVector
^^^^^^^^^^^^^^

:iroute:`SemanticVector` encapsulates semantic embeddings used for content-based routing.
It provides TLV encoding/decoding and cosine similarity computation.

.. code-block:: c++

   #include "extensions/iroute-vector.hpp"
   
   // Create a 64-dimensional semantic vector
   std::vector<float> data(64, 0.5f);
   iroute::SemanticVector vec(data);
   
   // Encode to wire format
   ndn::Block wire = vec.wireEncode();
   
   // Decode from wire format
   iroute::SemanticVector decoded;
   decoded.wireDecode(wire, 64);  // Validate dimension
   
   // Compute similarity
   double sim = vec.computeCosineSimilarity(decoded);

TLV Types:

* ``SemanticVector`` (Type 128): Container for the vector
* ``VectorData`` (Type 129): Raw float bytes
* ``VectorDim`` (Type 130): Vector dimension

RouteManager
^^^^^^^^^^^^

:iroute:`RouteManager` serves as the Routing Information Base (RIB) for semantic routing.
Each node has its own RouteManager instance managed by RouteManagerRegistry.

.. code-block:: c++

   #include "extensions/iroute-manager.hpp"
   #include "extensions/iroute-route-manager-registry.hpp"
   
   // Get RouteManager for current node
   uint32_t nodeId = GetNode()->GetId();
   auto rm = iroute::RouteManagerRegistry::getOrCreate(nodeId, 64);
   
   // Add a domain entry
   iroute::DomainEntry entry(Name("/domain1"), semVerId, seqNo);
   entry.centroids.push_back(centroid);
   rm->updateDomainEntry(entry);
   
   // Rank domains for a query
   auto results = rm->rankDomains(queryVector, kMax, tau);

Key methods:

* ``updateRoute()``: Add or update a route with semantic centroid
* ``rankDomains()``: Rank domains using gated scoring formula
* ``findBestMatches()``: Find top-K matching next-hops

SemanticStrategy
^^^^^^^^^^^^^^^^

:iroute:`SemanticStrategy` extends NFD's forwarding strategy with semantic-based routing.
It is designed for ingress-only semantic routing.

.. code-block:: c++

   // Install strategy on ingress node
   ndn::StrategyChoiceHelper::Install(ingressNode, "/iroute/query",
       "/localhost/nfd/strategy/semantic/%FD%01");
   
   // Configure with parameters
   ndn::StrategyChoiceHelper::Install(ingressNode, "/iroute/query",
       "/localhost/nfd/strategy/semantic/%FD%01/alpha~0.7/beta~0.3/topk~3");

Strategy parameters:

* ``alpha``: Weight for semantic similarity (default: 1.0)
* ``beta``: Weight for cost penalty (default: 0.0)
* ``topk``: Maximum faces to forward to (default: 1)
* ``vectordim``: Expected vector dimension (default: 384)
* ``penalty``: Enable face penalty (default: 0)

Applications
++++++++++++

IRouteApp
^^^^^^^^^

:ndnsim:`IRouteApp` implements the control plane of iRoute, handling LSA broadcasting
and discovery request processing.

.. code-block:: c++

   // Create IRouteApp
   ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
   routingHelper.SetAttribute("RouterId", StringValue("router1"));
   routingHelper.SetAttribute("LsaInterval", TimeValue(Seconds(5.0)));
   routingHelper.SetAttribute("VectorDim", UintegerValue(64));
   routingHelper.SetAttribute("IsIngress", BooleanValue(true));
   routingHelper.SetAttribute("SemVerId", UintegerValue(1));
   routingHelper.Install(node);

Attributes:

* ``RouterId``: Unique identifier for this router
* ``LsaInterval``: Interval between LSA broadcasts (default: 5s)
* ``VectorDim``: Dimension of semantic vectors (default: 384)
* ``IsIngress``: Whether this node acts as ingress (default: false)
* ``SemVerId``: Semantic embedding version identifier (default: 1)
* ``HysteresisThreshold``: Threshold for centroid drift to trigger LSA (default: 0.0)

IRouteDiscoveryConsumer
^^^^^^^^^^^^^^^^^^^^^^^

:ndnsim:`IRouteDiscoveryConsumer` implements the two-stage discovery and fetch workflow.

.. code-block:: c++

   // Create discovery consumer
   ndn::AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
   consumerHelper.SetAttribute("Frequency", DoubleValue(1.0));
   consumerHelper.SetAttribute("SemVerId", UintegerValue(1));
   consumerHelper.SetAttribute("Tau", DoubleValue(0.35));
   consumerHelper.SetAttribute("KMax", UintegerValue(5));
   consumerHelper.Install(consumerNode);
   
   // Set query vector
   auto app = consumerNode->GetApplication(0)->GetObject<IRouteDiscoveryConsumer>();
   app->SetQueryVector(queryVector);

Attributes:

* ``Frequency``: Query frequency (default: 1.0 Hz)
* ``SemVerId``: Semantic version ID for queries
* ``Tau``: Confidence threshold for probing (default: 0.35)
* ``KMax``: Maximum probing candidates (default: 5)
* ``FetchTimeout``: Timeout for fetch requests (default: 4s)

Two-Stage Workflow:

1. **Stage-1 Discovery**: Send Discovery Interest to candidate domains
   
   - Interest name: ``/<DomainID>/iroute/disc/<SemVerID>/<nonce>``
   - ApplicationParameters: SemanticVector TLV
   - Response: DiscoveryReply with CanonicalName and Confidence

2. **Stage-2 Fetch**: Fetch content using CanonicalName
   
   - Interest name: ``<CanonicalName>``
   - Response: Actual content Data

SemanticProducer
^^^^^^^^^^^^^^^^

:ndnsim:`SemanticProducer` handles both Discovery Interests and Fetch Interests.

.. code-block:: c++

   ndn::AppHelper producerHelper("ns3::ndn::SemanticProducer");
   producerHelper.SetPrefix("/domain1");
   producerHelper.SetAttribute("PayloadSize", UintegerValue(1024));
   producerHelper.Install(producerNode);
   
   // Register content with semantic vector
   auto producer = producerNode->GetApplication(0)->GetObject<SemanticProducer>();
   producer->RegisterContent(Name("/domain1/data/item1"), contentVector);

Gated Scoring Formula
+++++++++++++++++++++

The iRoute scoring formula combines semantic similarity with network cost:

.. math::

   S_D(q) = \max_i \left[ \alpha \cdot (q \cdot C_i) \cdot \sigma(\lambda(r_i - d)) \cdot \log(1 + w_i) - \beta \cdot \frac{Cost_D}{Cost_{max}} \right]

Where:

* :math:`q`: Query vector
* :math:`C_i`: Centroid vector
* :math:`r_i`: Coverage radius of centroid
* :math:`d`: Distance to centroid (1 - cosine similarity)
* :math:`w_i`: Weight (object count) of centroid
* :math:`\sigma`: Sigmoid gate function
* :math:`\alpha, \beta`: Weighting parameters
* :math:`\lambda`: Sigmoid steepness

Default parameters (from ``iroute::params``):

* ``kAlpha``: 1.0
* ``kBeta``: 0.5
* ``kLambda``: 20.0
* ``kTau``: 0.35 (confidence threshold)
* ``kWMax``: 10000.0 (weight clamp)
* ``kKMax``: 5 (max probing candidates)

TLV Encoding
++++++++++++

Discovery Reply TLV Types (600-605):

.. code-block:: c++

   namespace iroute::tlv {
       constexpr uint32_t DiscoveryReply = 600;
       constexpr uint32_t Status = 601;          // 0=NotFound, 1=Found
       constexpr uint32_t CanonicalName = 602;
       constexpr uint32_t Confidence = 603;
       constexpr uint32_t SemVerIdReply = 604;
       constexpr uint32_t ManifestDigest = 605;
   }

LSA TLV Types (140-149):

.. code-block:: c++

   namespace iroute::lsa_tlv {
       constexpr uint32_t OriginId = 140;
       constexpr uint32_t SemVerId = 141;
       constexpr uint32_t SeqNo = 142;
       constexpr uint32_t Lifetime = 143;
       constexpr uint32_t Scope = 144;
       constexpr uint32_t CentroidList = 145;
       constexpr uint32_t CentroidEntry = 146;
       constexpr uint32_t CentroidId = 147;
       constexpr uint32_t Radius = 148;
       constexpr uint32_t Weight = 149;
   }

Experiments
+++++++++++

The iRoute implementation includes several experiment scripts:

.. list-table:: iRoute Experiments
   :header-rows: 1
   :widths: 30 70

   * - Script
     - Description
   * - ``iroute-v2-demo``
     - Minimal demo of two-stage workflow
   * - ``iroute-v2-exp1-accuracy``
     - Accuracy measurement with synthetic corpus
   * - ``iroute-v2-exp2-m-sweep``
     - M (centroids per domain) parameter sweep
   * - ``iroute-v2-exp3-state-scaling``
     - State scaling with number of domains
   * - ``iroute-v2-exp4-latency``
     - End-to-end latency measurement
   * - ``iroute-v2-exp5-ingress-throughput``
     - Ingress compute throughput benchmark
   * - ``iroute-v2-exp6-failure``
     - Domain failure recovery
   * - ``iroute-v2-exp7-semver-rollout``
     - SemVerID rollout testing
   * - ``iroute-v2-exp8-ewma``
     - EWMA penalty effectiveness

Example usage:

.. code-block:: bash

   # Run accuracy experiment
   ./waf --run "iroute-v2-exp1-accuracy --domains=100 --queries=1000"
   
   # Run latency experiment
   ./waf --run "iroute-v2-exp4-latency --domains=50 --queries=200"
   
   # Run demo with verbose logging
   NS_LOG="ndn.IRouteApp:ndn.IRouteDiscoveryConsumer=info" \
       ./waf --run iroute-v2-demo

Example Simulation
++++++++++++++++++

A complete example using iRoute:

.. code-block:: c++

   #include "ns3/core-module.h"
   #include "ns3/network-module.h"
   #include "ns3/point-to-point-module.h"
   #include "ns3/ndnSIM-module.h"
   
   #include "apps/iroute-app.hpp"
   #include "apps/iroute-discovery-consumer.hpp"
   #include "apps/semantic-producer.hpp"
   #include "extensions/iroute-vector.hpp"
   #include "extensions/iroute-manager.hpp"
   
   int main(int argc, char* argv[])
   {
       // Create nodes
       NodeContainer nodes;
       nodes.Create(4);  // Consumer, Ingress, Core, Producer
       
       // Connect with point-to-point links
       PointToPointHelper p2p;
       p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
       p2p.SetChannelAttribute("Delay", StringValue("10ms"));
       p2p.Install(nodes.Get(0), nodes.Get(1));
       p2p.Install(nodes.Get(1), nodes.Get(2));
       p2p.Install(nodes.Get(2), nodes.Get(3));
       
       // Install NDN stack
       ndn::StackHelper ndnHelper;
       ndnHelper.SetDefaultRoutes(true);
       ndnHelper.InstallAll();
       
       // Install SemanticStrategy on ingress
       ndn::StrategyChoiceHelper::Install(nodes.Get(1), "/iroute/query",
           "/localhost/nfd/strategy/semantic/%FD%01");
       
       // Install IRouteApp on all nodes
       ndn::AppHelper routingHelper("ns3::ndn::IRouteApp");
       routingHelper.SetAttribute("VectorDim", UintegerValue(64));
       
       routingHelper.SetAttribute("RouterId", StringValue("ingress"));
       routingHelper.SetAttribute("IsIngress", BooleanValue(true));
       routingHelper.Install(nodes.Get(1));
       
       routingHelper.SetAttribute("RouterId", StringValue("core"));
       routingHelper.SetAttribute("IsIngress", BooleanValue(false));
       routingHelper.Install(nodes.Get(2));
       
       routingHelper.SetAttribute("RouterId", StringValue("producer"));
       routingHelper.Install(nodes.Get(3));
       
       // Install consumer
       ndn::AppHelper consumerHelper("ns3::ndn::IRouteDiscoveryConsumer");
       consumerHelper.SetAttribute("Frequency", DoubleValue(0.5));
       consumerHelper.Install(nodes.Get(0));
       
       // Install producer
       ndn::AppHelper producerHelper("ns3::ndn::SemanticProducer");
       producerHelper.SetPrefix("/producer");
       producerHelper.Install(nodes.Get(3));
       
       Simulator::Stop(Seconds(20.0));
       Simulator::Run();
       Simulator::Destroy();
       
       return 0;
   }

References
++++++++++

* iRoute Paper: "Semantic Routing for Named Data Networking"
* ndnSIM Documentation: https://ndnsim.net
* ndn-cxx Library: https://github.com/named-data/ndn-cxx
