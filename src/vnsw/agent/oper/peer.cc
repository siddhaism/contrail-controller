/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <controller/controller_route_walker.h>
#include <oper/peer.h>
#include <oper/vrf.h>

Peer::Peer(Agent *agent, Type type, const std::string &name) : 
    type_(type), name_(name), agent_(agent), 
    route_walker_(new ControllerRouteWalker(agent, this)) {
        is_disconnect_walk_ = false;
}

Peer::~Peer() {
}

void Peer::DelPeerRoutes(DelPeerDone cb) {
    route_walker_->Start(ControllerRouteWalker::DELPEER, false, cb);
}

void Peer::PeerNotifyRoutes() {
    route_walker_->Start(ControllerRouteWalker::NOTIFYALL, true, NULL);
}

void Peer::PeerNotifyMulticastRoutes(bool associate) {
    route_walker_->Start(ControllerRouteWalker::NOTIFYMULTICAST, associate, 
                         NULL);
}

void Peer::StalePeerRoutes() {
    route_walker_->Start(ControllerRouteWalker::STALE, true, NULL);
}

BgpPeer::~BgpPeer() {
    if (id_ != -1) {
        agent()->GetVrfTable()->Unregister(id_);
    }
}
