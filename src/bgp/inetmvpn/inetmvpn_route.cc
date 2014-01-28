/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/parse_object.h"
#include "bgp/inetmvpn/inetmvpn_route.h"
#include "bgp/inetmvpn/inetmvpn_table.h"

using namespace std;

InetMVpnPrefix::InetMVpnPrefix() : type_(0), as_number_(0) {
}

InetMVpnPrefix::InetMVpnPrefix(const RouteDistinguisher &rd,
    const Ip4Address &group, const Ip4Address &source)
    : type_(0), rd_(rd), as_number_(0), group_(group), source_(source) {
}

InetMVpnPrefix::InetMVpnPrefix(const RouteDistinguisher &rd, as4_t as_number,
    const Ip4Address &group, const Ip4Address &source)
    : type_(7), rd_(rd), as_number_(as_number), group_(group), source_(source) {
}

InetMVpnPrefix::InetMVpnPrefix(const RouteDistinguisher &rd,
    const Ip4Address &router_id,
    const Ip4Address &group, const Ip4Address &source)
    : type_(8), rd_(rd), as_number_(0), router_id_(router_id),
      group_(group), source_(source) {
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
    size_t rd_size = RouteDistinguisher::kSize;
    size_t as_rtid_size = 4;

    type_ = prefix.type;
    if (type_ != 7 && type_ != 8) return;

    size_t rd_offset = 0;
    rd_ = RouteDistinguisher(&prefix.prefix[rd_offset]);

    size_t as_rtid_offset = rd_offset + rd_size;
    if (type_ == 7) {
        as_number_ = get_value(&prefix.prefix[as_rtid_offset], as_rtid_size);
    } else if (type_ == 8) {
        router_id_ =
            Ip4Address(get_value(&prefix.prefix[as_rtid_offset], as_rtid_size));
    }

    size_t source_offset = as_rtid_offset + as_rtid_size + 1;
    source_ = Ip4Address(get_value(&prefix.prefix[source_offset], 4));

    size_t group_offset = source_offset + 4 + 1;
    source_ = Ip4Address(get_value(&prefix.prefix[group_offset], 4));
}

void InetMVpnPrefix::BuildProtoPrefix(BgpProtoPrefix *prefix) const {
    size_t rd_size = RouteDistinguisher::kSize;
    size_t as_rtid_size = 4;

    assert(type_ == 7 || type_ == 8);
    prefix->prefixlen = (rd_size + as_rtid_size + 1 + 4 + 1 + 4) * 8;
    prefix->prefix.clear();
    prefix->type = type_;
    prefix->prefix.resize(prefix->prefixlen/8, 0);

    size_t rd_offset = 0;
    std::copy(rd_.GetData(), rd_.GetData() + rd_size,
              prefix->prefix.begin() + rd_offset);

    size_t as_rtid_offset = rd_offset + rd_size;
    if (type_ == 7) {
        put_value(&prefix->prefix[as_rtid_offset], as_rtid_size, as_number_);
    } else if (type_ == 8) {
        const Ip4Address::bytes_type &rtid_bytes = router_id_.to_bytes();
        std::copy(rtid_bytes.begin(), rtid_bytes.begin() + 4,
              prefix->prefix.begin() + as_rtid_offset);
    }

    size_t source_offset = as_rtid_offset + as_rtid_size + 1;
    prefix->prefix[source_offset - 1] = 32;
    const Ip4Address::bytes_type &source_bytes = source_.to_bytes();
    std::copy(source_bytes.begin(), source_bytes.begin() + 4,
              prefix->prefix.begin() + source_offset);

    size_t group_offset = source_offset + 4 + 1;
    prefix->prefix[group_offset - 1] = 32;
    const Ip4Address::bytes_type &group_bytes = group_.to_bytes();
    std::copy(group_bytes.begin(), group_bytes.begin() + 4,
              prefix->prefix.begin() + group_offset);
}

