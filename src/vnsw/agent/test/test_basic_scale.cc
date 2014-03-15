/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <pugixml/pugixml.hpp>

#include <net/bgp_af.h>
#include "io/test/event_manager_test.h"
#include "test_cmn_util.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"
#include "openstack/instance_service_server.h"

#include "xmpp_multicast_types.h"
#include "xml/xml_pugi.h"

#include "controller/controller_init.h"
#include "controller/controller_ifmap.h"
#include "controller/controller_peer.h" 

#define MAX_CHANNEL 10 
#define MAX_VN 200
#define MAX_VMS_PER_VN 100
#define MAX_CONTROL_PEER 2
#define MAX_REMOTE_ROUTES_PER_VN 100

using namespace pugi;
int num_vns = 1;
int num_vms_per_vn = 1;
int num_ctrl_peers = 1;
int num_remote_route = 1;

void WaitForIdle2(int wait_seconds = 30) {
    static const int kTimeoutUsecs = 1000;
    static int envWaitTime;

    if (!envWaitTime) {
        if (getenv("WAIT_FOR_IDLE")) {
            envWaitTime = atoi(getenv("WAIT_FOR_IDLE"));
        } else {
            envWaitTime = wait_seconds;
        }
    }

    if (envWaitTime > wait_seconds) wait_seconds = envWaitTime;

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    for (int i = 0; i < ((wait_seconds * 1000000)/kTimeoutUsecs); i++) {
        if (scheduler->IsEmpty()) {
            return;
        }
        usleep(kTimeoutUsecs);
    }
    EXPECT_TRUE(scheduler->IsEmpty());
}

xml_node MessageHeader(xml_document *xdoc, std::string vrf) {
    xml_node msg = xdoc->append_child("message");
    msg.append_attribute("type") = "set";
    msg.append_attribute("from") = XmppInit::kAgentNodeJID; 
    string str(XmppInit::kControlNodeJID);
    str += "/";
    str += XmppInit::kBgpPeer;
    msg.append_attribute("to") = str.c_str();

    xml_node event = msg.append_child("event");
    event.append_attribute("xmlns") = "http://jabber.org/protocol/pubsub";
    xml_node xitems = event.append_child("items");
    stringstream node;
    node << BgpAf::IPv4 << "/" << BgpAf::Unicast << "/" << vrf.c_str();
    xitems.append_attribute("node") = node.str().c_str();
    return(xitems);
}

xml_node L2MessageHeader(xml_document *xdoc, std::string vrf) {
    xml_node msg = xdoc->append_child("message");
    msg.append_attribute("type") = "set";
    msg.append_attribute("from") = XmppInit::kAgentNodeJID; 
    string str(XmppInit::kControlNodeJID);
    str += "/";
    str += XmppInit::kBgpPeer;
    msg.append_attribute("to") = str.c_str();

    xml_node event = msg.append_child("event");
    event.append_attribute("xmlns") = "http://jabber.org/protocol/pubsub";
    xml_node xitems = event.append_child("items");
    stringstream ss;
    ss << "25" << "/" << "242" << "/" << vrf.c_str();
    std::string node_str(ss.str());
    xitems.append_attribute("node") = node_str.c_str();
    return(xitems);
}

void RouterIdDepInit() {
}

Ip4Address IncrementIpAddress(Ip4Address base_addr) {
    return Ip4Address(base_addr.to_ulong() + 1);
}

void InitXmppServers() {
    Ip4Address addr = Ip4Address::from_string("127.0.0.0");

    for (int i = 0; i <= num_ctrl_peers; i++) {
        addr = IncrementIpAddress(addr);
        Agent::GetInstance()->SetXmppServer(addr.to_string(), i);
    }
}

class AgentBgpXmppPeerTest : public AgentXmppChannel {
public:
    AgentBgpXmppPeerTest(XmppChannel *channel, std::string xs, 
                         std::string lr, uint8_t xs_idx) :
        AgentXmppChannel(channel, xs, lr, xs_idx), rx_count_(0),
        rx_channel_event_queue_(
            TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"), 0,
            boost::bind(&AgentBgpXmppPeerTest::ProcessChannelEvent, this, _1)) {
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        rx_count_++;
        AgentXmppChannel::ReceiveUpdate(msg);
    }

