/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_CONTROLLER_INIT_HPP__
#define __VNSW_CONTROLLER_INIT_HPP__

#include <sandesh/sandesh_trace.h>
#include <discovery_client.h>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

class AgentXmppChannel;
class AgentDnsXmppChannel;
class AgentIfMapVmExport;
class BgpPeer;

class VNController {
public:
    static const uint64_t kInvalidPeerIdentifier = 0xFFFFFFFFFFFFFFFF;
    static const uint32_t kUnicastStaleTimer = (2 * 60 * 1000); 
    static const uint32_t kMulticastStaleTimer = (5 * 60 * 1000); 
    VNController(Agent *agent);
    ~VNController();
    void Connect();
    void DisConnect();

    void Cleanup();

    void XmppServerConnect();
    void DnsXmppServerConnect();

    void XmppServerDisConnect();
    void DnsXmppServerDisConnect();

    void ApplyDiscoveryXmppServices(std::vector<DSResponse> resp); 
    void ApplyDiscoveryDnsXmppServices(std::vector<DSResponse> resp); 

    //Multicast peer identifier
    void increment_multicast_peer_identifier() {multicast_peer_identifier_++;}
    uint64_t multicast_peer_identifier() {return multicast_peer_identifier_;}

    //Peer maintenace 
    uint8_t GetActiveXmppConnections();
    uint32_t ControllerPeerListSize() const {return controller_peer_list_.size();}
    void AddToControllerPeerList(boost::shared_ptr<BgpPeer> peer);
    void ControllerPeerHeadlessAgentDelDone(BgpPeer *peer);

    //timer common
    void CancelTimer(Timer *timer);

    //Unicast timer
    void StartUnicastCleanupTimer();
    bool UnicastCleanupTimerExpired();
    Timer *unicast_cleanup_timer() const {return unicast_cleanup_timer_;}

    //Multicast timer
    void StartMulticastCleanupTimer(uint64_t peer_sequence);
    bool MulticastCleanupTimerExpired(uint64_t peer_sequence);
    Timer *multicast_cleanup_timer() const {return multicast_cleanup_timer_;}

    AgentIfMapVmExport *agent_ifmap_vm_export() const {return agent_ifmap_vm_export_.get();}
    Agent *agent() {return agent_;}

    void DeleteVrfStateOfDecommisionedPeers(DBTablePartBase *partition, DBEntryBase *e);
    void DeleteRouteStateOfDecommisionedPeers(DBTablePartBase *partition, DBEntryBase *e);

private:
    AgentXmppChannel *FindAgentXmppChannel(std::string server_ip);
    AgentDnsXmppChannel *FindAgentDnsXmppChannel(std::string server_ip);

    Agent *agent_;
    uint64_t multicast_peer_identifier_;
    std::list<boost::shared_ptr<BgpPeer> > controller_peer_list_;
    boost::scoped_ptr<AgentIfMapVmExport> agent_ifmap_vm_export_;
    Timer *unicast_cleanup_timer_;
    Timer *multicast_cleanup_timer_;
};

extern SandeshTraceBufferPtr ControllerTraceBuf;

#define CONTROLLER_TRACE(obj, ...)\
do {\
    AgentXmpp##obj::TraceMsg(ControllerTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(0);\

#endif
