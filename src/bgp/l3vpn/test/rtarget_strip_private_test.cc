/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/l3vpn/inetvpn_table.h"

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_message_builder.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/rtarget/rtarget_address.h"
#include "bgp/scheduling_group.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

class BgpPeerMock : public IPeer {
public:
    virtual std::string ToString() const { return "test-peer"; }
    virtual std::string ToUVEKey() const { return "test-peer"; }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        BGP_DEBUG_UT("UPDATE " << msgsize << " bytes");
        return true;
    }
    virtual BgpServer *server() { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close() { }
    BgpProto::BgpPeerType PeerType() const { return BgpProto::IBGP; }
    virtual uint32_t bgp_identifier() const { return 0; }
    virtual const std::string GetStateName() const { return "UNKNOWN"; }
    virtual void UpdateRefCount(int count) { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }
};

class MessageBuilderMock : public BgpMessageBuilder {
public:
    typedef ExtCommunity::ExtCommunityList ExtCommunityList;
    virtual Message *Create(const BgpTable *table,
                            const RibOutAttr *attr,
                            const BgpRoute *route) const {
        const ExtCommunityList &attr_list =
                attr->attr()->ext_community()->communities();
        community_list_.insert(community_list_.end(),
                               attr_list.begin(), attr_list.end());
        return new MessageMock();
    }

    const ExtCommunityList &community_list() { return community_list_; }

private:
    class MessageMock : public Message {
      public:
        MOCK_METHOD2(AddRoute, bool(const BgpRoute *, const RibOutAttr *));
        MOCK_METHOD0(Finish, void());
        virtual const uint8_t *GetData(IPeerUpdate *peer, size_t *lenp) {
            lenp = 0;
            return NULL;
        }
    };
    mutable ExtCommunityList community_list_;
};

class InetVpnTableExportTest : public ::testing::Test {
  protected:
    InetVpnTableExportTest()
            : server_(&evm_), rib_(server_.database(), "bgp.l3vpn.0"),
              ribout_(&rib_, &mgr_,
                      RibExportPolicy(BgpProto::EBGP, RibExportPolicy::BGP,
                                      10458, -1, 0, true)) {
        rib_.Init();
    }

    ~InetVpnTableExportTest() {
        STLDeleteValues(&peers_);
    }

    virtual void SetUp() {
        server_.database()->AddTable(&rib_);
        ribout_.updates()->SetMessageBuilder(&builder_);
        ribout_.RegisterListener();
    }

    virtual void TearDown() {
        server_.database()->RemoveTable(&rib_);

        server_.Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        task_util::WaitForIdle();
    }


    void RibOutRegister(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Register(peer);
        int bit = ribout->GetPeerIndex(peer);
        ribout->updates()->QueueJoin(RibOutUpdates::QUPDATE, bit);
        ribout->updates()->QueueJoin(RibOutUpdates::QBULK, bit);
    }

    void RibOutUnregister(RibOut *ribout, IPeerUpdate *peer) {
        int bit = ribout->GetPeerIndex(peer);
        ribout->updates()->QueueLeave(RibOutUpdates::QUPDATE, bit);
        ribout->updates()->QueueLeave(RibOutUpdates::QBULK, bit);
        ribout->Unregister(peer);
    }

    void CreatePeer() {
        BgpPeerMock *peer = new BgpPeerMock();
        RibOutRegister(&ribout_, peer);
        peers_.push_back(peer);
    }

    EventManager evm_;
    BgpServer server_;
    InetVpnTable rib_;
    SchedulingGroupManager mgr_;
    RibOut ribout_;
    MessageBuilderMock builder_;
    std::vector<IPeer *> peers_;
};

TEST_F(InetVpnTableExportTest, NoMatch) {
    CreatePeer();

    // Create a route prefix
    InetVpnPrefix prefix(InetVpnPrefix::FromString("123:456:192.168.24.0/24"));

    // Create a set of route attributes
    BgpAttrSpec attrs;
    RouteTarget rtarget = RouteTarget::FromString("target:10458:1");
    ExtCommunitySpec cspec;
    cspec.communities.push_back(rtarget.GetExtCommunityValue());
    attrs.push_back(&cspec);

    EXPECT_EQ(&rib_, server_.database()->FindTable("bgp.l3vpn.0"));

    // Enqueue the update
    DBRequest addReq;
    addReq.key.reset(new InetVpnTable::RequestKey(prefix, NULL));
    BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
    addReq.data.reset(new InetVpnTable::RequestData(attr, 0, 20));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    rib_.Enqueue(&addReq);

    task_util::WaitForIdle();

    EXPECT_EQ(1, builder_.community_list().size());
    EXPECT_EQ(rtarget.GetExtCommunity(), builder_.community_list().at(0));
}

TEST_F(InetVpnTableExportTest, Match) {
    CreatePeer();

    // Create a route prefix
    InetVpnPrefix prefix(InetVpnPrefix::FromString("123:456:192.168.24.0/24"));

    // Create a set of route attributes
    BgpAttrSpec attrs;
    RouteTarget rt1 = RouteTarget::FromString("target:10458:1");
    RouteTarget rt2 = RouteTarget::FromString("target:64512:1");
    ExtCommunitySpec cspec;
    cspec.communities.push_back(rt1.GetExtCommunityValue());
    cspec.communities.push_back(rt2.GetExtCommunityValue());
    attrs.push_back(&cspec);

    EXPECT_EQ(&rib_, server_.database()->FindTable("bgp.l3vpn.0"));

    // Enqueue the update
    DBRequest addReq;
    addReq.key.reset(new InetVpnTable::RequestKey(prefix, NULL));
    BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
    addReq.data.reset(new InetVpnTable::RequestData(attr, 0, 20));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    rib_.Enqueue(&addReq);

    task_util::WaitForIdle();

    EXPECT_EQ(1, builder_.community_list().size());
    EXPECT_EQ(rt1.GetExtCommunity(), builder_.community_list().at(0));
}

static void SetUp() {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}

