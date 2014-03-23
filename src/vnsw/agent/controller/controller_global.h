/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __CONTROLLER_GLOBAL_DATA_H__
#define __CONTROLLER_GLOBAL_DATA_H__

#include <list>

class AgentControllerGlobalData {
public:
    static const uint64_t kInvalidPeerIdentifier = 0xFFFFFFFFFFFFFFFF;
    AgentControllerGlobalData(Agent *agent, bool headless) : 
        multicast_peer_identifier_(0), headless_agent_(headless) { 
            controller_peer_list_.clear();
    };
    virtual ~AgentControllerGlobalData() { };

    Agent *agent() const {return agent_;}
    uint64_t incr_multicast_peer_identifier() {return multicast_peer_identifier_++;}
    uint64_t multicast_peer_identifier() {return multicast_peer_identifier_;}
    void AddToControllerPeerList(Peer *peer) {
        controller_peer_list_.push_back(peer);
    }
    bool headless_agent() const {return headless_agent_;}

private:
    Agent *agent_;
    uint64_t multicast_peer_identifier_;
    bool headless_agent_;
    std::list<Peer *> controller_peer_list_;
};

#endif // __CONTROLLER_GLOBAL_DATA_H__
