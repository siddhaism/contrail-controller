/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_inetmvpn_route_h
#define ctrlplane_inetmvpn_route_h

#include <set>

#include "base/util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_route.h"
#include "bgp/inetmvpn/inetmvpn_address.h"
#include "net/address.h"
#include "net/bgp_af.h"
#include "route/route.h"

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
                                  uint32_t router_id) const;
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