InetMVpnPrefix InetMVpnPrefix::FromString(const string &str,
    boost::system::error_code *errorp) {
    InetMVpnPrefix prefix;
    string temp_str;

    // Look for Type.
    size_t pos1 = str.find('-');
    if (pos1 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    temp_str = str.substr(0, pos1);
    stringToInteger(temp_str, prefix.type_);
    if (prefix.type_ != 0 && prefix.type_ != 7 && prefix.type_ != 8) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }

    // Look for RD.
    size_t pos2 = str.find('-', pos1 + 1);
    if (pos2 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    temp_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
    boost::system::error_code rd_err;
    prefix.rd_ = RouteDistinguisher::FromString(temp_str, &rd_err);
    if (rd_err != 0) {
        if (errorp != NULL) {
            *errorp = rd_err;
        }
        return prefix;
    }

    // Look for as/router-id.
    size_t pos3 = str.find(',', pos2 + 1);
    if (pos3 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    temp_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
    boost::system::error_code addr_err;
    Ip4Address addr = Ip4Address::from_string(temp_str, addr_err);
    if (addr_err == 0) {
        prefix.router_id_ = addr;
    } else {
        stringToInteger(temp_str, prefix.as_number_);
    }

    // Look for group.
    size_t pos4 = str.find(',', pos3 + 1);
    if (pos4 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return prefix;
    }
    temp_str = str.substr(pos3 + 1, pos4 - pos3 - 1);
    boost::system::error_code group_err;
    prefix.group_ = Ip4Address::from_string(temp_str, group_err);
    if (group_err != 0) {
        if (errorp != NULL) {
            *errorp = group_err;
        }
        return prefix;
    }

    // Rest is source.
    temp_str = str.substr(pos4 + 1, string::npos);
    boost::system::error_code source_err;
    prefix.source_ = Ip4Address::from_string(temp_str, source_err);
    if (source_err != 0) {
        if (errorp != NULL) {
            *errorp = source_err;
        }
        return prefix;
    }

    return prefix;
}

string InetMVpnPrefix::ToString() const {
    string repr = integerToString(type_);
    repr += "-" + rd_.ToString();
    if (type_ == 0) {
        repr += "-0";
    } else if (type_ == 7) {
        repr += "-" + integerToString(as_number_);
    } else if (type_ == 8) {
        repr += "-" + router_id_.to_string();
    }
    repr += "," + group_.to_string();
    repr += "," + source_.to_string();
    return repr;
}

string InetMVpnPrefix::ToXmppIdString() const {
    assert(type_ == 0);
    string repr = rd_.ToString();
    repr += ":" + group_.to_string();
    repr += "," + source_.to_string();
    return repr;
}

InetMVpnRoute::InetMVpnRoute(const InetMVpnPrefix &prefix) : prefix_(prefix) {
}

int InetMVpnRoute::CompareTo(const Route &rhs) const {
    const InetMVpnRoute &other = static_cast<const InetMVpnRoute &>(rhs);
    KEY_COMPARE(prefix_.type(), other.prefix_.type());
    KEY_COMPARE(prefix_.route_distinguisher(),
                other.prefix_.route_distinguisher());
    KEY_COMPARE(prefix_.as_number(), other.prefix_.as_number());
    KEY_COMPARE(prefix_.router_id(), other.prefix_.router_id());
    KEY_COMPARE(prefix_.source(), other.prefix_.source());
    KEY_COMPARE(prefix_.group(), other.prefix_.group());
    return 0;
}

string InetMVpnRoute::ToString() const {
    return prefix_.ToString();
}

string InetMVpnRoute::ToXmppIdString() const {
    return prefix_.ToXmppIdString();
}

void InetMVpnRoute::SetKey(const DBRequestKey *reqkey) {
    const InetMVpnTable::RequestKey *key =
        static_cast<const InetMVpnTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void InetMVpnRoute::BuildProtoPrefix(
    BgpProtoPrefix *prefix, uint32_t label) const {
    prefix_.BuildProtoPrefix(prefix);
}

void InetMVpnRoute::BuildBgpProtoNextHop(std::vector<uint8_t> &nh,
                                         IpAddress nexthop) const {
    nh.resize(4);
    const Ip4Address::bytes_type &addr_bytes = nexthop.to_v4().to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.end(), nh.begin());
}

DBEntryBase::KeyPtr InetMVpnRoute::GetDBRequestKey() const {
    InetMVpnTable::RequestKey *key;
    key = new InetMVpnTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}
