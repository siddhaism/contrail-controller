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
    ControllerRouteWalker(Agent *agent, const Peer *peer);
    virtual ~ControllerRouteWalker() { }

    void Start(Type type, bool associate);
    void Cancel();
    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void NotifyAll(DBTablePartBase *partition, DBEntryBase *e);
    void DelPeer(DBTablePartBase *partition, DBEntryBase *e);
    void StaleMarker(DBTablePartBase *partition, DBEntryBase *e);
    void RouteNotifyAll(DBTablePartBase *partition, DBEntryBase *e);
    void RouteDelPeer(DBTablePartBase *partition, DBEntryBase *e);
    void RouteStaleMarker(DBTablePartBase *partition, DBEntryBase *e);

    const Peer *peer_;
    bool associate_;
    Type type_;
    DISALLOW_COPY_AND_ASSIGN(ControllerRouteWalker);
};

#endif
