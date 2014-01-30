/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inetmvpn/inetmvpn_table.h"

#include <boost/bind.hpp>
#include <tbb/atomic.h>

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_multicast.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"
#include "testing/gunit.h"

using namespace std;
using namespace boost;

class McastTreeManagerMock : public McastTreeManager {
public:
    McastTreeManagerMock(InetMVpnTable *table) : McastTreeManager(table) {
    }
    ~McastTreeManagerMock() { }

    virtual void Initialize() { }
    virtual void Terminate() { }

    virtual UpdateInfo *GetUpdateInfo(InetMVpnRoute *route) { return NULL; }

private:
};

static const int kRouteCount = 8;

class InetMVpnTableTest : public ::testing::Test {
protected:
    InetMVpnTableTest()
        : server_(&evm_), blue_(NULL) {
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");

        adc_notification_ = 0;
        del_notification_ = 0;

        master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            BgpConfigManager::kMasterInstance, "", ""));
        blue_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            "blue", "target:65412:1", "target:65412:1"));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(master_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(blue_cfg_.get());
        scheduler->Start();
        task_util::WaitForIdle();

        blue_ = static_cast<InetMVpnTable *>(
            server_.database()->FindTable("blue.inetmvpn.0"));
        TASK_UTIL_EXPECT_EQ(Address::INETMVPN, blue_->family());
        master_ = static_cast<InetMVpnTable *>(
            server_.database()->FindTable("bgp.inetmvpn.0"));
        TASK_UTIL_EXPECT_EQ(Address::INETMVPN, master_->family());

        tid_ = blue_->Register(
            boost::bind(&InetMVpnTableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        blue_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    void AddRoute(InetMVpnTable *table, string prefix_str,
            string rtarget_str = "", string source_rd_str = "") {
        InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));

        BgpAttrSpec attrs;
        ExtCommunitySpec ext_comm;
        if (!rtarget_str.empty()) {
            RouteTarget rtarget = RouteTarget::FromString(rtarget_str);
            ext_comm.communities.push_back(rtarget.GetExtCommunityValue());
            attrs.push_back(&ext_comm);
        }
        BgpAttrSourceRd source_rd;
        if (!source_rd_str.empty()) {
            source_rd = BgpAttrSourceRd(
                RouteDistinguisher::FromString(source_rd_str));
            attrs.push_back(&source_rd);
        }
        BgpAttrPtr attr = server_.attr_db()->Locate(attrs);

        DBRequest addReq;
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        addReq.key.reset(new InetMVpnTable::RequestKey(prefix, NULL));
        addReq.data.reset(new InetMVpnTable::RequestData(attr, 0, 0));
        table->Enqueue(&addReq);
    }

    void DelRoute(InetMVpnTable *table, string prefix_str) {
        InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));

        DBRequest delReq;
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        delReq.key.reset(new InetMVpnTable::RequestKey(prefix, NULL));
        table->Enqueue(&delReq);
    }

    InetMVpnRoute *FindRoute(InetMVpnTable *table, string prefix_str) {
        InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));
        InetMVpnTable::RequestKey key(prefix, NULL);
        InetMVpnRoute *rt = dynamic_cast<InetMVpnRoute *>(table->Find(&key));
        return rt;
    }

    void VerifyRouteExists(InetMVpnTable *table, string prefix_str,
            size_t count = 1) {
        InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));
        InetMVpnTable::RequestKey key(prefix, NULL);
        TASK_UTIL_EXPECT_TRUE(table->Find(&key) != NULL);
        InetMVpnRoute *rt = dynamic_cast<InetMVpnRoute *>(table->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        TASK_UTIL_EXPECT_EQ(count, rt->count());
    }

    void VerifyRouteNoExists(InetMVpnTable *table, string prefix_str) {
        InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));
        InetMVpnTable::RequestKey key(prefix, NULL);
        TASK_UTIL_EXPECT_TRUE(table->Find(&key) == NULL);
        InetMVpnRoute *rt = static_cast<InetMVpnRoute *>(table->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt == NULL);
    }

    void TableListener(DBTablePartBase *tpart, DBEntryBase *entry) {
        bool del_notify = entry->IsDeleted();
        if (del_notify) {
            del_notification_++;
        } else {
            adc_notification_++;
        }
    }

    EventManager evm_;
    BgpServer server_;
    InetMVpnTable *master_;
    InetMVpnTable *blue_;
    DBTableBase::ListenerId tid_;
    scoped_ptr<BgpInstanceConfig> master_cfg_;
    scoped_ptr<BgpInstanceConfig> blue_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

class InetMVpnNativeTest : public InetMVpnTableTest {
};

