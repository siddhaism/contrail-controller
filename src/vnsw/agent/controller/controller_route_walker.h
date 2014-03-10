/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_controller_route_walker_hpp
#define vnsw_controller_route_walker_hpp

#include "cmn/agent_cmn.h"
#include "oper/agent_route_walker.h"

class PeerNotifyRouteWalker : public AgentRouteWalker {
public:    
    PeerNotifyRouteWalker(Agent *agent, const Peer *peer, bool associate);
    virtual ~PeerNotifyRouteWalker() { }

    void Start();
    void Cancel();
    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    Peer *peer_;
    bool associate_;
    DISALLOW_COPY_AND_ASSIGN(PeerNotifyRouteWalker);
};

class DelPeerRouteWalker : public AgentRouteWalker {
public:    
    DelPeerRouteWalker(Agent *agent, const Peer *peer);
    virtual ~DelPeerRouteWalker() { }

    void Start();
    void Cancel();
    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    Peer *peer_;
    DISALLOW_COPY_AND_ASSIGN(DelPeerRouteWalker);
};

class MulticastRouteWalker : public PeerNotifyRouteWalker {
public:    
    MulticastRouteWalker(Agent *agent, const Peer *peer, bool associate);
    virtual ~MulticastRouteWalker() { }

    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    DISALLOW_COPY_AND_ASSIGN(MulticastRouteWalker);
}

class StaleRouteWalker : public AgentRouteWalker {
public:    
    StaleRouteWalker(Agent *agent, const Peer *peer);
    virtual ~StaleRouteWalker() { }

    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    DISALLOW_COPY_AND_ASSIGN(StaleRouteWalker);
}
#endif
