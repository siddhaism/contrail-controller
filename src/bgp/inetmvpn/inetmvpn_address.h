/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inetmvpn_address_h
#define ctrlplane_inetmvpn_address_h

#include <boost/system/error_code.hpp>

#include "bgp/bgp_attr_base.h"
#include "net/address.h"
#include "net/rd.h"

class InetMVpnPrefix {
public:
    InetMVpnPrefix();
    explicit InetMVpnPrefix(const BgpProtoPrefix &prefix);
    InetMVpnPrefix(const RouteDistinguisher &rd, const uint32_t router_id,
                   const Ip4Address &group, const Ip4Address &source);
    static InetMVpnPrefix FromString(const std::string &str,
                                     boost::system::error_code &ec);
    std::string ToString() const;

    RouteDistinguisher route_distinguisher() const { return rd_; }
    Ip4Address router_id() const { return router_id_; }
    Ip4Address group() const { return group_; }
    Ip4Address source() const { return source_; }
    void BuildProtoPrefix(uint32_t router_id, BgpProtoPrefix *prefix) const;

private:
    Ip4Address router_id_;
    RouteDistinguisher rd_;
    Ip4Address group_, source_;
};


#endif
