/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/timer.h"
#include "base/contrail_ports.h"
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_trace.h>
#include "cmn/agent_cmn.h"
#include "xmpp/xmpp_init.h"
#include "pugixml/pugixml.hpp"
#include "oper/vrf.h"
#include "oper/peer.h"
#include "controller/controller_types.h"
#include "controller/controller_init.h"
#include "controller/controller_peer.h"
#include "controller/controller_ifmap.h"
#include "controller/controller_dns.h"
#include "bind/bind_resolver.h"

using namespace boost::asio;

SandeshTraceBufferPtr ControllerTraceBuf(SandeshTraceBufferCreate(
    "Controller", 1000));

VNController::VNController(Agent *agent) : agent_(agent), multicast_peer_identifier_(0) {
    controller_peer_list_.clear();
    cleanup_timer_ = TimerManager::CreateTimer(*(agent->GetEventManager()->
                                                io_service()),
                                               "Agent Stale cleanup timer",
                                               TaskScheduler::GetInstance()->
                                               GetTaskId("db::DBTable"), 0);
}

VNController::~VNController() {
}

void VNController::XmppServerConnect() {

    uint8_t count = 0;

    while (count < MAX_XMPP_SERVERS) {
        if (!agent_->GetXmppServer(count).empty()) {

            AgentXmppChannel *ch = agent_->GetAgentXmppChannel(count);
            if (ch) {
                // Channel is created, do not disturb
                CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server",
                                 ch->GetXmppServer(), 
                                 "is already present, ignore discovery response");
                count++;
                continue; 
            }

            XmppInit *xmpp = new XmppInit();
            XmppClient *client = new XmppClient(agent_->GetEventManager());
            XmppChannelConfig *xmpp_cfg = new XmppChannelConfig(true);
            xmpp_cfg->ToAddr = XmppInit::kControlNodeJID;
            boost::system::error_code ec;
            xmpp_cfg->FromAddr = agent_->GetHostName();
            xmpp_cfg->NodeAddr = XmppInit::kPubSubNS; 
            xmpp_cfg->endpoint.address(
                ip::address::from_string(agent_->GetXmppServer(count), ec));
            assert(ec.value() == 0);
            uint32_t port = agent_->GetXmppPort(count);
            if (!port) {
                port = XMPP_SERVER_PORT;
            }
            xmpp_cfg->endpoint.port(port);
            xmpp->AddXmppChannelConfig(xmpp_cfg);
            xmpp->InitClient(client);

            XmppChannel *channel = client->FindChannel(XmppInit::kControlNodeJID);
            assert(channel);

            agent_->SetAgentMcastLabelRange(count);
            // create bgp peer
            AgentXmppChannel *bgp_peer = new AgentXmppChannel(agent_, channel, 
                                             agent_->GetXmppServer(count), 
                                             agent_->GetAgentMcastLabelRange(count),
                                             count);
            if (agent_->headless_agent_mode()) {
                client->RegisterConnectionEvent(xmps::BGP,
                        boost::bind(&AgentXmppChannel::HandleHeadlessAgentXmppClientChannelEvent,
                                    bgp_peer, _2));
            } else {
                client->RegisterConnectionEvent(xmps::BGP,
                        boost::bind(&AgentXmppChannel::HandleXmppClientChannelEvent, 
                                    bgp_peer, _2));
            }

            // create ifmap peer
            AgentIfMapXmppChannel *ifmap_peer = 
                new AgentIfMapXmppChannel(agent_, channel, count);

            agent_->SetAgentXmppChannel(bgp_peer, count);
            agent_->SetAgentIfMapXmppChannel(ifmap_peer, count);
            agent_->SetAgentXmppClient(client, count);
            agent_->SetAgentXmppInit(xmpp, count);
        }
        count++;
    }
}

