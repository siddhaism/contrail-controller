/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inetmvpn/inetmvpn_address.h"

#include "bgp/inet/inet_route.h"

using namespace std;

InetMVpnPrefix::InetMVpnPrefix() {
}

InetMVpnPrefix::InetMVpnPrefix(const RouteDistinguisher &rd, uint32_t router_id,
            const Ip4Address &group, const Ip4Address &source)
        : router_id_(router_id), rd_(rd), group_(group), source_(source) {
}

// INET-MCAST-VPN Advertisement Route Format
//
// +------------------------------------+
// |      RD   (8 octets)               |
// +------------------------------------+
// |  Router-Id (4 octets)              |
// +------------------------------------+
// |  Multicast Source length (1 octet) |
// +------------------------------------+
// |  Multicast Source (4)              |
// +------------------------------------+
// |  Multicast Group length (1 octet)  |
// +------------------------------------+
// |  Multicast Group (4)               |
// +------------------------------------+
InetMVpnPrefix::InetMVpnPrefix(const BgpProtoPrefix &prefix) {
    size_t rd_offset = 0;
    size_t router_id_offset = 4;
    size_t source_offset = 13;
    size_t group_offset = 18;

    rd_ = RouteDistinguisher(&prefix.prefix[rd_offset]);
    router_id_ = Ip4Address(get_value(&prefix.prefix[router_id_offset], 4));
    source_ = Ip4Address(get_value(&prefix.prefix[source_offset], 4));
    group_ = Ip4Address(get_value(&prefix.prefix[group_offset], 4));
}

void InetMVpnPrefix::BuildProtoPrefix(uint32_t router_id,
                                      BgpProtoPrefix *prefix) const {
    size_t rd_size = RouteDistinguisher::kSize;
    size_t rd_offset = 0;
    size_t source_offset = 13;
    size_t group_offset = 18;

    prefix->prefixlen = (rd_size + 4 + 1 + 4 + 1 + 4) * 8;
    prefix->prefix.clear();
    prefix->type = 8;
    prefix->prefix.resize(prefix->prefixlen/8, 0);

#if 0
    size_t router_id_offset = 4;
    uint8_t len = 32; // Length in bits of source/group IPv4 address.
    const Ip4Address::bytes_type &addr_bytes = source_.to_bytes();
    std::copy(&router_id, &router_id + 1,
              prefix->prefix.begin() + router_id_offset, 4);
    std::copy(&len, &len + 1, prefix->prefix.begin() + source_offset - 1);
    std::copy(&len, &len + 1, prefix->prefix.begin() + group_offset - 1);
#endif
    std::copy(rd_.GetData(), rd_.GetData() + rd_size,
              prefix->prefix.begin() + rd_offset);
    const Ip4Address::bytes_type &source_bytes = source_.to_bytes();
    std::copy(source_bytes.begin(), source_bytes.begin() + 4,
              prefix->prefix.begin() + source_offset);

    const Ip4Address::bytes_type &group_bytes = group_.to_bytes();
    std::copy(group_bytes.begin(), group_bytes.begin() + 4,
              prefix->prefix.begin() + group_offset);
}

// Format is RD,Router-ID,Group,Source
InetMVpnPrefix InetMVpnPrefix::FromString(const string &str,
                                          boost::system::error_code &ec) {
    InetMVpnPrefix prefix;
    string addr_str;

    // Look for RD.
    size_t pos1 = str.find('-');
    if (pos1 == string::npos) {
        ec = make_error_code(boost::system::errc::invalid_argument);
        return prefix;
    }
    addr_str = str.substr(0, pos1);
    prefix.rd_ = RouteDistinguisher::FromString(addr_str, &ec);
    if (ec != 0) return prefix;

    // Look for router-id.
    size_t pos2 = str.find(',', pos1 + 1);
    if (pos2 == string::npos) {
        ec = make_error_code(boost::system::errc::invalid_argument);
        return prefix;
    }

    addr_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
    prefix.router_id_ = Ip4Address::from_string(addr_str, ec);
    if (ec != 0) return prefix;

    // Look for group.
    size_t pos3 = str.find(',', pos2 + 1);
    if (pos3 == string::npos) {
        ec = make_error_code(boost::system::errc::invalid_argument);
        return prefix;
    }
    addr_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
    prefix.group_ = Ip4Address::from_string(addr_str, ec);
    if (ec != 0) return prefix;

    // Rest is source.
    addr_str = str.substr(pos3 + 1, string::npos);
    prefix.source_ = Ip4Address::from_string(addr_str, ec);
    if (ec != 0) return prefix;
    
    return prefix;
}

string InetMVpnPrefix::ToString() const {
    string repr = rd_.ToString();
    repr += "-" + router_id_.to_string();
    repr += "," + group_.to_string();
    repr += "," + source_.to_string();
    return repr;
}
