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

TEST_F(InetMVpnPrefixTest, BuildNativePrefix) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip4Address group(Ip4Address::from_string("224.1.2.3", ec));
    Ip4Address source(Ip4Address::from_string("192.168.1.1", ec));
    InetMVpnPrefix prefix(InetMVpnPrefix::NativeRoute, rd, group, source);
    EXPECT_EQ("0-10.1.1.1:65535-0.0.0.0,224.1.2.3,192.168.1.1", prefix.ToString());
    EXPECT_EQ("10.1.1.1:65535:224.1.2.3,192.168.1.1", prefix.ToXmppIdString());
    EXPECT_EQ(InetMVpnPrefix::NativeRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("0.0.0.0", prefix.router_id().to_string());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("192.168.1.1", prefix.source().to_string());
}

TEST_F(InetMVpnPrefixTest, ParseNativePrefix) {
    string prefix_str("0-10.1.1.1:65535-0.0.0.0,224.1.2.3,192.168.1.1");
    InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));
    EXPECT_EQ("0-10.1.1.1:65535-0.0.0.0,224.1.2.3,192.168.1.1", prefix.ToString());
    EXPECT_EQ("10.1.1.1:65535:224.1.2.3,192.168.1.1", prefix.ToXmppIdString());
    EXPECT_EQ(InetMVpnPrefix::NativeRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("0.0.0.0", prefix.router_id().to_string());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("192.168.1.1", prefix.source().to_string());
}

TEST_F(InetMVpnPrefixTest, BuildCMcastPrefix) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip4Address router_id(Ip4Address::from_string("9.8.7.6", ec));
    Ip4Address group(Ip4Address::from_string("224.1.2.3", ec));
    Ip4Address source(Ip4Address::from_string("192.168.1.1", ec));
    InetMVpnPrefix prefix(InetMVpnPrefix::CMcastRoute, rd, router_id, group, source);
    EXPECT_EQ("1-10.1.1.1:65535-9.8.7.6,224.1.2.3,192.168.1.1", prefix.ToString());
    EXPECT_EQ(InetMVpnPrefix::CMcastRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("9.8.7.6", prefix.router_id().to_string());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("192.168.1.1", prefix.source().to_string());
}

TEST_F(InetMVpnPrefixTest, ParseCMcastPrefix) {
    string prefix_str("1-10.1.1.1:65535-9.8.7.6,224.1.2.3,192.168.1.1");
    InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));
    EXPECT_EQ(InetMVpnPrefix::CMcastRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("9.8.7.6", prefix.router_id().to_string());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("192.168.1.1", prefix.source().to_string());
}

TEST_F(InetMVpnPrefixTest, BuildTreePrefix) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip4Address router_id(Ip4Address::from_string("9.8.7.6", ec));
    Ip4Address group(Ip4Address::from_string("224.1.2.3", ec));
    Ip4Address source(Ip4Address::from_string("192.168.1.1", ec));
    InetMVpnPrefix prefix(InetMVpnPrefix::TreeRoute, rd, router_id, group, source);
    EXPECT_EQ("2-10.1.1.1:65535-9.8.7.6,224.1.2.3,192.168.1.1", prefix.ToString());
    EXPECT_EQ(InetMVpnPrefix::TreeRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("9.8.7.6", prefix.router_id().to_string());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("192.168.1.1", prefix.source().to_string());
}

TEST_F(InetMVpnPrefixTest, ParseTreePrefix) {
    string prefix_str("2-10.1.1.1:65535-9.8.7.6,224.1.2.3,192.168.1.1");
    InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));
    EXPECT_EQ(InetMVpnPrefix::TreeRoute, prefix.type());
    EXPECT_EQ("10.1.1.1:65535", prefix.route_distinguisher().ToString());
    EXPECT_EQ("9.8.7.6", prefix.router_id().to_string());
    EXPECT_EQ("224.1.2.3", prefix.group().to_string());
    EXPECT_EQ("192.168.1.1", prefix.source().to_string());
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