void VNController::DnsXmppServerConnect() {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if (!agent_->GetDnsXmppServer(count).empty()) {

            AgentDnsXmppChannel *ch = agent_->GetAgentDnsXmppChannel(count);
            if (ch) {
                // Channel is up and running, do not disturb
                CONTROLLER_TRACE(DiscoveryConnection, "DNS Server",
                                 ch->GetXmppServer(), 
                                 "is already present, ignore discovery response");
                count++;
                continue; 
            }

            // create Xmpp channel with DNS server
            XmppInit *xmpp_dns = new XmppInit();
            XmppClient *client_dns = new XmppClient(agent_->GetEventManager());
            XmppChannelConfig xmpp_cfg_dns(true);
            xmpp_cfg_dns.ToAddr = XmppInit::kDnsNodeJID;
            boost::system::error_code ec;
            xmpp_cfg_dns.FromAddr = agent_->GetHostName() + "/dns";
            xmpp_cfg_dns.NodeAddr = "";
            xmpp_cfg_dns.endpoint.address(
                     ip::address::from_string(agent_->GetDnsXmppServer(count), ec));
            assert(ec.value() == 0);
            xmpp_cfg_dns.endpoint.port(ContrailPorts::DnsXmpp);
            xmpp_dns->AddXmppChannelConfig(&xmpp_cfg_dns);
            xmpp_dns->InitClient(client_dns);

            XmppChannel *channel_dns = client_dns->FindChannel(
                                                   XmppInit::kDnsNodeJID);
            assert(channel_dns);

            // create dns peer
            AgentDnsXmppChannel *dns_peer = new AgentDnsXmppChannel(agent_, channel_dns, 
                                                agent_->GetDnsXmppServer(count), count);
            client_dns->RegisterConnectionEvent(xmps::DNS,
                boost::bind(&AgentDnsXmppChannel::HandleXmppClientChannelEvent, 
                            dns_peer, _2));
            agent_->SetAgentDnsXmppClient(client_dns, count);
            agent_->SetAgentDnsXmppChannel(dns_peer, count);
            agent_->SetAgentDnsXmppInit(xmpp_dns, count);
            BindResolver::Resolver()->SetupResolver(
                agent_->GetDnsXmppServer(count), count);
        }
        count++;
    }
}

void VNController::Connect() {
    /* Connect to Control-Node Xmpp Server */
    XmppServerConnect();

    /* Connect to DNS Xmpp Server */
    DnsXmppServerConnect();

    /* Inits */
    agent_->SetControlNodeMulticastBuilder(NULL);
    agent_ifmap_vm_export_.reset(new AgentIfMapVmExport(agent_));
}

void VNController::XmppServerDisConnect() {
    XmppClient *cl;
    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if ((cl = agent_->GetAgentXmppClient(count)) != NULL) {
            Peer *peer = agent_->GetAgentXmppChannel(count)->bgp_peer_id();
            // Sets the context of walk to decide on callback when walks are
            // done
            if (peer)
                peer->set_is_disconnect_walk(true);
            //shutdown triggers cleanup of routes learnt from
            //the control-node. 
            cl->Shutdown();
        }
        count ++;
    }
}


void VNController::DnsXmppServerDisConnect() {
    XmppClient *cl;
    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        if ((cl = agent_->GetAgentDnsXmppClient(count)) != NULL) {
            cl->Shutdown();
        }
        count ++;
    }


}

//Trigger shutdown and cleanup of routes for the client
void VNController::DisConnect() {
    XmppServerDisConnect();
    DnsXmppServerDisConnect();
}

void VNController::Cleanup() {
    uint8_t count = 0;
    XmppClient *cl;
    while (count < MAX_XMPP_SERVERS) {
        if ((cl = agent_->GetAgentXmppClient(count)) != NULL) {

            agent_->ResetAgentMcastLabelRange(count);

            delete agent_->GetAgentXmppChannel(count);
            agent_->SetAgentXmppChannel(NULL, count);

            delete agent_->GetAgentIfMapXmppChannel(count);
            agent_->SetAgentIfMapXmppChannel(NULL, count);

            agent_->SetAgentXmppClient(NULL, count);
          
            XmppInit *xmpp = agent_->GetAgentXmppInit(count);
            xmpp->Reset();
            delete xmpp;
            agent_->SetAgentXmppInit(NULL, count);
        }
        if ((cl = agent_->GetAgentDnsXmppClient(count)) != NULL) {

            delete agent_->GetAgentDnsXmppChannel(count);
            agent_->SetAgentDnsXmppChannel(NULL, count);

            agent_->SetAgentDnsXmppClient(NULL, count);

            XmppInit *xmpp = agent_->GetAgentDnsXmppInit(count);
            xmpp->Reset();
            delete xmpp;
            agent_->SetAgentDnsXmppInit(NULL, count);

            agent_->SetDnsXmppPort(0, count); 
            agent_->SetXmppDnsCfgServer(-1);
        }
        count++;
    }

    agent_->SetControlNodeMulticastBuilder(NULL);
    agent_ifmap_vm_export_.reset();
}


