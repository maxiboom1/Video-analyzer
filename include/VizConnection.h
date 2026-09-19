#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct VizRequest
{
    std::string ip;
    int port = 6100;
    std::string command;
};

struct VizResult
{
    bool success = false;
    std::string message;
};

struct VizSocketOps;
// Sends command + NUL with one two-second deadline. Does not access UI state.
VizResult Viz_SendRequest(const VizRequest& request, const std::atomic_bool& cancelled,
    const VizSocketOps* socketOps = nullptr);

class VizSender
{
public:
    using Transport = std::function<VizResult(const VizRequest&, const std::atomic_bool&)>;
    explicit VizSender(Transport transport = {});
    ~VizSender();
    bool Submit(VizRequest request);
    bool Poll(VizResult& result);
    void Stop();

private:
    void Run();
    Transport transport_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic_bool stopping_{ false };
    bool busy_ = false;
    std::optional<VizRequest> request_;
    std::optional<VizResult> result_;
    std::thread worker_;
};