    bool ProcessChannelEvent(xmps::PeerState state) {
        AgentXmppChannel::HandleXmppClientChannelEvent(
             static_cast<AgentXmppChannel *>(this), state);
        return true;
    }

    void HandleXmppChannelEvent(xmps::PeerState state) {
        rx_channel_event_queue_.Enqueue(state);
    }

    size_t Count() const { return rx_count_; }
    virtual ~AgentBgpXmppPeerTest() { }

private:
    size_t rx_count_;
    WorkQueue<xmps::PeerState> rx_channel_event_queue_;
};

class ControlNodeMockBgpXmppPeer {
public:
    ControlNodeMockBgpXmppPeer() : channel_ (NULL), rx_count_(0) {
    }

    void ReflectIpv4Route(string vrf_name, const pugi::xml_node &node, 
                          bool add_change) {
        autogen::ItemType item;
        item.Clear();

        //item.XmlParse(node);
        EXPECT_TRUE(item.XmlParse(node));
        EXPECT_TRUE(item.entry.nlri.af == BgpAf::IPv4);
        //Ip4Address addr = Ip4Address::from_string(item.entry.nlri.address);
        uint32_t label = 0;
        if (add_change) {
            EXPECT_TRUE(!item.entry.next_hops.next_hop.empty());
            //Assuming one interface NH has come
            SendRouteMessage(vrf_name, item.entry.nlri.address, 
                             item.entry.next_hops.next_hop[0].label);
        } else {
            SendRouteDeleteMessage(vrf_name, item.entry.nlri.address);
        }
    }

    void ReflectMulticast(string vrf_name, const pugi::xml_node &node, bool add_change) {
    }

    void ReflectEnetRoute(string vrf_name, const pugi::xml_node &node, bool add_change) {
        autogen::EnetItemType item;
        item.Clear();

        //item.XmlParse(node);
        EXPECT_TRUE(item.XmlParse(node));
        EXPECT_TRUE(item.entry.nlri.af == BgpAf::L2Vpn);
        //Ip4Address addr = Ip4Address::from_string(item.entry.nlri.address);
        uint32_t label = 0;
        if (add_change) {
            EXPECT_TRUE(!item.entry.next_hops.next_hop.empty());
            //Assuming one interface NH has come
            SendL2RouteMessage(vrf_name, item.entry.nlri.mac, 
                               item.entry.nlri.address, 
                               item.entry.next_hops.next_hop[0].label);
        } else {
            //SendL2RouteDeleteMessage(item.entry.nlri.mac, vrf_name, 
            //                         item.entry.nlri.address);
        }
    }

    void SendDocument(const pugi::xml_document &xdoc) {
        ostringstream oss;
        xdoc.save(oss);
        string msg = oss.str();
        uint8_t buf[4096];
        bzero(buf, sizeof(buf));
        memcpy(buf, msg.data(), msg.size());
        SendUpdate(buf, msg.size());
    }

    void SendRouteMessage(std::string vrf, std::string address, int label, 
                          const char *vn = "vn1") {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf);

        autogen::NextHopType item_nexthop;
        item_nexthop.af = BgpAf::IPv4;
        item_nexthop.address = Agent::GetInstance()->GetRouterId().to_string();;
        item_nexthop.label = label;

        autogen::ItemType item;
        item.entry.next_hops.next_hop.push_back(item_nexthop);
        item.entry.nlri.af = BgpAf::IPv4;
        item.entry.nlri.safi = BgpAf::Unicast;
        item.entry.nlri.address = address.c_str();
        item.entry.version = 1;
        item.entry.virtual_network = vn;

        xml_node node = xitems.append_child("item");
        node.append_attribute("id") = address.c_str();
        item.Encode(&node);

