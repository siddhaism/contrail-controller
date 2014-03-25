/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_CONTROLLER_INIT_HPP__
#define __VNSW_CONTROLLER_INIT_HPP__

#include <sandesh/sandesh_trace.h>
#include <discovery_client.h>
#include <boost/scoped_ptr.hpp>

class AgentXmppChannel;
class AgentDnsXmppChannel;
class AgentIfMapVmExport;

class VNController {
public:
    static const uint64_t kInvalidPeerIdentifier = 0xFFFFFFFFFFFFFFFF;
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

    Agent *agent() {return agent_;}
    uint64_t incr_multicast_peer_identifier() {
        return multicast_peer_identifier_++;}
    uint64_t multicast_peer_identifier() {return multicast_peer_identifier_;}
    uint32_t ControllerPeerListSize() const {return controller_peer_list_.size();}
    void AddToControllerPeerList(Peer *peer);
    AgentIfMapVmExport *agent_ifmap_vm_export() const {return agent_ifmap_vm_export_.get();}

private:
    AgentXmppChannel *FindAgentXmppChannel(std::string server_ip);
    AgentDnsXmppChannel *FindAgentDnsXmppChannel(std::string server_ip);

    Agent *agent_;
    uint64_t multicast_peer_identifier_;
    std::list<Peer *> controller_peer_list_;
    boost::scoped_ptr<AgentIfMapVmExport> agent_ifmap_vm_export_;
};

extern SandeshTraceBufferPtr ControllerTraceBuf;

#define CONTROLLER_TRACE(obj, ...)\
do {\
    AgentXmpp##obj::TraceMsg(ControllerTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while(0);\

#endif
