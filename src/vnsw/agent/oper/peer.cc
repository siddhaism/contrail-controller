/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <controller/controller_route_walker.h>
#include <controller/controller_peer.h>
#include <oper/peer.h>
#include <oper/vrf.h>

Peer::Peer(Type type, const std::string &name) : 
    type_(type), name_(name){ 
}

Peer::~Peer() {
}

BgpPeer::BgpPeer(const Ip4Address &server_ip, const std::string &name,
                 AgentXmppChannel *bgp_xmpp_peer, DBTableBase::ListenerId id)
    : Peer(Peer::BGP_PEER, name), server_ip_(server_ip), id_(id), 
    bgp_xmpp_peer_(bgp_xmpp_peer), 
    route_walker_(new ControllerRouteWalker(bgp_xmpp_peer_->agent(), this)) {
        is_disconnect_walk_ = false;
}

BgpPeer::~BgpPeer() {
    // TODO verify if this unregister can be done in walkdone callback 
    // for delpeer
    if ((id_ != -1) && route_walker_->agent()->GetVrfTable()) {
        route_walker_->agent()->GetVrfTable()->Unregister(id_);
    }
}

void BgpPeer::DelPeerRoutes(DelPeerDone walk_done_cb) {
    route_walker_->Start(ControllerRouteWalker::DELPEER, false, walk_done_cb);
}

void BgpPeer::PeerNotifyRoutes() {
    route_walker_->Start(ControllerRouteWalker::NOTIFYALL, true, NULL);
}

void BgpPeer::PeerNotifyMulticastRoutes(bool associate) {
    route_walker_->Start(ControllerRouteWalker::NOTIFYMULTICAST, associate, 
                         NULL);
}

void BgpPeer::StalePeerRoutes() {
    route_walker_->Start(ControllerRouteWalker::STALE, true, NULL);
}