        SendDocument(xdoc);
    }

    void SendL2RouteMessage(std::string vrf, std::string mac_string, std::string address, int label, 
                            const char *vn = "vn1", bool is_vxlan = false) {
        xml_document xdoc;
        xml_node xitems = L2MessageHeader(&xdoc, vrf);

        autogen::EnetNextHopType item_nexthop;
        item_nexthop.af = 1;
        item_nexthop.address = Agent::GetInstance()->GetRouterId().to_string();
        item_nexthop.label = label;
        if (is_vxlan) {
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back("vxlan");
        } else {
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back("gre");
            item_nexthop.tunnel_encapsulation_list.tunnel_encapsulation.push_back("udp");
        }

        autogen::EnetItemType item;
        item.entry.nlri.af = 25;
        item.entry.nlri.safi = 242;
        item.entry.nlri.mac = mac_string.c_str();
        item.entry.nlri.address = address.c_str();

        item.entry.next_hops.next_hop.push_back(item_nexthop);

        xml_node node = xitems.append_child("item");
        stringstream ss;
        ss << mac_string.c_str() << "," << address.c_str(); 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();
        item.Encode(&node);

        SendDocument(xdoc);
    }

    void SendRouteDeleteMessage(std::string vrf, std::string address) {
        xml_document xdoc;
        xml_node xitems = MessageHeader(&xdoc, vrf);
        xml_node node = xitems.append_child("retract");
        stringstream ss;
        ss << address.c_str() << "/32"; 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();

        SendDocument(xdoc);
    }

    void SendL2RouteDeleteMessage(std::string mac_string, std::string vrf,
                                  std::string address) {
        xml_document xdoc;
        xml_node xitems = L2MessageHeader(&xdoc, vrf);
        xml_node node = xitems.append_child("retract");
        stringstream ss;
        ss << mac_string.c_str() << "," << address.c_str(); 
        string node_str(ss.str());
        node.append_attribute("id") = node_str.c_str();

        SendDocument(xdoc);
    }

    void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        rx_count_++;
        if (msg && msg->type == XmppStanza::IQ_STANZA) {
            const XmppStanza::XmppMessageIq *iq =
                static_cast<const XmppStanza::XmppMessageIq *>(msg);
            XmlPugi *pugi = NULL;
            std::string vrf_name = iq->node;
            if (iq->iq_type.compare("set") == 0) {
                if (iq->action.compare("subscribe") == 0) {
                    pugi = reinterpret_cast<XmlPugi *>(iq->dom.get());
                    xml_node options = pugi->FindNode("options");
                    for (xml_node node = options.first_child(); node;
                         node = node.next_sibling()) {
                        if (strcmp(node.name(), "instance-id") == 0) {
                            //TODO Some verification
                        }
                    }
                } else if (iq->action.compare("unsubscribe") == 0) {
                    //TODO unsubscribe came
                } else if (iq->action.compare("publish") == 0) {
                    XmlBase *impl = msg->dom.get();
                    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl);
                    for (xml_node item = pugi->FindNode("item"); item;
                         item = item.next_sibling()) {
                        if (strcmp(item.name(), "item") != 0) continue;
                        std::string id(iq->as_node.c_str());
                        char *str = const_cast<char *>(id.c_str());
                        char *saveptr;
                        char *af = strtok_r(str, "/", &saveptr);
                        char *safi = strtok_r(NULL, "/", &saveptr);

                        if (atoi(af) == BgpAf::IPv4 && atoi(safi) == BgpAf::Unicast) {
                            ReflectIpv4Route(iq->node, item, iq->is_as_node);
                        } else if (atoi(af) == BgpAf::IPv4 && atoi(safi) == BgpAf::Mcast) {
                            ReflectMulticast(iq->node, item, iq->is_as_node);
                        } else if (atoi(af) == BgpAf::L2Vpn && atoi(safi) == BgpAf::Enet) {
                            ReflectEnetRoute(iq->node, item, iq->is_as_node);
                        }
                    }
                }
            }
        }
    }    

    void HandleXmppChannelEvent(XmppChannel *channel,
                                xmps::PeerState state) {
        if (!channel_ && state == xmps::NOT_READY) {
            return;
        }
        if (state != xmps::READY) {
            assert(channel_ && channel == channel_);
            channel->UnRegisterReceive(xmps::BGP);
            channel_ = NULL;
        } else {
            if (channel_) {
                assert(channel == channel_);
            }
            //assert(channel_ && channel == channel_);
            channel->RegisterReceive(xmps::BGP,
                    boost::bind(&ControlNodeMockBgpXmppPeer::ReceiveUpdate,
                                this, _1));
            channel_ = channel;
        }
    }

    bool SendUpdate(uint8_t *msg, size_t size) {
        if (channel_ && 
            (channel_->GetPeerState() == xmps::READY)) {
            return channel_->Send(msg, size, xmps::BGP,
                   boost::bind(&ControlNodeMockBgpXmppPeer::WriteReadyCb, this, _1));
        }
        return false;
    }

    void WriteReadyCb(const boost::system::error_code &ec) {
    }

    size_t Count() const { return rx_count_; }
    virtual ~ControlNodeMockBgpXmppPeer() {
    }
