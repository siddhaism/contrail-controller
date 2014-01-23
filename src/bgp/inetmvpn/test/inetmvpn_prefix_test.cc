/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inetmvpn/inetmvpn_route.h"

#include "base/logging.h"
#include "base/task.h"
#include "bgp/bgp_log.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using namespace std;

class InetMVpnPrefixTest : public ::testing::Test {
};

TEST_F(InetMVpnPrefixTest, Build1) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:258"));
    Ip4Address group(Ip4Address::from_string("224.1.2.3", ec));
    Ip4Address source(Ip4Address::from_string("1.2.3.4", ec));
    InetMVpnPrefix prefix(rd, 0x11121314, group, source);
    EXPECT_EQ("10.1.1.1:258-17.18.19.20,224.1.2.3,1.2.3.4", prefix.ToString());
    EXPECT_EQ("10.1.1.1:258", prefix.route_distinguisher().ToString());
    EXPECT_EQ("17.18.19.20", prefix.router_id().to_string());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("1.2.3.4", prefix.source().to_string());
}

TEST_F(InetMVpnPrefixTest, Build2) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.2.3.4:258"));
    Ip4Address group(Ip4Address::from_string("1.2.3.224", ec));
    Ip4Address source(Ip4Address::from_string("1.2.3.4", ec));
    InetMVpnPrefix prefix(rd, 0x11121314, group, source);

    EXPECT_EQ("10.2.3.4:258-17.18.19.20,1.2.3.224,1.2.3.4", prefix.ToString());
    EXPECT_EQ("10.2.3.4:258", prefix.route_distinguisher().ToString());
    EXPECT_EQ("17.18.19.20", prefix.router_id().to_string());
    EXPECT_EQ("1.2.3.224", prefix.group().to_string());
    EXPECT_EQ("1.2.3.4", prefix.source().to_string());
}

TEST_F(InetMVpnPrefixTest, Parse1) {
    boost::system::error_code ec;
    string prefix_str("10.2.3.4:258-17.18.19.20,1.2.3.224,1.2.3.4");
    InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str, ec));

    EXPECT_EQ("10.2.3.4:258-17.18.19.20,1.2.3.224,1.2.3.4", prefix.ToString());
    EXPECT_EQ("10.2.3.4:258", prefix.route_distinguisher().ToString());
    EXPECT_EQ("17.18.19.20", prefix.router_id().to_string());
    EXPECT_EQ("1.2.3.224", prefix.group().to_string());
    EXPECT_EQ("1.2.3.4", prefix.source().to_string());
}

TEST_F(InetMVpnPrefixTest, Parse2) {
    boost::system::error_code ec;
    string prefix_str("10.2.3.4:258-17.18.19.20,224.1.2.3,1.2.3.4");
    InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str, ec));

    EXPECT_EQ("10.2.3.4:258-17.18.19.20,224.1.2.3,1.2.3.4", prefix.ToString());
    EXPECT_EQ("10.2.3.4:258", prefix.route_distinguisher().ToString());
    EXPECT_EQ("17.18.19.20", prefix.router_id().to_string());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("1.2.3.4", prefix.source().to_string());
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
