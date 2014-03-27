#include <test/test_basic_scale.h>

TEST_F(AgentBasicScaleTest, Basic) {
    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200"}
	};
    AddIPAM("vn1", ipam_info, 1);
    WAIT_FOR(1000, 10000, RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));

    //expect subscribe message+route at the mock server
    Ip4Address mc_addr = Ip4Address::from_string("255.255.255.255");
    WAIT_FOR(1000, 10000, MCRouteFind("vrf1", mc_addr));
    //WAIT_FOR(100, 10000, (mock_peer[0].get()->Count() == 
    //                      ((6 * num_vns * num_vms_per_vn) + num_vns)));

    VerifyVmPortActive(true);
    VerifyRoutes(false);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    
    //Delete vm-port and route entry in vrf1
    DelIPAM("vn1");
    WAIT_FOR(1000, 10000, !RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));
    DeleteVmPortEnvironment();
}

TEST_F(AgentBasicScaleTest, one_channel_down_up) {
    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200"}
	};
    AddIPAM("vn1", ipam_info, 1);
    WAIT_FOR(1000, 10000, RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));

    //expect subscribe message+route at the mock server
    Ip4Address mc_addr = Ip4Address::from_string("255.255.255.255");
    WAIT_FOR(1000, 10000, MCRouteFind("vrf1", mc_addr));
    //WAIT_FOR(100, 10000, (mock_peer[0].get()->Count() == 
    //                      ((6 * num_vns * num_vms_per_vn) + num_vns)));

    VerifyVmPortActive(true);
    VerifyRoutes(false);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    
    //Store the src mpls label to verify it does not change after channel down
    MulticastGroupObject *mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);

    uint32_t old_multicast_identifier = 
        Agent::GetInstance()->controller()->multicast_peer_identifier();
    WAIT_FOR(1000, 10000, (mcobj->GetSourceMPLSLabel() != 0));
    uint32_t subnet_src_label = mcobj->GetSourceMPLSLabel();
    //EXPECT_TRUE(Agent::GetInstance()->GetMplsTable()->FindMplsLabel(subnet_src_label));

    //Bring down the channel
    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer[0].get());
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() == subnet_src_label);
    EXPECT_TRUE(mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_peer_identifier());
    //EXPECT_TRUE(Agent::GetInstance()->GetMplsTable()->FindMplsLabel(subnet_src_label));

    mc_addr = Ip4Address::from_string("255.255.255.255");
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    uint32_t source_flood_label = mcobj->GetSourceMPLSLabel();
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    EXPECT_TRUE(mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_peer_identifier());

    //Bring up the channel
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    VerifyConnections(0, 14);

    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    WAIT_FOR(1000, 1000, (mcobj->GetSourceMPLSLabel() != subnet_src_label));
    EXPECT_TRUE(mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_peer_identifier());
    mc_addr = Ip4Address::from_string("255.255.255.255");
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    WAIT_FOR(1000, 1000, (mcobj->GetSourceMPLSLabel() != source_flood_label));
    EXPECT_TRUE(mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_peer_identifier());
    EXPECT_TRUE(old_multicast_identifier != 
                Agent::GetInstance()->controller()->multicast_peer_identifier());

    //Delete vm-port and route entry in vrf1
    DelIPAM("vn1");
    mc_addr = Ip4Address::from_string("1.1.1.255");
    WAIT_FOR(1000, 10000, !RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));
    DeleteVmPortEnvironment();
}

TEST_F(AgentBasicScaleTest, one_channel_down_up_skip_route_from_peer) {
    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200"}
	};
    AddIPAM("vn1", ipam_info, 1);
    WAIT_FOR(1000, 10000, RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));

    //expect subscribe message+route at the mock server
    Ip4Address mc_addr = Ip4Address::from_string("255.255.255.255");
    WAIT_FOR(1000, 10000, MCRouteFind("vrf1", mc_addr));
    //WAIT_FOR(100, 10000, (mock_peer[0].get()->Count() == 
    //                      ((6 * num_vns * num_vms_per_vn) + num_vns)));

    VerifyVmPortActive(true);
    VerifyRoutes(false);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    
    //Store the src mpls label to verify it does not change after channel down
    MulticastGroupObject *mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);

    uint32_t old_multicast_identifier = 
        Agent::GetInstance()->controller()->multicast_peer_identifier();
    WAIT_FOR(1000, 1000, (mcobj->GetSourceMPLSLabel() != 0));
    uint32_t subnet_src_label = mcobj->GetSourceMPLSLabel();
    //EXPECT_TRUE(Agent::GetInstance()->GetMplsTable()->FindMplsLabel(subnet_src_label));

    //Bring down the channel
    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer[0].get());
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() == subnet_src_label);
    EXPECT_TRUE(mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_peer_identifier());
    //EXPECT_TRUE(Agent::GetInstance()->GetMplsTable()->FindMplsLabel(subnet_src_label));

    uint32_t source_flood_label = mcobj->GetSourceMPLSLabel();
    mc_addr = Ip4Address::from_string("255.255.255.255");
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    EXPECT_TRUE(mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_peer_identifier());

    //Bring up the channel
    mock_peer[0].get()->SkipRoute("1.1.1.255");
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::READY);
    VerifyConnections(0, 14);

    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() == subnet_src_label);
    EXPECT_TRUE(mcobj->peer_identifier() == old_multicast_identifier); 
    mc_addr = Ip4Address::from_string("255.255.255.255");
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);
    WAIT_FOR(1000, 1000, (mcobj->peer_identifier() == 
                Agent::GetInstance()->controller()->multicast_peer_identifier()));
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != source_flood_label);
    EXPECT_TRUE(old_multicast_identifier != 
                Agent::GetInstance()->controller()->multicast_peer_identifier());
    EXPECT_TRUE(MulticastHandler::GetInstance()->stale_timer()->running());

    //Fire the timer
    MulticastHandler::GetInstance()->stale_timer()->Fire();
    mc_addr = Ip4Address::from_string("1.1.1.255");
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    WAIT_FOR(1000, 1000, (mcobj->GetSourceMPLSLabel() == 0));

    //Delete vm-port and route entry in vrf1
    DelIPAM("vn1");
    WAIT_FOR(1000, 10000, !RouteFind("vrf1", Ip4Address::from_string("1.1.1.255"), 32));
    DeleteVmPortEnvironment();
}

int main(int argc, char **argv) {
    GETSCALEARGS();
    if ((num_vns * num_vms_per_vn) > MAX_INTERFACES) {
        LOG(DEBUG, "Max interfaces is 200");
        return false;
    }
    if (num_ctrl_peers == 0 || num_ctrl_peers > MAX_CONTROL_PEER) {
        LOG(DEBUG, "Supported values - 1, 2");
        return false;
    }

    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->set_headless_agent_mode(true);
    InitXmppServers();

    int ret = RUN_ALL_TESTS();
    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();
    return ret;
}
