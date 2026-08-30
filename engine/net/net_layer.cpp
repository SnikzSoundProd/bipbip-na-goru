#include "net/net_layer.h"
#include <cstdio>
#include <cstring>

namespace bip {

NetLayer* NetLayer::s_inst = nullptr;

bool NetLayer::host(uint16_t port) {
    s_inst = this;
    SteamDatagramErrMsg err;
    if (!GameNetworkingSockets_Init(nullptr, err)) {
        fprintf(stderr, "[net] GNS init failed: %s\n", err);
        return false;
    }
    inited_ = true;
    isHost_ = true;

    // Allow unauthenticated LAN/localhost connections (no Steam backend).
    SteamNetworkingUtils()->SetGlobalConfigValueInt32(
        k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
    // Required for raw IP (non-Steam) connections to function at all.
    SteamNetworkingUtils()->InitRelayNetworkAccess();

    // Host must register a GLOBAL connection-status callback, otherwise it
    // never learns about incoming connections. (Clients set a per-connection
    // callback in join().)
    SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(&NetLayer::cbConnState);

    SteamNetworkingIPAddr addr{};
    addr.m_port = port;
    listenSock_ = SteamNetworkingSockets()->CreateListenSocketIP(addr, 0, nullptr);
    pollGroup_ = SteamNetworkingSockets()->CreatePollGroup();
    if (listenSock_ == k_HSteamListenSocket_Invalid) {
        fprintf(stderr, "[net] listen socket failed\n");
        return false;
    }
    fprintf(stderr, "[net] hosting on :%u\n", (unsigned)port);
    return true;
}

bool NetLayer::join(const std::string& ip, uint16_t port) {
    s_inst = this;
    SteamDatagramErrMsg err;
    if (!GameNetworkingSockets_Init(nullptr, err)) {
        fprintf(stderr, "[net] GNS init failed: %s\n", err);
        return false;
    }
    inited_ = true;
    isHost_ = false;

    SteamNetworkingUtils()->SetGlobalConfigValueInt32(
        k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
    SteamNetworkingUtils()->InitRelayNetworkAccess();

    SteamNetworkingIPAddr addr{};
    addr.ParseString(ip.c_str());
    addr.m_port = port;
    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
               (void*)&NetLayer::cbConnState);
    conn_ = SteamNetworkingSockets()->ConnectByIPAddress(addr, 1, &opt);
    if (conn_ == k_HSteamNetConnection_Invalid) {
        fprintf(stderr, "[net] connect failed\n");
        return false;
    }
    fprintf(stderr, "[net] connecting to %s:%u...\n", ip.c_str(), (unsigned)port);
    return true;
}

void NetLayer::shutdown() {
    if (!inited_) return;
    if (conn_ != k_HSteamNetConnection_Invalid)
        SteamNetworkingSockets()->CloseConnection(conn_, 0, nullptr, false);
    if (listenSock_ != k_HSteamListenSocket_Invalid)
        SteamNetworkingSockets()->CloseListenSocket(listenSock_);
    if (pollGroup_ != k_HSteamNetPollGroup_Invalid)
        SteamNetworkingSockets()->DestroyPollGroup(pollGroup_);
    GameNetworkingSockets_Kill();
    inited_ = false;
    s_inst = nullptr;
}

void NetLayer::onConnState(HSteamNetConnection c, SteamNetConnectionStatusChangedCallback_t* info) {
    (void)c;
    auto& st = info->m_info;
    switch (info->m_info.m_eState) {
    case k_ESteamNetworkingConnectionState_Connecting:
        if (isHost_) {
            if (SteamNetworkingSockets()->AcceptConnection(info->m_hConn) != k_EResultOK) {
                SteamNetworkingSockets()->CloseConnection(info->m_hConn, 0, nullptr, false);
                break;
            }
            SteamNetworkingSockets()->SetConnectionPollGroup(info->m_hConn, pollGroup_);
            conn_ = info->m_hConn;   // single-peer MVP
            fprintf(stderr, "[net] client connected\n");
        }
        break;
    case k_ESteamNetworkingConnectionState_Connected:
        fprintf(stderr, "[net] connected!\n");
        break;
    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        fprintf(stderr, "[net] peer lost (%s)\n", st.m_szEndDebug);
        SteamNetworkingSockets()->CloseConnection(info->m_hConn, 0, nullptr, false);
        conn_ = k_HSteamNetConnection_Invalid;
        haveRemoteInput = haveRemoteSnapshot = false;
        break;
    default: break;
    }
}

void NetLayer::cbConnState(SteamNetConnectionStatusChangedCallback_t* info) {
    if (s_inst) s_inst->onConnState(info->m_hConn, info);
}

void NetLayer::pump(float dt) {
    if (!inited_) return;
    static uint64_t snapSent = 0, snapRecv = 0;
    static double rateT = 0;
    SteamNetworkingSockets()->RunCallbacks();

    // host: poll the listen socket for new connections handled via callback
    ISteamNetworkingSockets* ifc = SteamNetworkingSockets();

    // receive messages
    SteamNetworkingMessage_t* msgs[16];
    int n = ifc->ReceiveMessagesOnPollGroup(pollGroup_, msgs, 16);
    if (!isHost_) {
        // clients receive on their single connection
        n = ifc->ReceiveMessagesOnConnection(conn_, msgs, 16);
    }
    for (int i = 0; i < n; ++i) {
        auto* m = msgs[i];
        if (m->m_cbSize < 1) { m->Release(); continue; }
        ++packetsRecv;
        bytesRecv += (uint64_t)m->m_cbSize;
        uint8_t type = ((uint8_t*)m->m_pData)[0];
        const uint8_t* payload = (const uint8_t*)m->m_pData + 1;
        size_t avail = m->m_cbSize - 1;
        FILE* dbg = nullptr;
        if (type == (uint8_t)net::MsgType::Input && avail >= sizeof(net::InputPacket)) {
            memcpy(&remoteInput, payload, sizeof(net::InputPacket));
            haveRemoteInput = true;
            dbg = fopen("C:/Users/apex/AppData/Local/Temp/debug_net.txt", "a");
            if (dbg) { fprintf(dbg, "[recv] Input seq=%u\n", (unsigned)remoteInput.seq); fclose(dbg); }
        } else if (type == (uint8_t)net::MsgType::Snapshot && avail >= sizeof(net::PlayerSnapshot)) {
            memcpy(&remoteSnapshot, payload, sizeof(net::PlayerSnapshot));
            haveRemoteSnapshot = true;
            dbg = fopen("C:/Users/apex/AppData/Local/Temp/debug_net.txt", "a");
            if (dbg) { fprintf(dbg, "[recv] Snapshot px=%.1f py=%.1f pz=%.1f\n", remoteSnapshot.px, remoteSnapshot.py, remoteSnapshot.pz); fclose(dbg); }
        } else if (type == (uint8_t)net::MsgType::Welcome && avail >= sizeof(net::WelcomePacket)) {
            memcpy(&welcome, payload, sizeof(net::WelcomePacket));
            welcomeReceived = true;
        }
        m->Release();
    }

    // rolling 1-second rate computation
    rateT += (double)dt;
    if (rateT >= 1.0) {
        sentPerSec = (uint32_t)((packetsSent - snapSent) / rateT);
        recvPerSec = (uint32_t)((packetsRecv - snapRecv) / rateT);
        snapSent = packetsSent; snapRecv = packetsRecv; rateT = 0;
        // RTT via GNS ping (best-effort, 0 if unavailable)
        if (conn_ != k_HSteamNetConnection_Invalid) {
            SteamNetworkingSockets()->GetConnectionRealTimeStatus(conn_, nullptr, 0, nullptr);
        }
    }
}

static void sendTyped(NetLayer& self, HSteamNetConnection conn, uint8_t type,
                      const void* payload, size_t size) {
    if (conn == k_HSteamNetConnection_Invalid) return;
    uint8_t buf[sizeof(net::PlayerSnapshot) + 1]; // largest wire packet
    if (size > sizeof(net::PlayerSnapshot)) return;
    buf[0] = type;
    memcpy(buf + 1, payload, size);
    SteamNetworkingSockets()->SendMessageToConnection(conn, buf, (uint32_t)size + 1,
                                                      k_nSteamNetworkingSend_Unreliable, nullptr);
    ++self.packetsSent;
    self.bytesSent += (uint64_t)size + 1;
}

void NetLayer::sendInput(const net::InputPacket& in) {
    sendTyped(*this, conn_, (uint8_t)net::MsgType::Input, &in, sizeof(in));
}

void NetLayer::sendSnapshot(const net::PlayerSnapshot& s) {
    sendTyped(*this, conn_, (uint8_t)net::MsgType::Snapshot, &s, sizeof(s));
}

void NetLayer::sendWelcome(const net::WelcomePacket& w) {
    if (conn_ == k_HSteamNetConnection_Invalid) return;
    uint8_t buf[sizeof(net::WelcomePacket) + 1];
    buf[0] = (uint8_t)net::MsgType::Welcome;
    memcpy(buf + 1, &w, sizeof(w));
    SteamNetworkingSockets()->SendMessageToConnection(conn_, buf, sizeof(buf),
        k_nSteamNetworkingSend_Reliable, nullptr);
}

} // namespace bip
