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

    void Start(Type type, bool associate, AgentRouteWalker::WalkDone cb);
    void Cancel();
    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    bool Notify(DBTablePartBase *partition, DBEntryBase *e);
    bool NotifyAll(DBTablePartBase *partition, DBEntryBase *e);
    bool DelPeer(DBTablePartBase *partition, DBEntryBase *e);
    bool StaleMarker(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteNotifyAll(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteDelPeer(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteStaleMarker(DBTablePartBase *partition, DBEntryBase *e);

    Peer *peer_;
    bool associate_;
    Type type_;
    DISALLOW_COPY_AND_ASSIGN(ControllerRouteWalker);
};

#endif
