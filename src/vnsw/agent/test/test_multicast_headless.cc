#include <test/test_basic_scale.h>

TEST_F(AgentBasicScaleTest, Basic) {
    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
    client->WaitForIdle();
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200"}
	};
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    //expect subscribe message+route at the mock server
    Ip4Address mc_addr = Ip4Address::from_string("255.255.255.255");
    WAIT_FOR(1000, 10000, MCRouteFind("vrf1", mc_addr));
    WAIT_FOR(100, 10000, (mock_peer[0].get()->Count() == 
                          ((6 * num_vns * num_vms_per_vn) + num_vns)));
    client->WaitForIdle();

    VerifyVmPortActive(true);
    VerifyUnicastRoutes(false);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    
    //Delete vm-port and route entry in vrf1
    DelIPAM("vn1");
    DeleteVmportEnv(input, (num_vns * num_vms_per_vn), true);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (Agent::GetInstance()->GetVnTable()->Size() == 0));
    WAIT_FOR(1000, 10000, !MCRouteFind("vrf1", mc_addr));
    VerifyVmPortActive(false);
    VerifyUnicastRoutes(true);
    mc_addr = Ip4Address::from_string("255.255.255.255");
    EXPECT_FALSE(MCRouteFind("vrf1", mc_addr));
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_FALSE(RouteFind("vrf1", mc_addr, 32));
    client->WaitForIdle();
}

TEST_F(AgentBasicScaleTest, one_channel_down) {
    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();
    client->WaitForIdle();
	IpamInfo ipam_info[] = {
	    {"1.1.1.0", 24, "1.1.1.200"}
	};
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    //expect subscribe message+route at the mock server
    Ip4Address mc_addr = Ip4Address::from_string("255.255.255.255");
    WAIT_FOR(1000, 10000, MCRouteFind("vrf1", mc_addr));
    WAIT_FOR(100, 10000, (mock_peer[0].get()->Count() == 
                          ((6 * num_vns * num_vms_per_vn) + num_vns)));
    client->WaitForIdle();

    VerifyVmPortActive(true);
    VerifyUnicastRoutes(false);
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    
    //Store the src mpls label to verify it does not change after channel down
    MulticastGroupObject *mcobj = MulticastHandler::GetInstance()->
        FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    uint32_t subnet_src_label = mcobj->GetSourceMPLSLabel();
    EXPECT_TRUE(subnet_src_label != 0);
    //EXPECT_TRUE(Agent::GetInstance()->GetMplsTable()->FindMplsLabel(subnet_src_label));

    //Bring down the channel
    AgentXmppChannel *ch = static_cast<AgentXmppChannel *>(bgp_peer[0].get());
    bgp_peer[0].get()->HandleXmppChannelEvent(xmps::NOT_READY);
    client->WaitForIdle();
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_TRUE(RouteFind("vrf1", mc_addr, 32));
    mcobj = MulticastHandler::GetInstance()->FindGroupObject("vrf1", mc_addr);
    EXPECT_TRUE(mcobj != NULL);
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() == subnet_src_label);
    //EXPECT_TRUE(Agent::GetInstance()->GetMplsTable()->FindMplsLabel(subnet_src_label));

    mc_addr = Ip4Address::from_string("255.255.255.255");
    EXPECT_TRUE(MCRouteFind("vrf1", mc_addr));
    EXPECT_TRUE(mcobj->GetSourceMPLSLabel() != 0);

    //Delete vm-port and route entry in vrf1
    DelIPAM("vn1");
    DeleteVmportEnv(input, (num_vns * num_vms_per_vn), true);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (Agent::GetInstance()->GetVnTable()->Size() == 0));
    WAIT_FOR(1000, 10000, !MCRouteFind("vrf1", mc_addr));
    VerifyVmPortActive(false);
    VerifyUnicastRoutes(true);
    mc_addr = Ip4Address::from_string("255.255.255.255");
    EXPECT_FALSE(MCRouteFind("vrf1", mc_addr));
    mc_addr = Ip4Address::from_string("1.1.1.255");
    EXPECT_FALSE(RouteFind("vrf1", mc_addr, 32));
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETSCALEARGS();
    if (num_vns > MAX_VN) {
        LOG(DEBUG, "Max VN is 254");
        return false;
    }
    if (num_vms_per_vn > MAX_VMS_PER_VN) {
        LOG(DEBUG, "Max VM per VN is 100");
        return false;
    }
    if (num_ctrl_peers == 0 || num_ctrl_peers > MAX_CONTROL_PEER) {
        LOG(DEBUG, "Supported values - 1, 2");
        return false;
    }
    if (num_vms_per_vn > MAX_REMOTE_ROUTES_PER_VN) {
        LOG(DEBUG, "Max remote route per vn is 100");
        return false;
    }

    client = TestInit(init_file, ksync_init);
    InitXmppServers();

    int ret = RUN_ALL_TESTS();
    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();
    return ret;
}
