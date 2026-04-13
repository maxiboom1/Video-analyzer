#include "VizConnection.h"
#include "Logger.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

bool Viz_SendCommand(const std::string& command, AppState& state)
{
    // --- Init Winsock (safe to call multiple times) ---
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        state.lastVizOk  = false;
        state.lastVizMsg = "WSAStartup failed";
        AddLog(CurrentTimestamp() + " | VIZ ERROR: " + state.lastVizMsg);
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        state.lastVizOk  = false;
        state.lastVizMsg = "socket() failed: " + std::to_string(WSAGetLastError());
        AddLog(CurrentTimestamp() + " | VIZ ERROR: " + state.lastVizMsg);
        WSACleanup();
        return false;
    }

    // Set a 2-second connect/send timeout
    DWORD timeout = 2000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<u_short>(state.vizPort));

    if (inet_pton(AF_INET, state.vizIp, &addr.sin_addr) != 1)
    {
        state.lastVizOk  = false;
        state.lastVizMsg = "Invalid IP: " + std::string(state.vizIp);
        AddLog(CurrentTimestamp() + " | VIZ ERROR: " + state.lastVizMsg);
        closesocket(sock);
        WSACleanup();
        return false;
    }

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        state.lastVizOk  = false;
        state.lastVizMsg = "connect() failed (WSA " + std::to_string(WSAGetLastError()) + ")";
        AddLog(CurrentTimestamp() + " | VIZ ERROR: " + state.lastVizMsg);
        closesocket(sock);
        WSACleanup();
        return false;
    }

    // Viz expects command terminated with \0
    std::string payload = command;
    payload.push_back('\0');
    int sent = send(sock, payload.c_str(), (int)payload.size(), 0);
    if (sent == SOCKET_ERROR)
    {
        state.lastVizOk  = false;
        state.lastVizMsg = "send() failed (WSA " + std::to_string(WSAGetLastError()) + ")";
        AddLog(CurrentTimestamp() + " | VIZ ERROR: " + state.lastVizMsg);
        closesocket(sock);
        WSACleanup();
        return false;
    }

    closesocket(sock);
    WSACleanup();

    state.lastVizOk  = true;
    state.lastVizMsg = "OK";
    AddLog(CurrentTimestamp() + " | VIZ SENT: " + command);
    return true;
}

bool Viz_SendOn(AppState& state)
{
    return Viz_SendCommand(state.cmdOn, state);
}

bool Viz_SendOff(AppState& state)
{
    return Viz_SendCommand(state.cmdOff, state);
}
