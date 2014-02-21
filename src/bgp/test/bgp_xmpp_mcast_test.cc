/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inetmvpn/inetmvpn_route.h"
#include "bgp/inetmvpn/inetmvpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "sandesh/sandesh.h"
#include "schema/xmpp_multicast_types.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_server.h"

using namespace autogen;
using namespace boost::asio;
using namespace boost::assign;
using namespace std;
using namespace test;

class BgpXmppChannelMock : public BgpXmppChannel {
public:
    BgpXmppChannelMock(XmppChannel *channel, BgpServer *server, 
            BgpXmppChannelManager *manager) : 
        BgpXmppChannel(channel, server, manager), count_(0) {
            bgp_policy_ = RibExportPolicy(BgpProto::XMPP,
                                          RibExportPolicy::XMPP, -1, 0);
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_ ++;
        BgpXmppChannel::ReceiveUpdate(msg);
    }

    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }
    virtual ~BgpXmppChannelMock() { }

private:
    size_t count_;
};

class BgpXmppChannelManagerMock : public BgpXmppChannelManager {
public:
    BgpXmppChannelManagerMock(XmppServer *x, BgpServer *b) :
        BgpXmppChannelManager(x, b), count(0), channels(0) { }

    virtual void XmppHandleChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state) {
         count++;
         BgpXmppChannelManager::XmppHandleChannelEvent(channel, state);
    }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        channel_[channels] = new BgpXmppChannelMock(channel, bgp_server_, this);
        channels++;
        return channel_[channels-1];
    }

    int Count() {
        return count;
    }
    int count;
    int channels;
    BgpXmppChannelMock *channel_[3];
};


class BgpXmppMcastTest : public ::testing::Test {
protected:
    static const char *config_tmpl;

    static void ValidateShowRouteResponse(Sandesh *sandesh,
        vector<size_t> &result) {
        ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
        EXPECT_NE((ShowRouteResp *)NULL, resp);

        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_tables().size());

        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < resp->get_tables().size(); i++) {
            TASK_UTIL_EXPECT_EQ(result[i], resp->get_tables()[i].routes.size());
            cout << resp->get_tables()[i].routing_instance << " "
                 << resp->get_tables()[i].routing_table_name << endl;
            for (size_t j = 0; j < resp->get_tables()[i].routes.size(); j++) {
                cout << resp->get_tables()[i].routes[j].prefix << " "
                        << resp->get_tables()[i].routes[j].paths.size() << endl;
            }
        }
        cout << "*******************************************************"<<endl;
        validate_done_ = 1;
    }

    BgpXmppMcastTest() : thread_(&evm_) { }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        xs_x_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        bs_x_->session_manager()->Initialize(0);
        xs_x_->Initialize(0, false);
        bcm_x_.reset(new BgpXmppChannelManagerMock(xs_x_, bs_x_.get()));

        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        xs_y_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        bs_y_->session_manager()->Initialize(0);
        xs_y_->Initialize(0, false);
        bcm_y_.reset(new BgpXmppChannelManagerMock(xs_y_, bs_y_.get()));

        thread_.Start();
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        bcm_x_.reset();
        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;

        if (agent_xa_) { agent_xa_->Delete(); }
        if (agent_xb_) { agent_xb_->Delete(); }
        if (agent_xc_) { agent_xc_->Delete(); }

        xs_y_->Shutdown();
        task_util::WaitForIdle();
        bs_y_->Shutdown();
        task_util::WaitForIdle();
        bcm_y_.reset();
        TcpServerManager::DeleteServer(xs_y_);
        xs_y_ = NULL;

        if (agent_ya_) { agent_ya_->Delete(); }
        if (agent_yb_) { agent_yb_->Delete(); }
        if (agent_yc_) { agent_yc_->Delete(); }

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure() {
        char config[4096];
        snprintf(config, sizeof(config), config_tmpl,
                 bs_x_->session_manager()->GetPort(),
                 bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
    }

    bool CheckOListElem(const test::NetworkAgentMock *agent,
            const string &net, const string &prefix, size_t olist_size,
            const string &address, const string &encap) {
        const NetworkAgentMock::McastRouteEntry *rt =
                agent->McastRouteLookup(net, prefix);
        if (olist_size == 0 && rt != NULL)
            return false;
        if (olist_size == 0)
            return true;
        if (rt == NULL)
            return false;

        const autogen::OlistType &olist = rt->entry.olist;
        if (olist.next_hop.size() != olist_size)
            return false;

        vector<string> tunnel_encapsulation;
        if (encap == "all") {
            tunnel_encapsulation.push_back("gre");
            tunnel_encapsulation.push_back("udp");
        } else if (!encap.empty()) {
            tunnel_encapsulation.push_back(encap);
        }
        sort(tunnel_encapsulation.begin(), tunnel_encapsulation.end());

        for (autogen::OlistType::const_iterator it = olist.begin();
             it != olist.end(); ++it) {
            if (it->address == address) {
                return (it->tunnel_encapsulation_list.tunnel_encapsulation ==
                        tunnel_encapsulation);
            }
        }

        return false;
    }

    void VerifyOListElem(const test::NetworkAgentMock *agent,
            const string &net, const string &prefix, size_t olist_size,
            const string &address, const string &encap = "") {
        TASK_UTIL_EXPECT_TRUE(
                CheckOListElem(agent, net, prefix, olist_size, address, encap));
    }

    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> bs_x_, bs_y_;
    XmppServer *xs_x_, *xs_y_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bcm_x_, bcm_y_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_xa_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_xb_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_xc_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_ya_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_yb_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_yc_;

    static int validate_done_;
};

