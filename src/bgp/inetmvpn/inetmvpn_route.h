/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inetmvpn_route_h
#define ctrlplane_inetmvpn_route_h

#include <set>

#include <boost/system/error_code.hpp>

#include "base/util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_route.h"
#include "net/address.h"
#include "net/bgp_af.h"
#include "net/rd.h"

class InetMVpnPrefix {
public:
    InetMVpnPrefix();
    explicit InetMVpnPrefix(const BgpProtoPrefix &prefix);
    InetMVpnPrefix(const RouteDistinguisher &rd,
                   const Ip4Address &group, const Ip4Address &source);
    InetMVpnPrefix(const RouteDistinguisher &rd, as4_t as_number,
                   const Ip4Address &group, const Ip4Address &source);
    InetMVpnPrefix(const RouteDistinguisher &rd, const Ip4Address &router_id,
                   const Ip4Address &group, const Ip4Address &source);
    static InetMVpnPrefix FromString(const std::string &str,
                                     boost::system::error_code *errorp = NULL);
    std::string ToString() const;

    uint8_t type() const { return type_; }
    RouteDistinguisher route_distinguisher() const { return rd_; }
    as4_t as_number() const { return as_number_; }
    Ip4Address router_id() const { return router_id_; }
    Ip4Address group() const { return group_; }
    Ip4Address source() const { return source_; }
    void BuildProtoPrefix(BgpProtoPrefix *prefix) const;

private:
    uint8_t type_;
    RouteDistinguisher rd_;
    as4_t as_number_;
    Ip4Address router_id_;
    Ip4Address group_;
    Ip4Address source_;
};

class InetMVpnRoute : public BgpRoute {
public:
    explicit InetMVpnRoute(const InetMVpnPrefix &prefix);
    virtual int CompareTo(const Route &rhs) const;

    virtual std::string ToString() const;

    const InetMVpnPrefix &GetPrefix() const {
        return prefix_;
    }

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *reqkey);
    virtual void BuildProtoPrefix(BgpProtoPrefix *prefix,
                                  uint32_t label) const;
    virtual void BuildBgpProtoNextHop(std::vector<uint8_t> &nh, 
                                      IpAddress nexthop) const;

    virtual bool IsLess(const DBEntry &genrhs) const {
        const InetMVpnRoute &rhs = static_cast<const InetMVpnRoute &>(genrhs);
        int cmp = CompareTo(rhs);
        return (cmp < 0);
    }

    virtual u_int16_t Afi() const { return BgpAf::IPv4; }
    virtual u_int8_t Safi() const { return BgpAf::McastVpn; }

private:
    InetMVpnPrefix prefix_;
    DISALLOW_COPY_AND_ASSIGN(InetMVpnRoute);
};

#endif
