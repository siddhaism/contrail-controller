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
        return VrfNotifyAll(partition, entry);
    case NOTIFYMULTICAST:
        return VrfNotifyMulticast(partition, entry);
    case DELPEER:
        return VrfDelPeer(partition, entry);
    case STALE:
        return VrfNotifyStale(partition, entry);
    default:
        return false;
    }
    return false;
}

bool ControllerRouteWalker::VrfNotifyAll(DBTablePartBase *partition, 
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

bool ControllerRouteWalker::VrfDelPeer(DBTablePartBase *partition,
                                       DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer_->GetType() == Peer::BGP_PEER) {
        // Register Callback for deletion of VRF state on completion of route
        // walks 
        RouteWalkDoneForVrfCallback(boost::bind(
                                    &ControllerRouteWalker::RouteWalkDoneForVrf,
                                    this, _1, _2));
        StartRouteWalk(vrf);
        return true;
    }
    return false;
}

bool ControllerRouteWalker::VrfNotifyMulticast(DBTablePartBase *partition, 
                                               DBEntryBase *entry) {
    return VrfNotifyInternal(partition, entry);
}

bool ControllerRouteWalker::VrfNotifyStale(DBTablePartBase *partition, 
                                           DBEntryBase *entry) {
    return VrfNotifyInternal(partition, entry);
}

//Common routeine if basic vrf and peer check is required for the walk
bool ControllerRouteWalker::VrfNotifyInternal(DBTablePartBase *partition, 
                                              DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer_->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), 
                                                          id)); 
        //TODO check if state is not added for default vrf
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
        return RouteNotifyAll(partition, entry);
    case NOTIFYMULTICAST:
        return RouteNotifyMulticast(partition, entry);
    case DELPEER:
        return RouteDelPeer(partition, entry);
    case STALE:
        return RouteStaleMarker(partition, entry);
    default:
        return false;
    }
    return false;
}

DBState *ControllerRouteWalker::GetVrfExportState(DBTablePartBase *partition, 
                                                  DBEntryBase *entry) {
    BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);
    DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    return(static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), 
                                                         id)));
}

DBState *ControllerRouteWalker::GetRouteExportState(DBTablePartBase *partition, 
                                                    DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    VrfEntry *vrf = route->vrf();

    DBTablePartBase *vrf_partition = 
        agent()->GetVrfTable()->GetTablePartition(vrf);

    VrfExport::State *vs = static_cast<VrfExport::State *>
        (GetVrfExportState(vrf_partition, vrf)); 

    if (vs == NULL)
        return NULL;

    Agent::RouteTableType table_type = route->GetTableType();
    RouteExport::State *state = NULL;
    if (vs->rt_export_[table_type]) {
        state = static_cast<RouteExport::State *>(route->GetState(partition->parent(),
                            vs->rt_export_[table_type]->GetListenerId()));
    }
    return state;
}

bool ControllerRouteWalker::RouteNotifyInternal(DBTablePartBase *partition, 
                                                DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);

    if ((type_ == NOTIFYMULTICAST) && !route->is_multicast())
        return true;

    BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);
    Agent::RouteTableType table_type = route->GetTableType();

    RouteExport::State *route_state = static_cast<RouteExport::State *>
        (GetRouteExportState(partition, entry));
    if (route_state) {
        route_state->force_chg_ = true;
    }

    VrfEntry *vrf = route->vrf();
    DBTablePartBase *vrf_partition = agent()->GetVrfTable()->
        GetTablePartition(vrf);
    VrfExport::State *vs = static_cast<VrfExport::State *>
        (GetVrfExportState(vrf_partition, vrf));

    vs->rt_export_[table_type]->
      Notify(bgp_peer->GetBgpXmppPeer(), associate_, table_type, partition, 
             entry);
    return true;
}

bool ControllerRouteWalker::RouteNotifyAll(DBTablePartBase *partition, 
                                           DBEntryBase *entry) {
    return RouteNotifyInternal(partition, entry);
}

bool ControllerRouteWalker::RouteNotifyMulticast(DBTablePartBase *partition, 
                                                 DBEntryBase *entry) {
    return RouteNotifyInternal(partition, entry);
}

bool ControllerRouteWalker::RouteDelPeer(DBTablePartBase *partition,
                                         DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);

    if (!route)
        return true;

    VrfEntry *vrf = route->vrf();
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>
        (GetVrfExportState(partition, entry));
    RouteExport::State *state = static_cast<RouteExport::State *>
        (GetRouteExportState(partition, entry));
    if (vrf_state && state) {
        route->ClearState(partition->parent(), vrf_state->rt_export_[route->
                          GetTableType()]->GetListenerId());
        delete state;
    }

    vrf->GetRouteTable(route->GetTableType())->DeletePathFromPeer(partition,
                                                                  route, peer_);
    return true;
}

bool ControllerRouteWalker::RouteStaleMarker(DBTablePartBase *partition, 
                                             DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    if (route) {
        route->vrf()->GetRouteTable(route->GetTableType())->
            StalePathFromPeer(partition, route, peer_);
    }
    return true;
}

void ControllerRouteWalker::RouteWalkDoneForVrf(DBTableBase *partition,
                                                VrfEntry *vrf) {
    // Currently used only for delete peer handling
    // Deletes the state and release the listener id
    if (type_ != DELPEER)
        return;

    BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

    DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
    VrfExport::State *state = 
        static_cast<VrfExport::State *>(vrf->GetState(partition, id)); 
    if (state != NULL) {
        if (vrf->GetName().compare(bgp_peer->GetBgpXmppPeer()->agent()->
                                   GetDefaultVrf()) != 0) {
            for (uint8_t table_type = 0; table_type < Agent::ROUTE_TABLE_MAX;
                 table_type++) {
                if (state->rt_export_[table_type]) {
                    state->rt_export_[table_type]->Unregister();
                }
            }
        }

        vrf->ClearState(partition, id);
        delete state;
    }
}

void ControllerRouteWalker::Start(Type type, bool associate, 
                            AgentRouteWalker::WalkDone walk_done_cb) {
    associate_ = associate;
    type_ = type;
    WalkDoneCallback(walk_done_cb);

    StartVrfWalk(); 
}

void ControllerRouteWalker::Cancel() {
    CancelVrfWalk(); 
}
