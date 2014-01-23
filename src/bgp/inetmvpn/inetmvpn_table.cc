/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inetmvpn/inetmvpn_table.h"

#include "base/util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/inetmcast/inetmcast_route.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inetmvpn/inetmvpn_route.h"
#include "bgp/bgp_multicast.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db_table_partition.h"

using namespace std;

size_t InetMVpnTable::HashFunction(const InetMVpnPrefix &prefix) const {
    return boost::hash_value(prefix.group().to_ulong());
}

InetMVpnTable::InetMVpnTable(DB *db, const string &name)
        : BgpTable(db, name), tree_manager_(NULL) {
}

std::auto_ptr<DBEntry> InetMVpnTable::AllocEntry(
        const DBRequestKey *key) const {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(key);
    return std::auto_ptr<DBEntry> (new InetMVpnRoute(pfxkey->prefix));
}


std::auto_ptr<DBEntry> InetMVpnTable::AllocEntryStr(
        const string &key_str) const {
    boost::system::error_code ec;
    InetMVpnPrefix prefix = InetMVpnPrefix::FromString(key_str, ec);
    return std::auto_ptr<DBEntry> (new InetMVpnRoute(prefix));
}

size_t InetMVpnTable::Hash(const DBEntry *entry) const {
    const InetMVpnRoute *rt_entry = static_cast<const InetMVpnRoute *>(entry);
    const InetMVpnPrefix &inetmvpnprefix = rt_entry->GetPrefix();
    size_t value = InetMVpnTable::HashFunction(inetmvpnprefix);
    return value % DB::PartitionCount();
}

size_t InetMVpnTable::Hash(const DBRequestKey *key) const {
    const RequestKey *rkey = static_cast<const RequestKey *>(key);
    Ip4Prefix prefix(rkey->prefix.group(), 32);
    size_t value = InetTable::HashFunction(prefix);
    return value % DB::PartitionCount();
}

BgpRoute *InetMVpnTable::TableFind(DBTablePartition *rtp,
                                   const DBRequestKey *prefix) {
    const RequestKey *pfxkey = static_cast<const RequestKey *>(prefix);
    InetMVpnRoute rt_key(pfxkey->prefix);
    return static_cast<BgpRoute *>(rtp->Find(&rt_key));
}

DBTableBase *InetMVpnTable::CreateTable(DB *db, const std::string &name) {
    InetMVpnTable *table = new InetMVpnTable(db, name);
    table->Init();
    return table;
}

static RouteDistinguisher GenerateDistinguisher(
        const BgpTable *src_table, const BgpPath *src_path) {
    RouteDistinguisher source_rd = src_path->GetAttr()->source_rd();
    if (!source_rd.IsNull())
        return source_rd;

    assert(!src_path->GetPeer() || !src_path->GetPeer()->IsXmppPeer());
    const RoutingInstance *src_instance = src_table->routing_instance();
    return *src_instance->GetRD();
}

