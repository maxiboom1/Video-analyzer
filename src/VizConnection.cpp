#include "VizSocketOps.h"
#include <ws2tcpip.h>
#include "VizConnection.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <exception>
#include <utility>

#pragma comment(lib, "ws2_32.lib")

namespace
{
    using Clock = std::chrono::steady_clock;
    struct WinsockSession
    {
        WSADATA data{};
        int error = WSAStartup(MAKEWORD(2, 2), &data);
        ~WinsockSession() { if (!error) WSACleanup(); }
    };
    struct Socket
    {
        SOCKET value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
    };
    std::string SocketError(const char* operation, int error = WSAGetLastError())
    {
        return std::string(operation) + " failed (WSA " + std::to_string(error) + ")";
    }

    bool CanContinue(const std::atomic_bool& cancelled, Clock::time_point deadline, std::string& error)
    {
        if (cancelled.load()) error = "Send cancelled";
        else if (Clock::now() >= deadline) error = "Connection/send timed out after 2 seconds";
        else return true;
        return false;
    }

    bool WaitWritable(SOCKET socket, const VizSocketOps& ops, const std::atomic_bool& cancelled,
        Clock::time_point deadline, std::string& error)
    {
        while (CanContinue(cancelled, deadline, error))
        {
            fd_set writable, failed;
            FD_ZERO(&writable);
            FD_ZERO(&failed);
            FD_SET(socket, &writable);
            FD_SET(socket, &failed);
            const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count();
            timeval wait{ 0, static_cast<long>(std::clamp<long long>(remaining, 0, 50000)) };
            const int ready = ops.select(0, nullptr, &writable, &failed, &wait);
            if (ready == SOCKET_ERROR)
            {
                error = SocketError("select()");
                return false;
            }
            if (!ready) continue;
            int socketError = 0;
            int size = sizeof(socketError);
            if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &size) == SOCKET_ERROR)
            {
                error = SocketError("getsockopt()");
                return false;
            }
            if (socketError)
            {
                error = SocketError("Connection/send", socketError);
                return false;
            }
            return CanContinue(cancelled, deadline, error);
        }
        return false;
    }
}

VizResult Viz_SendRequest(const VizRequest& request, const std::atomic_bool& cancelled, const VizSocketOps* socketOps)
{
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    const VizSocketOps defaults;
    const auto& ops = socketOps ? *socketOps : defaults;
    std::string error;
    if (!CanContinue(cancelled, deadline, error)) return { false, error };
    if (request.port < 1 || request.port > 65535) return { false, "Invalid port" };
    WinsockSession winsock;
    if (winsock.error) return { false, SocketError("WSAStartup()", winsock.error) };
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(request.port));
    if (inet_pton(AF_INET, request.ip.c_str(), &address.sin_addr) != 1)
        return { false, "Invalid IP: " + request.ip };
    Socket socket;
    if (socket.value == INVALID_SOCKET) return { false, SocketError("socket()") };
    u_long nonblocking = 1;
    if (ioctlsocket(socket.value, FIONBIO, &nonblocking) == SOCKET_ERROR)
        return { false, SocketError("ioctlsocket()") };

    if (ops.connect(socket.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        const int connectError = WSAGetLastError();
        if (connectError != WSAEWOULDBLOCK)
            return { false, SocketError("connect()", connectError) };
        if (!WaitWritable(socket.value, ops, cancelled, deadline, error)) return { false, error };
    }

    std::string payload = request.command;
    payload.push_back('\0');
    size_t totalSent = 0;
    while (totalSent < payload.size())
    {
        if (!CanContinue(cancelled, deadline, error)) return { false, error };
        const int size = static_cast<int>(std::min<size_t>(payload.size() - totalSent, INT_MAX));
        const int sent = ops.send(socket.value, payload.data() + totalSent, size, 0);
        if (sent == SOCKET_ERROR)
        {
            const int sendError = WSAGetLastError();
            if (sendError != WSAEWOULDBLOCK) return { false, SocketError("send()", sendError) };
            if (!WaitWritable(socket.value, ops, cancelled, deadline, error)) return { false, error };
        }
        else if (sent == 0) return { false, "Connection closed before command was sent" };
        else totalSent += static_cast<size_t>(sent);
    }
    return { true, "Last send succeeded" };
}

VizSender::VizSender(Transport transport)
    : transport_(transport ? std::move(transport) : Transport([](const VizRequest& request, const std::atomic_bool& cancelled) {
        return Viz_SendRequest(request, cancelled);
    })), worker_(&VizSender::Run, this)
{}

VizSender::~VizSender() { Stop(); }

bool VizSender::Submit(VizRequest request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || busy_) return false;
    request_ = std::move(request);
    busy_ = true;
    wake_.notify_one();
    return true;
}

bool VizSender::Poll(VizResult& result)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!result_) return false;
    result = std::move(*result_);
    result_.reset();
    busy_ = false;
    return true;
}

void VizSender::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    request_.reset();
    result_.reset();
    busy_ = false;
}

void VizSender::Run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (true)
    {
        wake_.wait(lock, [this] { return stopping_ || request_.has_value(); });
        if (stopping_) return;
        auto request = std::move(*request_);
        request_.reset();
        lock.unlock();
        VizResult result;
        try { result = transport_(request, stopping_); }
        catch (const std::exception& e) { result = { false, std::string("Send failed: ") + e.what() }; }
        catch (...) { result = { false, "Unexpected send failure" }; }
        lock.lock();
        if (stopping_) return;
        result_ = std::move(result);
    }
}
