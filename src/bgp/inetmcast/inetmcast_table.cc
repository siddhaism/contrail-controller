/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inetmcast/inetmcast_table.h"

#include <boost/functional/hash.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_route.h"
#include "bgp/ipeer.h"
#include "bgp/inetmcast/inetmcast_route.h"
#include "bgp/inetmvpn/inetmvpn_route.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db_table_partition.h"

using namespace std;

size_t InetMcastTable::HashFunction(const InetMcastPrefix &prefix) {
    return boost::hash_value(prefix.group().to_ulong());
}

InetMcastTable::InetMcastTable(DB *db, const std::string &name)
    : BgpTable(db, name), tree_manager_(NULL) {
}

std::auto_ptr<DBEntry> InetMcastTable::AllocEntry(
        const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return std::auto_ptr<DBEntry> (new InetMcastRoute(pfxkey->prefix));
}

std::auto_ptr<DBEntry> InetMcastTable::AllocEntryStr(
        const string &key_str) const {
    InetMcastPrefix prefix = InetMcastPrefix::FromString(key_str);
    return std::auto_ptr<DBEntry> (new InetMcastRoute(prefix));
}

size_t InetMcastTable::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    size_t value = HashFunction(rkey->prefix);
    return value % DB::PartitionCount();
}

size_t InetMcastTable::Hash(const DBEntry *entry) const {
    const InetMcastRoute *rt_entry = static_cast<const InetMcastRoute *>(entry);
    size_t value = HashFunction(rt_entry->GetPrefix());
    return value % DB::PartitionCount();
}

BgpRoute *InetMcastTable::TableFind(DBTablePartition *rtp,
        const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    InetMcastRoute rt_key(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&rt_key));
}

DBTableBase *InetMcastTable::CreateTable(DB *db, const std::string &name) {
    InetMcastTable *table = new InetMcastTable(db, name);
    table->Init();
    return table;
}

BgpRoute *InetMcastTable::RouteReplicate(BgpServer *server,
        BgpTable *src_table, BgpRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr community) {
    if (src_table->family() != Address::INETMVPN)
        return NULL;

    OriginVn origin_vn(server->autonomous_system(),
        routing_instance()->virtual_network_index());
    if (!community->ContainsOriginVn(origin_vn.GetExtCommunity()))
        return NULL;

    InetMcastRoute *inetmcast= dynamic_cast<InetMcastRoute *>(src_rt);
    boost::scoped_ptr<InetMcastPrefix> inetmcast_prefix;

    if (inetmcast) {
        inetmcast_prefix.reset(
            new InetMcastPrefix(inetmcast->GetPrefix().route_distinguisher(),
            inetmcast->GetPrefix().group(), inetmcast->GetPrefix().source()));
    } else {
        InetMVpnRoute *inetmvpn = dynamic_cast<InetMVpnRoute *>(src_rt);
        assert(inetmvpn);
        inetmcast_prefix.reset(
            new InetMcastPrefix(inetmvpn->GetPrefix().route_distinguisher(),
            inetmvpn->GetPrefix().group(), inetmvpn->GetPrefix().source()));
    }

    InetMcastRoute rt_key(*inetmcast_prefix);
    DBTablePartition *rtp =
        static_cast<DBTablePartition *>(GetTablePartition(&rt_key));
    BgpRoute *dest_route = static_cast<BgpRoute *>(rtp->Find(&rt_key));
    if (dest_route == NULL) {
        dest_route = new InetMcastRoute(rt_key.GetPrefix());
        rtp->Add(dest_route);
    } else {
        dest_route->ClearDelete();
    }

    // Replace the extended community with the one provided.
    BgpAttrPtr new_attr = server->attr_db()->ReplaceExtCommunityAndLocate(
        src_path->GetAttr(), community);

    // Check whether peer already has a path.
    BgpPath *dest_path =
        dest_route->FindSecondaryPath(src_rt, src_path->GetSource(),
                                      src_path->GetPeer(), 
                                      src_path->GetPathId());
    if (dest_path != NULL) {
        if ((new_attr != dest_path->GetAttr()) ||
            (src_path->GetLabel() != dest_path->GetLabel())) {
            assert(dest_route->RemoveSecondaryPath(src_rt,
                       src_path->GetSource(), src_path->GetPeer(),
                       src_path->GetPathId()));
        } else {
            return dest_route;
        }
    }

    BgpSecondaryPath *replicated_path =
        new BgpSecondaryPath(src_path->GetPeer(), src_path->GetPathId(),
                             src_path->GetSource(), new_attr,
                             src_path->GetFlags(), src_path->GetLabel());
    replicated_path->SetReplicateInfo(src_table, src_rt);
    dest_route->InsertPath(replicated_path);

    // Notify the route even if the best path may not have changed. For XMPP
    // peers, we support sending multiple ECMP next-hops for a single route.
    rtp->Notify(dest_route);

    return dest_route;
}

bool InetMcastTable::Export(RibOut *ribout, Route *route,
        const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {
    InetMcastRoute *mcast_route = dynamic_cast<InetMcastRoute *>(route);
    assert(mcast_route);

    if (!tree_manager_ || tree_manager_->deleter()->IsDeleted())
        return false;

    const IPeer *peer = mcast_route->BestPath()->GetPeer();
    if (!peer || !ribout->IsRegistered(const_cast<IPeer *>(peer)))
        return false;

    size_t peerbit = ribout->GetPeerIndex(const_cast<IPeer *>(peer));
    if (!peerset.test(peerbit))
        return false;

    UpdateInfo *uinfo = tree_manager_->GetUpdateInfo(mcast_route);
    if (!uinfo)
        return false;

    uinfo->target.set(peerbit);
    uinfo_slist->push_front(*uinfo);
    return true;
}

void InetMcastTable::CreateTreeManager() {
    assert(!tree_manager_);
    tree_manager_ = BgpObjectFactory::Create<McastTreeManager>(this);
    tree_manager_->Initialize();
}

void InetMcastTable::DestroyTreeManager() {
    tree_manager_->Terminate();
    delete tree_manager_;
    tree_manager_ = NULL;
}

McastTreeManager *InetMcastTable::GetTreeManager() {
    return tree_manager_;
}

void InetMcastTable::set_routing_instance(RoutingInstance *rtinstance) {
    BgpTable::set_routing_instance(rtinstance);
    CreateTreeManager();
}

static void RegisterFactory() {
    DB::RegisterFactory("inetmcast.0", &InetMcastTable::CreateTable);
}

MODULE_INITIALIZER(RegisterFactory);
