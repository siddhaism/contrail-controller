/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_controller_route_walker_hpp
#define vnsw_controller_route_walker_hpp

#include "cmn/agent_cmn.h"
#include "oper/agent_route_walker.h"

class ControllerRouteWalker : public AgentRouteWalker {
public:    
    enum Type {
        NOTIFYALL,
        NOTIFYMULTICAST,
        DELPEER,
        STALE,
    };
    ControllerRouteWalker(Agent *agent, Peer *peer);
    virtual ~ControllerRouteWalker() { }

    void Start(Type type, bool associate, 
               AgentRouteWalker::WalkDone walk_done_cb);
    void Cancel();
    void RouteWalkDoneForVrf(DBTableBase *partition, VrfEntry *vrf);
    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    bool VrfNotifyInternal(DBTablePartBase *partition, DBEntryBase *e);
    bool VrfNotifyMulticast(DBTablePartBase *partition, DBEntryBase *e);
    bool VrfNotifyStale(DBTablePartBase *partition, DBEntryBase *e);
    bool VrfNotifyAll(DBTablePartBase *partition, DBEntryBase *e);
    bool VrfDelPeer(DBTablePartBase *partition, DBEntryBase *e);
    bool VrfStaleMarker(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteNotifyInternal(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteNotifyAll(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteNotifyMulticast(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteDelPeer(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteStaleMarker(DBTablePartBase *partition, DBEntryBase *e);

    DBState *GetVrfExportState(DBTablePartBase *partition, DBEntryBase *e);
    DBState *GetRouteExportState(DBTablePartBase *partition, DBEntryBase *e);

    Peer *peer_;
    bool associate_;
    Type type_;
    DISALLOW_COPY_AND_ASSIGN(ControllerRouteWalker);
};

#endif