private:
    XmppChannel *channel_;
    size_t rx_count_;
};


class AgentBasicScaleTest : public ::testing::Test { 
protected:
    AgentBasicScaleTest() : thread_(&evm_), agent_(Agent::GetInstance())  {}
 
    virtual void SetUp() {
        AgentIfMapVmExport::Init();
        for (int i = 0; i < num_ctrl_peers; i++) {
            xs[i] = new XmppServer(&evm_, XmppInit::kControlNodeJID);
            xc[i] = new XmppClient(&evm_);

            xs[i]->Initialize(0, false);
        }
        
        thread_.Start();
    }

    virtual void TearDown() {
        AgentIfMapVmExport::Shutdown();
        for (int i = 0; i < num_ctrl_peers; i++) {
            xc[i]->ConfigUpdate(new XmppConfigData());
            client->WaitForIdle(5);
            xs[i]->Shutdown();
            bgp_peer[i].reset(); 
            client->WaitForIdle();
            xc[i]->Shutdown();
            client->WaitForIdle();
            TcpServerManager::DeleteServer(xs[i]);
            TcpServerManager::DeleteServer(xc[i]);
        }
        evm_.Shutdown();
        thread_.Join();
        client->WaitForIdle();
    }

    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                            const string &from, const string &to,
                                            bool isclient) {
        XmppChannelConfig *cfg = new XmppChannelConfig(isclient);
        cfg->endpoint.address(boost::asio::ip::address::from_string(address));
        cfg->endpoint.port(port);
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        return cfg;
    }


    void VerifyConnections(int i) {
        XmppServer *xs_l = xs[i];
        XmppClient *xc_l = xc[i];

        XmppConnection *sconnection_l = sconnection[i];

        WAIT_FOR(100, 10000,
                 ((sconnection_l = xs_l->FindConnection(XmppInit::kAgentNodeJID)) 
                  != NULL));
        assert(sconnection_l);
        //wait for connection establishment
        WAIT_FOR(100, 10000, (sconnection_l->GetStateMcState() == xmsm::ESTABLISHED));

        XmppChannel *cchannel_l = cchannel[i];;
        WAIT_FOR(100, 10000, (cchannel_l->GetPeerState() == xmps::READY));
        client->WaitForIdle();
        //expect subscribe for __default__ at the mock server

        ControlNodeMockBgpXmppPeer *mock_peer_l = mock_peer[i].get();
        WAIT_FOR(100, 10000, (mock_peer_l->Count() == 1));
    }

    void BuildControlPeers() {
        Ip4Address addr = Ip4Address::from_string("127.0.0.0");
        for (int i = 0; i < num_ctrl_peers; i++) {
            addr = IncrementIpAddress(addr);
            //Create control node mock
            mock_peer[i].reset(new ControlNodeMockBgpXmppPeer());
            xs[i]->RegisterConnectionEvent(xmps::BGP,
                   boost::bind(&ControlNodeMockBgpXmppPeer::HandleXmppChannelEvent,
                               mock_peer[i].get(), _1, _2));                        
            LOG(DEBUG, "Create xmpp agent clien - t" << i);
            //New config data for this channel and peer
            xmppc_cfg[i] = new XmppConfigData;
            xmppc_cfg[i]->AddXmppChannelConfig(CreateXmppChannelCfg(addr.to_string().c_str(),
                                               xs[i]->GetPort(),
                                               XmppInit::kAgentNodeJID, 
                                               XmppInit::kControlNodeJID, true));
            xc[i]->ConfigUpdate(xmppc_cfg[i]);
            cchannel[i] = xc[i]->FindChannel(XmppInit::kControlNodeJID);
            //New bgp peer from agent
            bgp_peer[i].reset(new AgentBgpXmppPeerTest(cchannel[i],
                                  Agent::GetInstance()->GetXmppServer(i),
                                  Agent::GetInstance()->GetAgentMcastLabelRange(i), i));
            xc[i]->RegisterConnectionEvent(xmps::BGP,
                           boost::bind(&AgentBgpXmppPeerTest::HandleXmppChannelEvent, 
                                       bgp_peer[i].get(), _2));
            Agent::GetInstance()->SetAgentXmppChannel(bgp_peer[i].get(), i);

            // server connection
            VerifyConnections(i);
        }
    }

    void BuildVmPortEnvironment() {
        //TODO take vxlan state from command line, keep default as false
        VxLanNetworkIdentifierMode(false);
        client->WaitForIdle();

        uint32_t num_entries = (num_vns * num_vms_per_vn);
        //Build the VM port list
        input = (struct PortInfo *)malloc(sizeof(struct PortInfo) * num_entries);
        int intf_id = 1;
        for (int vn_cnt = 1; vn_cnt <= num_vns; vn_cnt++) {
            stringstream ip_addr;
            ip_addr << vn_cnt << ".1.1.0";
            Ip4Address addr = 
                IncrementIpAddress(Ip4Address::from_string(ip_addr.str()));
            for (int vm_cnt = 0; vm_cnt < num_vms_per_vn; vm_cnt++) {
                stringstream name;
                stringstream mac;
                int cnt = intf_id - 1;
                name << "vnet" << intf_id;
                mac << "00:00:00:00:" << std::hex << vn_cnt << ":" << 
                    std::hex << (vm_cnt + 1);
                memcpy(&(input[cnt].name), name.str().c_str(), 32);
                input[cnt].intf_id = intf_id;
                memcpy(&(input[cnt].addr), addr.to_string().c_str(), 32);
                memcpy(&(input[cnt].mac), mac.str().c_str(), 32);
                input[cnt].vn_id = vn_cnt;
                input[cnt].vm_id = intf_id;
                intf_id++;
                addr = IncrementIpAddress(addr);
            }
        }
        //Create vn,vrf,vm,vm-port and route entry in vrf1 
        CreateVmportEnv(input, (intf_id - 1));
        client->WaitForIdle();

        /*
struct PortInfo {
    char name[32];
    uint8_t intf_id;
    char addr[32];
    char mac[32];
    int vn_id;
    int vm_id;
};
        */
    }

    void VerifyVmPortActive(bool active) {
        for (int i = 0; i < (num_vns * num_vms_per_vn); i++) {
            if (active) {
                EXPECT_TRUE(VmPortActive(input, i));
            } else {
                EXPECT_FALSE(VmPortActive(input, i));
            }
        }
    }

    void XmppConnectionSetUp() {
        Agent::GetInstance()->SetControlNodeMulticastBuilder(NULL);
        BuildControlPeers();
    }

    EventManager evm_;
    ServerThread thread_;

    XmppConfigData *xmpps_cfg[MAX_CHANNEL]; //Not in use currently
    XmppConfigData *xmppc_cfg[MAX_CHANNEL];

    XmppServer *xs[MAX_CHANNEL];
    XmppClient *xc[MAX_CHANNEL];

    XmppConnection *sconnection[MAX_CHANNEL];
    XmppChannel *cchannel[MAX_CHANNEL];

    auto_ptr<AgentBgpXmppPeerTest> bgp_peer[MAX_CHANNEL];
    auto_ptr<ControlNodeMockBgpXmppPeer> mock_peer[MAX_CHANNEL];
    Agent *agent_;
    struct PortInfo *input;
};

