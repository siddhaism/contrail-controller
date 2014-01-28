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

static const int kRouteCount = 255;

class InetMVpnTableTest : public ::testing::Test {
protected:
    InetMVpnTableTest()
        : server_(&evm_), blue_(NULL) {
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");

        adc_notification_ = 0;
        del_notification_ = 0;

        blue_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            "blue", "target:1.2.3.4:1", "target:1.2.3.4:1"));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(blue_cfg_.get());
        scheduler->Start();
        task_util::WaitForIdle();

        blue_ = static_cast<InetMVpnTable *>(
            server_.database()->FindTable("blue.inetmvpn.0"));
        TASK_UTIL_EXPECT_EQ(Address::INETMVPN, blue_->family());

        tid_ = blue_->Register(
            boost::bind(&InetMVpnTableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        blue_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    void AddRoute(string prefix_str) {
        InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));

        BgpAttrSpec attrs;
        DBRequest addReq;
        addReq.key.reset(new InetMVpnTable::RequestKey(prefix, NULL));
        BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
        addReq.data.reset(new InetMVpnTable::RequestData(attr, 0, 0));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        blue_->Enqueue(&addReq);
    }

    void DelRoute(string prefix_str) {
        InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));

        DBRequest delReq;
        delReq.key.reset(new InetMVpnTable::RequestKey(prefix, NULL));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        blue_->Enqueue(&delReq);
    }

    InetMVpnRoute *FindRoute(string prefix_str) {
        InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));
        InetMVpnTable::RequestKey key(prefix, NULL);
        InetMVpnRoute *rt = dynamic_cast<InetMVpnRoute *>(blue_->Find(&key));
        return rt;
    }

    void VerifyRouteExists(string prefix_str) {
        InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));
        InetMVpnTable::RequestKey key(prefix, NULL);
        InetMVpnRoute *rt = dynamic_cast<InetMVpnRoute *>(blue_->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        TASK_UTIL_EXPECT_EQ(1, rt->count());
    }

    void VerifyRouteNoExists(string prefix_str) {
        InetMVpnPrefix prefix(InetMVpnPrefix::FromString(prefix_str));
        InetMVpnTable::RequestKey key(prefix, NULL);
        InetMVpnRoute *rt = static_cast<InetMVpnRoute *>(blue_->Find(&key));
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
    InetMVpnTable *blue_;
    DBTableBase::ListenerId tid_;
    scoped_ptr<BgpInstanceConfig> blue_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

class InetMVpnNativeTest : public InetMVpnTableTest {
};

TEST_F(InetMVpnNativeTest, AddDeleteSingleRoute) {
    AddRoute("0-10.1.1.1:65535-0,192.168.1.255,0.0.0.0");
    task_util::WaitForIdle();
    VerifyRouteExists("0-10.1.1.1:65535-0,192.168.1.255,0.0.0.0");
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    InetMVpnRoute *rt = FindRoute("0-10.1.1.1:65535-0,192.168.1.255,0.0.0.0");
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, rt->Afi());
    TASK_UTIL_EXPECT_EQ(BgpAf::McastVpn, rt->Safi());
    TASK_UTIL_EXPECT_EQ(BgpAf::Mcast, rt->XmppSafi());

    DelRoute("0-10.1.1.1:65535-0,192.168.1.255,0.0.0.0");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists("0-10.1.1.1:65535-0,192.168.1.255,0.0.0.0");
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(InetMVpnNativeTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0,224.168.1.255,192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0,224.168.1.255,192.168.1.1";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0,224.168.1.255,192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0,224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the group field.
TEST_F(InetMVpnNativeTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0,224.168.1." << idx << ",192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0,224.168.1." << idx << ",192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the source field.
TEST_F(InetMVpnNativeTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0,224.168.1.255,192.168.1." << idx;
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0,224.168.1.255,192.168.1." << idx;
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0,224.168.1.255,192.168.1." << idx;
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0,224.168.1.255,192.168.1." << idx;
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(InetMVpnNativeTest, Hashing) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0,224.168.1." << idx << ",192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0,224.168.1." << idx << ",192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();
}

class InetMVpnCMcastTest : public InetMVpnTableTest {
};

TEST_F(InetMVpnCMcastTest, AddDeleteSingleRoute) {
    AddRoute("7-10.1.1.1:65535-65412,192.168.1.255,0.0.0.0");
    task_util::WaitForIdle();
    VerifyRouteExists("7-10.1.1.1:65535-65412,192.168.1.255,0.0.0.0");
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    InetMVpnRoute *rt = FindRoute("7-10.1.1.1:65535-65412,192.168.1.255,0.0.0.0");
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, rt->Afi());
    TASK_UTIL_EXPECT_EQ(BgpAf::McastVpn, rt->Safi());

    DelRoute("7-10.1.1.1:65535-65412,192.168.1.255,0.0.0.0");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists("7-10.1.1.1:65535-65412,192.168.1.255,0.0.0.0");
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(InetMVpnCMcastTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1." << idx << ":65535-65412,224.168.1.255,192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1." << idx << ":65535-65412,224.168.1.255,192.168.1.1";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1." << idx << ":65535-65412,224.168.1.255,192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1." << idx << ":65535-65412,224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the group field.
TEST_F(InetMVpnCMcastTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1.1:65535-65412,224.168.1." << idx << ",192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1.1:65535-65412,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1.1:65535-65412,224.168.1." << idx << ",192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1.1:65535-65412,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the source field.
TEST_F(InetMVpnCMcastTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1.1:65535-65412,224.168.1.255,192.168.1." << idx;
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1.1:65535-65412,224.168.1.255,192.168.1." << idx;
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1.1:65535-65412,224.168.1.255,192.168.1." << idx;
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1.1:65535-65412,224.168.1.255,192.168.1." << idx;
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the as number field.
TEST_F(InetMVpnCMcastTest, AddDeleteMultipleRoute4) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-" << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-" << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-" << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-" << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(InetMVpnCMcastTest, Hashing) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1.1:65535-65412,224.168.1." << idx << ",192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "7-10.1.1.1:65535-65412,224.168.1." << idx << ",192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();
}

class InetMVpnTreeTest : public InetMVpnTableTest {
};

TEST_F(InetMVpnTreeTest, AddDeleteSingleRoute) {
    AddRoute("8-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0");
    task_util::WaitForIdle();
    VerifyRouteExists("8-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0");
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    InetMVpnRoute *rt = FindRoute("8-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0");
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, rt->Afi());
    TASK_UTIL_EXPECT_EQ(BgpAf::McastVpn, rt->Safi());

    DelRoute("8-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists("8-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0");
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(InetMVpnTreeTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the group field.
TEST_F(InetMVpnTreeTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the source field.
TEST_F(InetMVpnTreeTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the router-id field.
TEST_F(InetMVpnTreeTest, AddDeleteMultipleRoute4) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(InetMVpnTreeTest, Hashing) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "8-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();
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