AgentXmppChannel *VNController::FindAgentXmppChannel(std::string server_ip) {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        AgentXmppChannel *ch = agent_->GetAgentXmppChannel(count);
        if (ch && (ch->GetXmppServer().compare(server_ip) == 0)) {
            return ch; 
        }
        count++;
    }

    return NULL;
}


void VNController::ApplyDiscoveryXmppServices(std::vector<DSResponse> resp) {

    std::vector<DSResponse>::iterator iter;
    for (iter = resp.begin(); iter != resp.end(); iter++) {
        DSResponse dr = *iter;
        
        CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server", dr.ep.address().to_string(),
                         "Discovery Server Response"); 
        AgentXmppChannel *chnl = FindAgentXmppChannel(dr.ep.address().to_string());
        if (chnl) { 
            if (chnl->GetXmppChannel() &&
                chnl->GetXmppChannel()->GetPeerState() == xmps::READY) {
                CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server",
                                 chnl->GetXmppServer(), "is UP and running, ignore");
                continue;
            } else { 
                CONTROLLER_TRACE(DiscoveryConnection, "XMPP Server",
                                 chnl->GetXmppServer(), "is NOT_READY, ignore");
                continue;
            } 

        } else { 
            if (agent_->GetXmppServer(0).empty()) {
                agent_->SetXmppServer(dr.ep.address().to_string(), 0);
                agent_->SetXmppPort(dr.ep.port(), 0);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (agent_->GetXmppServer(1).empty()) {
                agent_->SetXmppServer(dr.ep.address().to_string(), 1);
                agent_->SetXmppPort(dr.ep.port(), 1);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (agent_->GetAgentXmppChannel(0)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                agent_->ResetAgentMcastLabelRange(0);
                delete agent_->GetAgentXmppChannel(0);
                agent_->SetAgentXmppChannel(NULL, 0);
                delete agent_->GetAgentIfMapXmppChannel(0);
                agent_->SetAgentIfMapXmppChannel(NULL, 0);
                agent_->SetAgentXmppClient(NULL, 0);
                delete agent_->GetAgentXmppInit(0);
                agent_->SetAgentXmppInit(NULL, 0);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                "Refresh Xmpp Channel[0] = ", dr.ep.address().to_string(), ""); 
                agent_->SetXmppServer(dr.ep.address().to_string(),0);
                agent_->SetXmppPort(dr.ep.port(), 0);

            } else if (agent_->GetAgentXmppChannel(1)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                agent_->ResetAgentMcastLabelRange(1);
                delete agent_->GetAgentXmppChannel(1);
                agent_->SetAgentXmppChannel(NULL, 1);
                delete agent_->GetAgentIfMapXmppChannel(1);
                agent_->SetAgentIfMapXmppChannel(NULL, 1);
                agent_->SetAgentXmppClient(NULL, 1);
                delete agent_->GetAgentXmppInit(1);
                agent_->SetAgentXmppInit(NULL, 1);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                 "Refresh Xmpp Channel[1] = ", dr.ep.address().to_string(), ""); 
                agent_->SetXmppServer(dr.ep.address().to_string(), 1);
                agent_->SetXmppPort(dr.ep.port(), 1);
           }
        }
    }

    XmppServerConnect();
}

AgentDnsXmppChannel *VNController::FindAgentDnsXmppChannel(std::string server_ip) {

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
        AgentDnsXmppChannel *ch = agent_->GetAgentDnsXmppChannel(count);
        if (ch && (ch->GetXmppServer().compare(server_ip) == 0)) {
            return ch; 
        }
        count++;
    }

    return NULL;
}


