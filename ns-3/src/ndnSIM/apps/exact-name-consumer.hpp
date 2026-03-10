#ifndef EXACT_NAME_CONSUMER_HPP
#define EXACT_NAME_CONSUMER_HPP

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/random-variable-stream.h"
#include <vector>

namespace ns3 {
namespace ndn {

struct BaselineTxRecord {
    uint64_t queryId;
    std::string requestedName;
    double startTime;
    double endTime;
    double totalMs;
    bool success;
    bool fromCache;
};

/**
 * @brief A consumer that requests a specific sequence of full names.
 * Used for "PrefixFlood" (Oracle) baseline where consumer "knows" 
 * the exact data name without discovery.
 */
class ExactNameConsumer : public App {
public:
    static TypeId GetTypeId();

    ExactNameConsumer();
    virtual ~ExactNameConsumer();

    // Set the list of names to fetch sequentially
    void SetQueryList(const std::vector<std::string>& names);
    
    // Get collected statistics
    const std::vector<BaselineTxRecord>& GetTransactions() const;

    // Export metrics to CSV
    void ExportToCsv(const std::string& filename) const;

protected:
    // From App
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    virtual void OnData(std::shared_ptr<const Data> contentObject) override;
    virtual void OnNack(std::shared_ptr<const lp::Nack> nack) override;
    virtual void OnTimeout(Name name);

private:
    void ScheduleNextPacket();
    void SendPacket();

    std::vector<std::string> m_queryList;
    size_t m_queryIndex;
    
    // Packet parameters
    Time m_interestLifetime;
    Time m_interQueryGap;
    
    // Transaction tracking
    std::vector<BaselineTxRecord> m_transactions;
    BaselineTxRecord m_currentTx;
    bool m_isRunning;
    bool m_hasPending;
    
    EventId m_sendEvent;
    EventId m_timeoutEvent;
    
    Ptr<UniformRandomVariable> m_seqRng; // For nonces
};

} // namespace ndn
} // namespace ns3

#endif // EXACT_NAME_CONSUMER_HPP
