#ifndef NLSR_OVERHEAD_APP_HPP
#define NLSR_OVERHEAD_APP_HPP

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/random-variable-stream.h"

namespace ns3 {
namespace ndn {

/**
 * @brief NlsrOverheadApp simulates the control traffic of NLSR
 * by periodically broadcasting dummy LSA Interests with appropriate payload size.
 */
class NlsrOverheadApp : public App {
public:
    static TypeId GetTypeId();

    NlsrOverheadApp();
    virtual ~NlsrOverheadApp();

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;

private:
    void SendLsaInterest();

    Time m_lsaInterval;
    uint32_t m_nodeId;
    uint32_t m_prefixes; // Number of prefixes to simulate size
    uint32_t m_fixedHeader;
    uint32_t m_seqNo;
    
    EventId m_sendEvent;
    Ptr<UniformRandomVariable> m_seqRng;
};

} // namespace ndn
} // namespace ns3

#endif // NLSR_OVERHEAD_APP_HPP