void VNController::ApplyDiscoveryDnsXmppServices(std::vector<DSResponse> resp) {

    std::vector<DSResponse>::iterator iter;
    for (iter = resp.begin(); iter != resp.end(); iter++) {
        DSResponse dr = *iter;
        
        CONTROLLER_TRACE(DiscoveryConnection, "DNS Server", dr.ep.address().to_string(),
                         "Discovery Server Response"); 
        AgentDnsXmppChannel *chnl = FindAgentDnsXmppChannel(dr.ep.address().to_string());
        if (chnl) { 
            if (chnl->GetXmppChannel() &&
                chnl->GetXmppChannel()->GetPeerState() == xmps::READY) {
                CONTROLLER_TRACE(DiscoveryConnection, "DNS Server",
                                 chnl->GetXmppServer(), "is UP and running, ignore");
                continue;
            } else { 
                CONTROLLER_TRACE(DiscoveryConnection, "DNS Server",
                                 chnl->GetXmppServer(), "is NOT_READY, ignore");
                continue;
            } 

        } else { 
            if (agent_->GetDnsXmppServer(0).empty()) {
                agent_->SetDnsXmppServer(dr.ep.address().to_string(), 0);
                agent_->SetDnsXmppPort(dr.ep.port(), 0);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Dns Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (agent_->GetDnsXmppServer(1).empty()) {
                agent_->SetDnsXmppServer(dr.ep.address().to_string(), 1);
                agent_->SetDnsXmppPort(dr.ep.port(), 1);
                CONTROLLER_TRACE(DiscoveryConnection, "Set Dns Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), ""); 
            } else if (agent_->GetAgentDnsXmppChannel(0)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                delete agent_->GetAgentDnsXmppChannel(0);
                delete agent_->GetAgentDnsXmppInit(0);
                agent_->SetAgentDnsXmppChannel(NULL, 0);
                agent_->SetAgentDnsXmppClient(NULL, 0);
                agent_->SetAgentDnsXmppInit(NULL, 0);

                CONTROLLER_TRACE(DiscoveryConnection,   
                                "Refresh Dns Xmpp Channel[0] = ", 
                                 dr.ep.address().to_string(), ""); 
                agent_->SetDnsXmppServer(dr.ep.address().to_string(), 0);
                agent_->SetDnsXmppPort(dr.ep.port(), 0);

            } else if (agent_->GetAgentDnsXmppChannel(1)->GetXmppChannel()->GetPeerState() 
                       == xmps::NOT_READY) {

                //cleanup older xmpp channel
                delete agent_->GetAgentDnsXmppChannel(1);
                delete agent_->GetAgentDnsXmppInit(1);
                agent_->SetAgentDnsXmppChannel(NULL, 1);
                agent_->SetAgentDnsXmppClient(NULL, 1);
                agent_->SetAgentDnsXmppInit(NULL, 1);

                CONTROLLER_TRACE(DiscoveryConnection, 
                                 "Refresh Dns Xmpp Channel[1] = ", 
                                 dr.ep.address().to_string(), ""); 
                agent_->SetDnsXmppServer(dr.ep.address().to_string(), 1);
                agent_->SetDnsXmppPort(dr.ep.port(), 1);
           }
        }
    } 

    DnsXmppServerConnect();
}

uint8_t VNController::GetActiveXmppConnections() {
    uint8_t active_xmpps = 0;
    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {
       AgentXmppChannel *xc = agent_->GetAgentXmppChannel(count);
       if (xc) {
           if (xc->GetState() == AgentXmppChannel::UP)
               active_xmpps++;
       }
    }

    return active_xmpps;
}

void VNController::AddToControllerPeerList(boost::shared_ptr<Peer> peer) {
    controller_peer_list_.push_back(peer);
}

void VNController::ControllerPeerHeadlessAgentDelDone(Peer *bgp_peer) {
    for (std::list<boost::shared_ptr<Peer> >::iterator it  = 
         controller_peer_list_.begin(); it != controller_peer_list_.end(); 
         ++it) {
        Peer *peer = static_cast<Peer *>((*it).get());
        if (peer == bgp_peer) {
            controller_peer_list_.remove(*it);
            return;
        }
    }
}

bool VNController::ControllerPeerCleanupTimerExpired() {
    for (std::list<boost::shared_ptr<Peer> >::iterator it  = 
         controller_peer_list_.begin(); it != controller_peer_list_.end(); 
         ++it) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>((*it).get());
        bgp_peer->DelPeerRoutes(
            boost::bind(&VNController::ControllerPeerHeadlessAgentDelDone, this, bgp_peer));
    }

    return true;
}

void VNController::ControllerPeerStartCleanupTimer() {
    if (cleanup_timer_ == NULL) {
        return;
    }

    if (cleanup_timer_->running()) {
        cleanup_timer_->Cancel();
    }
    cleanup_timer_->Start(kUnicastStaleTimer,
        boost::bind(&VNController::ControllerPeerCleanupTimerExpired, this));
}