BgpRoute *InetMVpnTable::RouteReplicate(BgpServer *server,
        BgpTable *src_table, BgpRoute *src_rt, const BgpPath *src_path,
        ExtCommunityPtr community) {
    assert(src_table->family()  == Address::INET);

    InetMcastRoute *route = dynamic_cast<InetMcastRoute *> (src_rt);
    assert(route);

    // Check whether there's already a path with the given peer and path id.
    BgpPath *dest_path =
        route->FindSecondaryPath(src_rt, src_path->GetSource(),
                                      src_path->GetPeer(),
                                      src_path->GetPathId());

    BgpAttrPtr edge_discovery_attribute;
    if (!tree_manager_->ShouldReplicate(route, edge_discovery_attribute)) {
        if (route) {
            assert(route->RemoveSecondaryPath(src_rt, src_path->GetSource(),
                   src_path->GetPeer(), src_path->GetPathId()));
        }
        return NULL;
    }

    BgpAttrPtr new_attr =
        server->attr_db()->ReplaceMulticastEdgeDiscoveryAndLocate(
                   src_path->GetAttr(), edge_discovery_attribute);

    const RouteDistinguisher &rd = GenerateDistinguisher(src_table, src_path);

    InetMVpnPrefix vpn(rd, server->bgp_identifier(), route->GetPrefix().group(),
            route->GetPrefix().source());

    InetMVpnRoute rt_key(vpn);

    DBTablePartition *rtp =
        static_cast<DBTablePartition *>(GetTablePartition(&rt_key));
    BgpRoute *dest_route = static_cast<BgpRoute *>(rtp->Find(&rt_key));
    if (dest_route == NULL) {
        dest_route = new InetMVpnRoute(vpn);
        rtp->Add(dest_route);
    } else {
        dest_route->ClearDelete();
    }

    new_attr = server->attr_db()->ReplaceExtCommunityAndLocate(
            new_attr.get(), community);
    new_attr = server->attr_db()->ReplaceMulticastEdgeDiscoveryAndLocate(
                   new_attr.get(), edge_discovery_attribute);
    if (dest_path != NULL) {
        if ((new_attr != dest_path->GetAttr()) || 
            (src_path->GetLabel() != dest_path->GetLabel())) {
            // Update Attributes and notify (if needed)
            assert(dest_route->RemoveSecondaryPath(src_rt,
                                src_path->GetSource(), src_path->GetPeer(), 
                                src_path->GetPathId()));
        } else {
            return dest_route;
        }
    }

    // Create replicated path and insert it on the route
    BgpSecondaryPath *replicated_path = 
        new BgpSecondaryPath(src_path->GetPeer(), src_path->GetPathId(), 
                            src_path->GetSource(), new_attr, 
                            src_path->GetFlags(), src_path->GetLabel());
    replicated_path->SetReplicateInfo(src_table, src_rt);
    dest_route->InsertPath(replicated_path);

    // Trigger notification only if the inserted path is selected
    if (replicated_path == dest_route->front())
        rtp->Notify(dest_route);

    return dest_route;
}

bool InetMVpnTable::Export(RibOut *ribout, Route *route,
        const RibPeerSet &peerset, UpdateInfoSList &uinfo_slist) {
    InetMVpnRoute *inetmvpn_route = dynamic_cast<InetMVpnRoute *>(route);
    assert(inetmvpn_route);

    if (!tree_manager_ || tree_manager_->deleter()->IsDeleted())
        return false;

    const IPeer *peer = inetmvpn_route->BestPath()->GetPeer();
    if (!peer || !ribout->IsRegistered(const_cast<IPeer *>(peer)))
        return false;

    size_t peerbit = ribout->GetPeerIndex(const_cast<IPeer *>(peer));
    if (!peerset.test(peerbit))
        return false;

    UpdateInfo *uinfo = tree_manager_->GetUpdateInfo(inetmvpn_route);
    if (!uinfo)
        return false;

    uinfo->target.set(peerbit);
    uinfo_slist->push_front(*uinfo);
    return true;
}

void InetMVpnTable::CreateTreeManager() {
    assert(!tree_manager_);
    tree_manager_ = BgpObjectFactory::Create<McastTreeManager>(this);
    tree_manager_->Initialize();
}

void InetMVpnTable::DestroyTreeManager() {
    tree_manager_->Terminate();
    delete tree_manager_;
    tree_manager_ = NULL;
}

McastTreeManager *InetMVpnTable::GetTreeManager() {
    return tree_manager_;
}

void InetMVpnTable::set_routing_instance(RoutingInstance *rtinstance) {
    BgpTable::set_routing_instance(rtinstance);
    CreateTreeManager();
}

static void RegisterFactory() {
    DB::RegisterFactory("bgp.mvpn.0", &InetMVpnTable::CreateTable);
}
MODULE_INITIALIZER(RegisterFactory);
