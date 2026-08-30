#pragma once
// bipbip net: GNS wrapper — host (listen server) or client, 20Hz send loop.
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include "net/protocol.h"
#include <string>

namespace bip {

class NetLayer {
public:
    bool host(uint16_t port);
    bool join(const std::string& ip, uint16_t port);
    void shutdown();

    // call every frame: pumps callbacks, updates rolling rates
    void pump(float dt = 0.f);

    // client: queue input (sent at 20Hz); host: applies via callback below
    void sendInput(const net::InputPacket& in);

    // host: send snapshot to the connected peer; client: sends to server
    void sendSnapshot(const net::PlayerSnapshot& snap);
    void sendWelcome(const net::WelcomePacket& w);

    // last received data (valid after pump)
    bool connected() const { return conn_ != k_HSteamNetConnection_Invalid; }
    bool haveRemoteInput = false;
    net::InputPacket remoteInput{};
    bool haveRemoteSnapshot = false;
    net::PlayerSnapshot remoteSnapshot{};
    bool welcomeReceived = false;
    net::WelcomePacket welcome{};

    // network statistics (real, incremented on every send/recv)
    uint64_t packetsSent = 0;
    uint64_t packetsRecv = 0;
    uint64_t bytesSent = 0;
    uint64_t bytesRecv = 0;
    // rolling 1-second rates (updated by pump())
    uint32_t sentPerSec = 0;
    uint32_t recvPerSec = 0;
    float rttMs = 0.f;

private:
    static NetLayer* s_inst;
    void onConnState(HSteamNetConnection conn, SteamNetConnectionStatusChangedCallback_t* info);

    static void cbConnState(SteamNetConnectionStatusChangedCallback_t* info);

    HSteamListenSocket listenSock_ = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup pollGroup_ = k_HSteamNetPollGroup_Invalid;
    HSteamNetConnection conn_ = k_HSteamNetConnection_Invalid;
    bool isHost_ = false;
    bool inited_ = false;
};

} // namespace bip