TEST_F(InetMVpnNativeTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "0-10.1.1.1:65535-0.0.0.0,192.168.1.255,0.0.0.0";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());

    InetMVpnRoute *rt = FindRoute(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, rt->Afi());
    TASK_UTIL_EXPECT_EQ(BgpAf::McastVpn, rt->Safi());
    TASK_UTIL_EXPECT_EQ(BgpAf::Mcast, rt->XmppSafi());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(InetMVpnNativeTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0.0.0.0,224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0.0.0.0,224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0.0.0.0,224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0.0.0.0,224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the group field.
TEST_F(InetMVpnNativeTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the source field.
TEST_F(InetMVpnNativeTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1.255,192.168.1." << idx;
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1.255,192.168.1." << idx;
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1.255,192.168.1." << idx;
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1.255,192.168.1." << idx;
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(InetMVpnNativeTest, Hashing) {
    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();
}

class InetMVpnCMcastTest : public InetMVpnTableTest {
};

TEST_F(InetMVpnCMcastTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "1-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());

    InetMVpnRoute *rt = FindRoute(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, rt->Afi());
    TASK_UTIL_EXPECT_EQ(BgpAf::McastVpn, rt->Safi());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(InetMVpnCMcastTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the group field.
TEST_F(InetMVpnCMcastTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the source field.
TEST_F(InetMVpnCMcastTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the router-id field.
TEST_F(InetMVpnCMcastTest, AddDeleteMultipleRoute4) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(InetMVpnCMcastTest, Hashing) {
    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();
}

TEST_F(InetMVpnCMcastTest, ReplicateRouteFromVPN1) {
    ostringstream repr1, repr2;
    repr1 << "1-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    repr2 << "1-0:0-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(master_, repr1.str(), "target:65412:1");
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    DelRoute(master_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    VerifyRouteNoExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

TEST_F(InetMVpnCMcastTest, RouteReplicateFromVPN2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(master_, repr1.str(), "target:65412:1");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "1-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(master_, repr1.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "1-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(master_, repr1.str());
        VerifyRouteNoExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

TEST_F(InetMVpnCMcastTest, RouteReplicateFromVPN3) {
    ostringstream repr2;
    repr2 << "1-0:0-20.1.1.1,224.168.1.255,192.168.1.1";

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        AddRoute(master_, repr1.str(), "target:65412:1");
        task_util::WaitForIdle();
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str(), idx);
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    InetMVpnRoute *rt = FindRoute(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(kRouteCount, rt->count());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        DelRoute(master_, repr1.str());
        task_util::WaitForIdle();
        VerifyRouteNoExists(master_, repr1.str());
        if (idx == kRouteCount) {
            VerifyRouteNoExists(blue_, repr2.str());
        } else {
            VerifyRouteExists(blue_, repr2.str(), kRouteCount - idx);
        }
    }

    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

class InetMVpnTreeTest : public InetMVpnTableTest {
};

TEST_F(InetMVpnTreeTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "2-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    InetMVpnRoute *rt = FindRoute(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, rt->Afi());
    TASK_UTIL_EXPECT_EQ(BgpAf::McastVpn, rt->Safi());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(blue_, repr.str());
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(InetMVpnTreeTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the group field.
TEST_F(InetMVpnTreeTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the source field.
TEST_F(InetMVpnTreeTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the router-id field.
TEST_F(InetMVpnTreeTest, AddDeleteMultipleRoute4) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(InetMVpnTreeTest, Hashing) {
    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();
}

TEST_F(InetMVpnTreeTest, ReplicateRouteToVPN1) {
    ostringstream repr1, repr2;
    repr1 << "2-0:0-20.1.1.1,192.168.1.255,0.0.0.0";
    repr2 << "2-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(blue_, repr1.str(), "", "10.1.1.1:65535");
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr1.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    VerifyRouteExists(master_, repr2.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());

    DelRoute(blue_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, repr1.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
    VerifyRouteNoExists(master_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

TEST_F(InetMVpnTreeTest, RouteReplicateToVPN2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr1.str(), "", "10.1.1.1:65535");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr1.str());
        VerifyRouteExists(master_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr1.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr1.str());
        VerifyRouteNoExists(master_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

TEST_F(InetMVpnTreeTest, ReplicateRouteFromVPN1) {
    ostringstream repr1, repr2;
    repr1 << "2-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    repr2 << "2-0:0-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(master_, repr1.str(), "target:65412:1");
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    DelRoute(master_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    VerifyRouteNoExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

TEST_F(InetMVpnTreeTest, RouteReplicateFromVPN2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(master_, repr1.str(), "target:65412:1");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(master_, repr1.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(master_, repr1.str());
        VerifyRouteNoExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

TEST_F(InetMVpnTreeTest, RouteReplicateFromVPN3) {
    ostringstream repr2;
    repr2 << "2-0:0-20.1.1.1,224.168.1.255,192.168.1.1";

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        AddRoute(master_, repr1.str(), "target:65412:1");
        task_util::WaitForIdle();
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str(), idx);
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    InetMVpnRoute *rt = FindRoute(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(kRouteCount, rt->count());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        DelRoute(master_, repr1.str());
        task_util::WaitForIdle();
        VerifyRouteNoExists(master_, repr1.str());
        if (idx == kRouteCount) {
            VerifyRouteNoExists(blue_, repr2.str());
        } else {
            VerifyRouteExists(blue_, repr2.str(), kRouteCount - idx);
        }
    }

    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<McastTreeManager>(
        boost::factory<McastTreeManagerMock *>());
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
