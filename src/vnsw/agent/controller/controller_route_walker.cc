/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/shared_ptr.hpp>

#include "controller/controller_route_walker.h"
#include "controller/controller_vrf_export.h"
#include "controller/controller_export.h"
#include "oper/route_common.h"
#include "oper/vrf.h"
#include "oper/mirror_table.h"
#include "oper/agent_sandesh.h"

ControllerRouteWalker::ControllerRouteWalker(Agent *agent, Peer *peer) : 
    AgentRouteWalker(agent, AgentRouteWalker::ALL), peer_(peer), 
    associate_(false), type_(NOTIFYALL) {
}

bool ControllerRouteWalker::VrfWalkNotify(DBTablePartBase *partition,
                                          DBEntryBase *entry) {
    switch (type_) {
    case NOTIFYALL:
        return NotifyAll(partition, entry);
    case NOTIFYMULTICAST:
        return Notify(partition, entry);
    case DELPEER:
        return DelPeer(partition, entry);
    case STALE:
        if (associate_) {
            return Notify(partition, entry);
        } else {
            //On route notify check nature of walk and take action
            return DelPeer(partition, entry);
        }
        break;
    default:
        return false;
    }
    return false;
}

bool ControllerRouteWalker::NotifyAll(DBTablePartBase *partition, 
                                      DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer_->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), 
                                                          id)); 
        if (state) {
            /* state for __default__ instance will not be created if the 
             * xmpp channel is up the first time as export code registers to 
             * vrf-table after entry for __default__ instance is created */
            state->force_chg_ = true;
        }

        //Pass this object pointer so that VrfExport::Notify can start the route
        //walk if required on this VRF.
        VrfExport::Notify(bgp_peer->GetBgpXmppPeer(), partition, entry);
        return true;
    }
    return false;
}

bool ControllerRouteWalker::DelPeer(DBTablePartBase *partition,
                                    DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (entry->IsDeleted()) {
        return true;
    }

    if (peer_->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), 
                                                          id)); 
        if (state == NULL) {
            return true;
        }

        StartRouteWalk(vrf);
        return true;
    }
    return false;
}

//Common routeine if basic vrf and peer check is required for the walk
bool ControllerRouteWalker::Notify(DBTablePartBase *partition, 
                                   DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer_->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), 
                                                          id)); 
        if (state && (vrf->GetName().compare(agent()->GetDefaultVrf()) != 0)) {
            StartRouteWalk(vrf);
        }

        return true;
    }
    return false;
}

bool ControllerRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                      DBEntryBase *entry) {
    switch (type_) {
    case NOTIFYALL:
    case NOTIFYMULTICAST:
        return RouteNotifyAll(partition, entry);
    case DELPEER:
        return RouteDelPeer(partition, entry);
    case STALE:
        if (associate_) {
            return RouteStaleMarker(partition, entry);
        } else {
            //On route notify check nature of walk and take action
            return RouteDelPeer(partition, entry);
        }
        break;
    default:
        return false;
    }
    return false;
}

bool ControllerRouteWalker::RouteNotifyAll(DBTablePartBase *partition, 
                                           DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);

    if ((type_ == NOTIFYMULTICAST) && !route->is_multicast())
        return true;

    BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);
    Agent::RouteTableType table_type = route->GetTableType();
    VrfEntry *vrf = route->vrf();

    DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
    DBTablePartBase *vrf_partition = 
        agent()->GetVrfTable()->GetTablePartition(vrf);
    VrfExport::State *vs = 
        static_cast<VrfExport::State *>(vrf->GetState(vrf_partition->parent(), 
                                                      id)); 

    RouteExport::State *state =
        static_cast<RouteExport::State *>(route->GetState(partition->parent(),
                      vs->rt_export_[table_type]->GetListenerId()));
    if (state) {
        state->force_chg_ = true;
    }

    vs->rt_export_[table_type]->
      Notify(bgp_peer->GetBgpXmppPeer(), associate_, table_type, partition, 
             entry);
    return true;
}

bool ControllerRouteWalker::RouteDelPeer(DBTablePartBase *partition,
                                         DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    if (route) {
        route->vrf()->GetRouteTable(route->GetTableType())->
            DeletePathFromPeer(partition, route, peer_);
    }
    return true;
}

bool ControllerRouteWalker::RouteStaleMarker(DBTablePartBase *partition, 
                                             DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    if (route) {
        route->StalePathFromPeer(partition, peer_);
    }
    return true;
}

void ControllerRouteWalker::Start(Type type, bool associate, 
                                  AgentRouteWalker::WalkDone cb) {
    associate_ = associate;
    type_ = type;
    WalkDoneCallback(cb);
    StartVrfWalk(); 
}

void ControllerRouteWalker::Cancel() {
    CancelVrfWalk(); 
}
