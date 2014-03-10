/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/shared_ptr.hpp>

#include "oper/controller_route_walker.h"
#include "oper/route_common.h"
#include "oper/vrf.h"
#include "oper/mirror_table.h"
#include "oper/agent_sandesh.h"

//Routines for peer notification
PeerNotifyRouteWalker::PeerNotifyRouteWalker(Agent *agent, const Peer *peer, bool associate) : 
    AgentRouteWalker(agent, AgentRouteWalker::ALL), peer_(peer), associate_(associate) {
}

bool PeerNotifyRouteWalker::VrfWalkNotify(DBTablePartBase *partition,
                                          DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer_->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), id)); 
        if (state) {
            /* state for __default__ instance will not be created if the 
             * xmpp channel is up the first time as export code registers to 
             * vrf-table after entry for __default__ instance is created */
            state->force_chg_ = true;
        }

        //Pass this object pointer so that VrfExport::Notify can start the route
        //walk if required on this VRF.
        VrfExport::Notify(bgp_peer->GetBgpXmppPeer(), part, entry, this);
        return true;
    }
    return false;
}

bool PeerNotifyRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);
    BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);
    Agent::RouteTableType table_type = route->GetTableType();
    VrfEntry *vrf = route->vrf();

    DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
    DBTablePartBase *vrf_partition = agent()->GetVrfTable()->GetTablePartition(vrf);
    VrfExport::State *vs = 
        static_cast<VrfExport::State *>(vrf->GetState(vrf_partition->parent(), id)); 

    RouteExport::State *state =
        static_cast<RouteExport::State *>(route->GetState(partition->parent(),
                      vs->rt_export_[table_type]->GetListenerId()));
    if (state) {
        state->force_chg_ = true;
    }

    vs->rt_export_[table_type]->
        Notify(bgp_peer->GetBgpXmppPeer(), associate_, table_type, partition, e);
    return true;
}

void PeerNotifyRouteWalker::Start() {
    StartVrfWalk(); 
}

void PeerNotifyRouteWalker::Cancel() {
    CancelVrfWalk(); 
}

//Delete peer walker
DelPeerRouteWalker::DelPeerRouteWalker(Agent *agent, const Peer *peer) : 
    AgentRouteWalker(agent, AgentRouteWalker::ALL), peer_(peer) {
}

bool DelPeerRouteWalker::VrfWalkNotify(DBTablePartBase *partition,
                                          DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (entry->IsDeleted()) {
        return true;
    }

    if (peer_->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), id)); 
        if (state == NULL) {
            return true;
        }

        StartRouteWalk(vrf);
        return true;
    }
    return false;
}

bool DelPeerRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    if (route) {
        DeletePathFromPeer(partition, route, peer_);
    }
    return true;
}

void DelPeerRouteWalker::Start() {
    StartVrfWalk(); 
}

void DelPeerRouteWalker::Cancel() {
    CancelVrfWalk(); 
}

//Routines for multicast notification
MulticastRouteWalker::MulticastRouteWalker(Agent *agent, const Peer *peer, bool associate) : 
    PeerNotifyRouteWalker(agent, AgentRouteWalker::ALL, associate) {
}

bool MulticastRouteWalker::VrfWalkNotify(DBTablePartBase *partition,
                                         DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer_->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), id)); 
        if (state && (vrf->GetName().compare(agent()->GetDefaultVrf()) != 0)) {
            StartRouteWalk(vrf);
        }

        return true;
    }
    return false;
}

//Routines for stale peer handling
StaleRouteWalker::StaleRouteWalker(Agent *agent, const Peer *peer) : 
    AgentRouteWalker(agent, AgentRouteWalker::ALL) {
}

bool StaleRouteWalker::VrfWalkNotify(DBTablePartBase *partition,
                                     DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer_->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), id)); 
        if (state) {
            StartRouteWalk(vrf);
        }
        return true;
    }
    return false;
}

bool StaleRouteWalker::RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    if (route) {
        route->StalePathFromPeer(partition, peer_);
    }
    return true;
}