int BgpXmppMcastTest::validate_done_;

const char *BgpXmppMcastTest::config_tmpl = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet-mvpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.4</identifier>\
        <address>127.0.0.4</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet-mvpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <routing-instance name='blue'>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <vrf-target>target:1:2</vrf-target>\
    </routing-instance>\
</config>\
";

class BgpXmppMcastErrorTest : public BgpXmppMcastTest {
protected:
    virtual void SetUp() {
        BgpXmppMcastTest::SetUp();

        Configure();
        task_util::WaitForIdle();

        // Create agent a and register to multicast table.
        agent_xa_.reset(new test::NetworkAgentMock(
                &evm_, "agent-a", xs_x_->GetPort(), "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_xa_->IsEstablished());
        agent_xa_->McastSubscribe("blue", 1);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        // Trigger TCP close on server and wait for channel to be deleted.
        agent_xa_->SessionDown();
        task_util::WaitForIdle();

        BgpXmppMcastTest::TearDown();
    }
};

TEST_F(BgpXmppMcastErrorTest, BadGroupAddress) {
    agent_xa_->AddMcastRoute("blue", "225.0.0,10.1.1.1", "7.7.7.7", "1000-2000");
    task_util::WaitForIdle();
    InetMVpnTable *blue_table_ = static_cast<InetMVpnTable *>(
        bs_x_->database()->FindTable("blue.inetmvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadSourceAddress) {
    agent_xa_->AddMcastRoute("blue", "225.0.0.1,10.1.1", "7.7.7.7", "1000-2000");
    task_util::WaitForIdle();
    InetMVpnTable *blue_table_ = static_cast<InetMVpnTable *>(
        bs_x_->database()->FindTable("blue.inetmvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadNexthopAddress) {
    agent_xa_->AddMcastRoute("blue", "225.0.0.1,10.1.1.1", "7.7", "1000-2000");
    task_util::WaitForIdle();
    InetMVpnTable *blue_table_ = static_cast<InetMVpnTable *>(
        bs_x_->database()->FindTable("blue.inetmvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadLabelBlock1) {
    agent_xa_->AddMcastRoute("blue", "225.0.0.1,10.1.1.1", "7.7.7.7", "100,200");
    task_util::WaitForIdle();
    InetMVpnTable *blue_table_ = static_cast<InetMVpnTable *>(
        bs_x_->database()->FindTable("blue.inetmvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadLabelBlock2) {
    agent_xa_->AddMcastRoute("blue", "225.0.0.1,10.1.1.1", "7.7.7.7", "1-2-3");
    task_util::WaitForIdle();
    InetMVpnTable *blue_table_ = static_cast<InetMVpnTable *>(
        bs_x_->database()->FindTable("blue.inetmvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

class BgpXmppMcastSubscriptionTest : public BgpXmppMcastTest {
protected:
    virtual void SetUp() {
        BgpXmppMcastTest::SetUp();

        Configure();
        task_util::WaitForIdle();

        // Create agents and wait for the sessions to be Established.
        // Do not register agents to the multicast table.
        agent_xa_.reset(new test::NetworkAgentMock(
                &evm_, "agent-a", xs_x_->GetPort(), "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_xa_->IsEstablished());

        agent_xb_.reset(new test::NetworkAgentMock(
                &evm_, "agent-b", xs_x_->GetPort(), "127.0.0.2"));
        TASK_UTIL_EXPECT_TRUE(agent_xb_->IsEstablished());
    }

    virtual void TearDown() {
        // Trigger TCP close on server and wait for channel to be deleted.
        agent_xa_->SessionDown();
        agent_xb_->SessionDown();
        task_util::WaitForIdle();

        BgpXmppMcastTest::TearDown();
    }
};

TEST_F(BgpXmppMcastSubscriptionTest, PendingSubscribe) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Register agent b to the multicast table and add a mcast route
    // after waiting for the subscription to be processed.
    agent_xb_->McastSubscribe("blue", 1);
    task_util::WaitForIdle();
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");

    // Register agent a to the multicast table and add a mcast route
    // without waiting for the subscription to be processed.
    agent_xa_->McastSubscribe("blue", 1);
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 1, "8.8.8.8");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
}

TEST_F(BgpXmppMcastSubscriptionTest, PendingUnsubscribe) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Register agent b to the multicast table and add a mcast route
    // after waiting for the subscription to be processed.
    agent_xb_->McastSubscribe("blue", 1);
    task_util::WaitForIdle();
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");

    // Register agent a to the multicast table and add a mcast route
    // without waiting for the subscription to be processed. Then go
    // ahead and unsubscribe right away.
    agent_xa_->McastSubscribe("blue", 1);
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xa_->McastUnsubscribe("blue");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xb_->McastRouteCount());

    // Delete mcast route for agent b.
    agent_xb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
}

TEST_F(BgpXmppMcastSubscriptionTest, SubsequentSubscribeUnsubscribe) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Register agent b to the multicast table and add a mcast route
    // after waiting for the subscription to be processed.
    agent_xb_->McastSubscribe("blue", 1);
    task_util::WaitForIdle();
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");

    // Register agent a to the multicast table and add a mcast route
    // without waiting for the subscription to be processed. Then go
    // ahead and unsubscribe right away. Then subscribe again with a
    // different id and add the route again.
    agent_xa_->McastSubscribe("blue", 1);
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xa_->McastUnsubscribe("blue");
    agent_xa_->McastSubscribe("blue", 2);
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 1, "8.8.8.8");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");

    // Verify that agent a mcast route was added with instance_id = 2.
    InetMVpnTable *blue_table_ = static_cast<InetMVpnTable *>(
            bs_x_->database()->FindTable("blue.inetmvpn.0"));
    const char *route = "0-127.0.0.1:2-0.0.0.0,225.0.0.1,0.0.0.0";
    InetMVpnPrefix prefix(InetMVpnPrefix::FromString(route));
    InetMVpnTable::RequestKey key(prefix, NULL);
    TASK_UTIL_EXPECT_TRUE(
        dynamic_cast<InetMVpnRoute *>(blue_table_->Find(&key)) != NULL);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

class BgpXmppMcastMultiAgentTest : public BgpXmppMcastTest {
protected:
    virtual void SetUp() {
        BgpXmppMcastTest::SetUp();

        Configure();
        task_util::WaitForIdle();

        // Create agents and register to multicast table.
        agent_xa_.reset(new test::NetworkAgentMock(
                &evm_, "agent-a", xs_x_->GetPort(), "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_xa_->IsEstablished());
        agent_xa_->McastSubscribe("blue", 1);

        agent_xb_.reset(new test::NetworkAgentMock(
                &evm_, "agent-b", xs_x_->GetPort(), "127.0.0.2"));
        TASK_UTIL_EXPECT_TRUE(agent_xb_->IsEstablished());
        agent_xb_->McastSubscribe("blue", 1);

        agent_xc_.reset(new test::NetworkAgentMock(
                &evm_, "agent-c", xs_x_->GetPort(), "127.0.0.3"));
        TASK_UTIL_EXPECT_TRUE(agent_xc_->IsEstablished());
        agent_xc_->McastSubscribe("blue", 1);
    }

    virtual void TearDown() {
        // Trigger TCP close on server and wait for channel to be deleted.
        agent_xa_->SessionDown();
        agent_xb_->SessionDown();
        agent_xc_->SessionDown();
        task_util::WaitForIdle();

        BgpXmppMcastTest::TearDown();
    }
};

TEST_F(BgpXmppMcastMultiAgentTest, SourceAndGroup) {
    const char *mroute = "225.0.0.1,10.1.1.1";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, GroupOnly) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, MultipleRoutes) {
    const char *mroute1 = "225.0.0.1,10.1.1.1";
    const char *mroute2 = "225.0.0.1,10.1.1.2";

    // Add mcast routes for all agents.
    agent_xa_->AddMcastRoute("blue", mroute1, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute1, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute1, "9.9.9.9", "60000-80000");
    agent_xa_->AddMcastRoute("blue", mroute2, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute2, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute2, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify that all agents have both routes.
    TASK_UTIL_EXPECT_EQ(2, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_xc_->McastRouteCount());

    // Verify all OList elements for the route 1 on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute1, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute1, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute1, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute1, 1, "7.7.7.7");

    // Verify all OList elements for the route 2 on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute2, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute2, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute2, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute2, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute1);
    agent_xb_->DeleteMcastRoute("blue", mroute1);
    agent_xc_->DeleteMcastRoute("blue", mroute1);
    agent_xa_->DeleteMcastRoute("blue", mroute2);
    agent_xb_->DeleteMcastRoute("blue", mroute2);
    agent_xc_->DeleteMcastRoute("blue", mroute2);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, Join) {
    const char *mroute = "225.0.0.1,10.1.1.1";

    // Add mcast route for agents a and b.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 1, "8.8.8.8");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 0, "0.0.0.0");

    // Add mcast route for agent c.
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, Leave) {
    const char *mroute = "225.0.0.1,10.1.1.1";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for agent c.
    agent_xc_->DeleteMcastRoute("blue", mroute);

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 1, "8.8.8.8");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 0, "0.0.0.0");

    // Delete mcast route for agents a and b.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, Introspect) {
    const char *mroute = "225.0.0.1,10.1.1.1";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify that all agents have the route.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify routes via sandesh.
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = bs_x_.get();
    sandesh_context.xmpp_peer_manager = bcm_x_.get();
    Sandesh::set_client_context(&sandesh_context);

    // First get all tables.
    std::vector<size_t> result = list_of(3);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    ShowRouteReq *show_req = new ShowRouteReq;
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Now get blue.inetmvpn.0.
    result = list_of(3);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmvpn.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify that no agents have the route.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xc_->McastRouteCount());

    // Get blue.inetmvpn.0 again.
    result.resize(0);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmvpn.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);
};

TEST_F(BgpXmppMcastMultiAgentTest, ChangeNexthop) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7");

    // Update mcast route for agent_xa - change nexthop to 1.1.1.1.
    agent_xa_->AddMcastRoute("blue", mroute, "1.1.1.1", "10000-20000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "1.1.1.1");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "1.1.1.1");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, MultipleNetworks) {
    const char *mroute = "225.0.0.1,10.1.1.1";

    // Subscribe to another network.
    agent_xa_->McastSubscribe("pink", 2);
    agent_xb_->McastSubscribe("pink", 2);
    agent_xc_->McastSubscribe("pink", 2);
    task_util::WaitForIdle();

    // Add mcast routes in blue and pink for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    agent_xa_->AddMcastRoute("pink", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("pink", mroute, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("pink", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify that all agents have both routes.
    TASK_UTIL_EXPECT_EQ(2, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_xc_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount("pink"));

    // Verify all OList elements for the route in blue on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7");

    // Verify all OList elements for the route in pink on all agents.
    VerifyOListElem(agent_xa_.get(), "pink", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "pink", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "pink", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "pink", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    agent_xa_->DeleteMcastRoute("pink", mroute);
    agent_xb_->DeleteMcastRoute("pink", mroute);
    agent_xc_->DeleteMcastRoute("pink", mroute);
    task_util::WaitForIdle();
};

class BgpXmppMcastMultiServerTest : public BgpXmppMcastTest {
protected:
    virtual void SetUp() {
        BgpXmppMcastTest::SetUp();

        Configure();
        task_util::WaitForIdle();

        // Create agents and register to multicast table.
        agent_xa_.reset(new test::NetworkAgentMock(
                &evm_, "agent-xa", xs_x_->GetPort(), "127.0.0.1", "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_xa_->IsEstablished());
        agent_xa_->McastSubscribe("blue", 1);

        agent_xb_.reset(new test::NetworkAgentMock(
                &evm_, "agent-xb", xs_x_->GetPort(), "127.0.0.2", "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_xb_->IsEstablished());
        agent_xb_->McastSubscribe("blue", 1);

        agent_xc_.reset(new test::NetworkAgentMock(
                &evm_, "agent-xc", xs_x_->GetPort(), "127.0.0.3", "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_xc_->IsEstablished());
        agent_xc_->McastSubscribe("blue", 1);

        agent_ya_.reset(new test::NetworkAgentMock(
                &evm_, "agent-ya", xs_y_->GetPort(), "127.0.0.4", "127.0.0.4"));
        TASK_UTIL_EXPECT_TRUE(agent_ya_->IsEstablished());
        agent_ya_->McastSubscribe("blue", 1);

        agent_yb_.reset(new test::NetworkAgentMock(
                &evm_, "agent-yb", xs_y_->GetPort(), "127.0.0.5", "127.0.0.4"));
        TASK_UTIL_EXPECT_TRUE(agent_yb_->IsEstablished());
        agent_yb_->McastSubscribe("blue", 1);

        agent_yc_.reset(new test::NetworkAgentMock(
                &evm_, "agent-yc", xs_y_->GetPort(), "127.0.0.6", "127.0.0.4"));
        TASK_UTIL_EXPECT_TRUE(agent_yc_->IsEstablished());
        agent_yc_->McastSubscribe("blue", 1);
    }

    virtual void TearDown() {
        // Trigger TCP close on server and wait for channel to be deleted.
        agent_xa_->SessionDown();
        agent_xb_->SessionDown();
        agent_xc_->SessionDown();
        agent_ya_->SessionDown();
        agent_yb_->SessionDown();
        agent_yc_->SessionDown();
        task_util::WaitForIdle();

        BgpXmppMcastTest::TearDown();
    }
};

TEST_F(BgpXmppMcastMultiServerTest, Test1) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for agent xa.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());

    // Add mcast route for agent ya.
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 1, "10.1.1.4");
    VerifyOListElem(agent_ya_.get(), "blue", mroute, 1, "10.1.1.1");

    // Delete mcast route for agent ya.
    agent_ya_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());

    // Delete mcast route for agent xa.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
};

TEST_F(BgpXmppMcastMultiServerTest, TestN) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "60000-80000");
    task_util::WaitForIdle();
    agent_ya_->AddMcastRoute("blue", mroute, "20.1.1.1", "10000-20000");
    agent_yb_->AddMcastRoute("blue", mroute, "20.1.1.2", "40000-60000");
    agent_yc_->AddMcastRoute("blue", mroute, "20.1.1.3", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_yb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_yc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "10.1.1.2");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "10.1.1.3");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "10.1.1.1");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 2, "10.1.1.1");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 2, "20.1.1.3");

    VerifyOListElem(agent_ya_.get(), "blue", mroute, 2, "20.1.1.2");
    VerifyOListElem(agent_ya_.get(), "blue", mroute, 2, "20.1.1.3");
    VerifyOListElem(agent_yb_.get(), "blue", mroute, 1, "20.1.1.1");
    VerifyOListElem(agent_yc_.get(), "blue", mroute, 2, "20.1.1.1");
    VerifyOListElem(agent_yc_.get(), "blue", mroute, 2, "10.1.1.3");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_yc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

class BgpXmppMcastEncapTest : public BgpXmppMcastMultiAgentTest {
protected:
};

TEST_F(BgpXmppMcastEncapTest, ImplicitOnly) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitSingle) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "gre");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "gre");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "gre");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8", "gre");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9", "gre");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7", "gre");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7", "gre");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitAll) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "all");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "all");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8", "all");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9", "all");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7", "all");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7", "all");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitMixed1) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "all");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "gre");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "udp");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8", "gre");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9", "udp");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7", "all");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7", "all");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitMixed2) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "gre");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "all");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8", "all");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9", "all");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7", "gre");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7", "gre");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ImplicitAndExplicitMixed1) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "all");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7", "all");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7", "all");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ImplicitAndExplicitMixed2) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "gre");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8", "gre");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9", "all");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ImplicitAndExplicitMixed3) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "all");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "gre");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8", "gre");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7", "all");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7", "all");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, Change) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7");

    // Update mcast route for all agents - change encaps to all.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "all");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "all");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8", "all");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9", "all");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7", "all");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7", "all");

    // Update mcast route for all agents - change encaps to gre.
    agent_xa_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "gre");
    agent_xb_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "gre");
    agent_xc_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "gre");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "8.8.8.8", "gre");
    VerifyOListElem(agent_xa_.get(), "blue", mroute, 2, "9.9.9.9", "gre");
    VerifyOListElem(agent_xb_.get(), "blue", mroute, 1, "7.7.7.7", "gre");
    VerifyOListElem(agent_xc_.get(), "blue", mroute, 1, "7.7.7.7", "gre");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
}
static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();

    return result;
}