TEST_F(AgentBasicScaleTest, Basic) {
    client->Reset();
    client->WaitForIdle();

    XmppConnectionSetUp();
    BuildVmPortEnvironment();

    //expect subscribe message+route at the mock server
    WAIT_FOR(100, 10000, (mock_peer[0].get()->Count() == 
                          (6 * num_vns * num_vms_per_vn)));
    client->WaitForIdle();

    VerifyVmPortActive(true);
    //Delete vm-port and route entry in vrf1
    DeleteVmportEnv(input, (num_vns * num_vms_per_vn), true);
    WAIT_FOR(1000, 1000, (Agent::GetInstance()->GetVnTable()->Size() == 0));
    client->WaitForIdle();
}

#define GETSCALEARGS()                          \
    bool ksync_init = false;                    \
    char init_file[1024];                       \
    memset(init_file, '\0', sizeof(init_file)); \
    ::testing::InitGoogleTest(&argc, argv);     \
    namespace opt = boost::program_options;     \
    opt::options_description desc("Options");   \
    opt::variables_map vm;                      \
    desc.add_options()                          \
        ("help", "Print help message")          \
        ("config", opt::value<string>(), "Specify Init config file")  \
        ("kernel", "Run with vrouter")         \
        ("vn", opt::value<int>(), "Number of VN")                   \
        ("vm", opt::value<int>(), "Number of VM per VN")            \
        ("control", opt::value<int>(), "Number of control peer")     \
        ("remote_route", opt::value<int>(), "Number of remote route");\
    opt::store(opt::parse_command_line(argc, argv, desc), vm); \
    opt::notify(vm);                            \
    if (vm.count("help")) {                     \
        cout << "Test Help" << endl << desc << endl; \
        exit(0);                                \
    }                                           \
    if (vm.count("kernel")) {                   \
        ksync_init = true;                      \
    }                                           \
    if (vm.count("vn")) {                      \
        num_vns = vm["vn"].as<int>();          \
    }                                           \
    if (vm.count("vm")) {                      \
        num_vms_per_vn = vm["vm"].as<int>();   \
    }                                           \
    if (vm.count("control")) {                      \
        num_ctrl_peers = vm["control"].as<int>();   \
    }                                           \
    if (vm.count("remote_route")) {                      \
        num_remote_route = vm["remote_route"].as<int>();   \
    }                                           \
    if (vm.count("config")) {                   \
        strncpy(init_file, vm["config"].as<string>().c_str(), (sizeof(init_file) - 1) ); \
    } else {                                    \
        strcpy(init_file, DEFAULT_VNSW_CONFIG_FILE); \
    }                                           \


int main(int argc, char **argv) {
    GETSCALEARGS();
    if (num_vns > MAX_VN) {
        LOG(DEBUG, "Max VN is 254");
        return false;
    }
    if (num_vms_per_vn > MAX_VMS_PER_VN) {
        LOG(DEBUG, "Max VM per VN is 100");
        return false;
    }
    if (num_ctrl_peers == 0 || num_ctrl_peers > MAX_CONTROL_PEER) {
        LOG(DEBUG, "Supported values - 1, 2");
        return false;
    }
    if (num_vms_per_vn > MAX_REMOTE_ROUTES_PER_VN) {
        LOG(DEBUG, "Max remote route per vn is 100");
        return false;
    }

    client = TestInit(init_file, ksync_init);
    InitXmppServers();

    int ret = RUN_ALL_TESTS();
    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();
    return ret;
}
