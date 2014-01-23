/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inetmvpn/inetmvpn_route.h"
#include "bgp/inetmvpn/inetmvpn_table.h"
#include "bgp/inet/inet_route.h"

using namespace std;

InetMVpnRoute::InetMVpnRoute(const InetMVpnPrefix &prefix)
    : prefix_(prefix) {
}

int InetMVpnRoute::CompareTo(const Route &rhs) const {
    const InetMVpnRoute &other = static_cast<const InetMVpnRoute &>(rhs);
    int res = prefix_.route_distinguisher().CompareTo(
        other.prefix_.route_distinguisher());
    if (res != 0) {
        return res;
    }
    Ip4Address laddr = prefix_.source();
    Ip4Address raddr = other.prefix_.source();
    if (laddr < raddr) return -1;
    if (laddr > raddr) return 1;

    laddr = prefix_.group();
    raddr = other.prefix_.group();
    if (laddr < raddr) return -1;
    if (laddr > raddr) return 1;

    return 0;
}

string InetMVpnRoute::ToString() const {
    return prefix_.ToString();
}

void InetMVpnRoute::SetKey(const DBRequestKey *reqkey) {
    const InetMVpnTable::RequestKey *key =
        static_cast<const InetMVpnTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void InetMVpnRoute::BuildProtoPrefix(BgpProtoPrefix *prefix,
                                     uint32_t router_id) const {
    prefix_.BuildProtoPrefix(0, prefix);
}

void InetMVpnRoute::BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
                                         IpAddress nexthop) const {
    nh.resize(4+RouteDistinguisher::kSize);
    const Ip4Address::bytes_type &addr_bytes = nexthop.to_v4().to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.end(),
              nh.begin()+RouteDistinguisher::kSize);
}

DBEntryBase::KeyPtr InetMVpnRoute::GetDBRequestKey() const {
    InetMVpnTable::RequestKey *key;
    key = new InetMVpnTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}
