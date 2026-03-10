#include "exact-name-consumer.hpp"
#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/callback.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include <fstream>

NS_LOG_COMPONENT_DEFINE("ExactNameConsumer");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(ExactNameConsumer);

TypeId
ExactNameConsumer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ndn::ExactNameConsumer")
        .SetParent<App>()
        .AddConstructor<ExactNameConsumer>()
        .AddAttribute("InterestLifetime", "Interest lifetime",
                      TimeValue(Seconds(1.0)),
                      MakeTimeAccessor(&ExactNameConsumer::m_interestLifetime),
                      MakeTimeChecker())
        .AddAttribute("InterQueryGap", "Time to wait between queries",
                      TimeValue(MilliSeconds(10)),
                      MakeTimeAccessor(&ExactNameConsumer::m_interQueryGap),
                      MakeTimeChecker());
    return tid;
}

ExactNameConsumer::ExactNameConsumer()
    : m_queryIndex(0)
    , m_isRunning(false)
    , m_hasPending(false)
{
    m_seqRng = CreateObject<UniformRandomVariable>();
}

ExactNameConsumer::~ExactNameConsumer()
{
}

void
ExactNameConsumer::SetQueryList(const std::vector<std::string>& names)
{
    m_queryList = names;
    m_queryIndex = 0;
}

const std::vector<BaselineTxRecord>&
ExactNameConsumer::GetTransactions() const
{
    return m_transactions;
}

void
ExactNameConsumer::StartApplication()
{
    App::StartApplication();
    m_isRunning = true;
    m_queryIndex = 0;
    
    // Schedule first packet
    ScheduleNextPacket();
}

void
ExactNameConsumer::StopApplication()
{
    m_isRunning = false;
    if (m_sendEvent.IsRunning()) {
        m_sendEvent.Cancel();
    }
    if (m_timeoutEvent.IsRunning()) {
        m_timeoutEvent.Cancel();
    }
    App::StopApplication();
}

void
ExactNameConsumer::ScheduleNextPacket()
{
    if (!m_isRunning) return;
    
    if (m_queryIndex >= m_queryList.size()) {
        NS_LOG_INFO("All queries processed.");
        return;
    }
    
    if (!m_sendEvent.IsRunning()) {
        m_sendEvent = Simulator::Schedule(m_interQueryGap, 
                                          &ExactNameConsumer::SendPacket, this);
    }
}

void
ExactNameConsumer::SendPacket()
{
    if (!m_isRunning || m_queryIndex >= m_queryList.size()) return;
    
    std::string nameStr = m_queryList[m_queryIndex];
    Name name(nameStr);
    
    // Prepare Interest
    auto interest = std::make_shared<Interest>(name);
    interest->setNonce(m_seqRng->GetValue(0, std::numeric_limits<uint32_t>::max()));
    interest->setInterestLifetime(ndn::time::milliseconds(m_interestLifetime.GetMilliSeconds()));
    interest->setMustBeFresh(false); 
    
    // Record Start
    m_currentTx.queryId = m_queryIndex;
    m_currentTx.requestedName = nameStr;
    m_currentTx.startTime = Simulator::Now().GetSeconds();
    m_currentTx.success = false;
    m_currentTx.fromCache = false; // Simplified
    m_hasPending = true;
    
    NS_LOG_INFO("Sending Interest for " << name);
    
    // Send
    m_transmittedInterests(interest, this, m_face);
    m_appLink->onReceiveInterest(*interest);
    
    // Schedule timeout
    m_timeoutEvent = Simulator::Schedule(m_interestLifetime, 
                                         &ExactNameConsumer::OnTimeout, this, name);
}

void
ExactNameConsumer::OnData(std::shared_ptr<const Data> data)
{
    if (!m_hasPending) return;
    
    Name dataName = data->getName();
    // Simplified matching: we assume we just sent one interest and blocked
    // In a real pipeline we'd check names, but here we enforce sequentiality
    
    if (m_timeoutEvent.IsRunning()) {
        m_timeoutEvent.Cancel();
    }
    
    m_currentTx.endTime = Simulator::Now().GetSeconds();
    m_currentTx.totalMs = (m_currentTx.endTime - m_currentTx.startTime) * 1000.0;
    m_currentTx.success = true;
    
    m_transactions.push_back(m_currentTx);
    m_hasPending = false;
    m_queryIndex++;
    
    NS_LOG_INFO("Received Data for " << dataName << " RTT=" << m_currentTx.totalMs << "ms");
    
    ScheduleNextPacket();
}

void
ExactNameConsumer::OnNack(std::shared_ptr<const lp::Nack> nack)
{
    if (!m_hasPending) return;
    
    if (m_timeoutEvent.IsRunning()) {
        m_timeoutEvent.Cancel();
    }
    
    m_currentTx.endTime = Simulator::Now().GetSeconds();
    m_currentTx.totalMs = (m_currentTx.endTime - m_currentTx.startTime) * 1000.0;
    m_currentTx.success = false;
    
    m_transactions.push_back(m_currentTx);
    m_hasPending = false;
    m_queryIndex++;
    
    NS_LOG_INFO("Received NACK");
    
    ScheduleNextPacket();
}

void
ExactNameConsumer::OnTimeout(Name name)
{
    if (!m_hasPending) return;
    
    m_currentTx.endTime = Simulator::Now().GetSeconds();
    m_currentTx.totalMs = (m_currentTx.endTime - m_currentTx.startTime) * 1000.0;
    m_currentTx.success = false;
    
    m_transactions.push_back(m_currentTx);
    m_hasPending = false;
    m_queryIndex++;
    
    NS_LOG_INFO("Timeout for " << name);
    
    ScheduleNextPacket();
}

void 
ExactNameConsumer::ExportToCsv(const std::string& filename) const
{
    std::ofstream out(filename);
    out << "queryId,name,success,totalMs" << std::endl;
    for (const auto& tx : m_transactions) {
        out << tx.queryId << "," << tx.requestedName << "," 
            << tx.success << "," << tx.totalMs << std::endl;
    }
    out.close();
}

} // namespace ndn
} // namespace ns3
