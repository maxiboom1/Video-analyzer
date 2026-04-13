#include "Logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

std::vector<std::string> g_logs;

void AddLog(const std::string& text)
{
    g_logs.push_back(text);
    if (g_logs.size() > 500)
        g_logs.erase(g_logs.begin());
}

std::string CurrentTimestamp()
{
    auto sys_now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        sys_now.time_since_epoch()) % 1000;

    auto now_time = std::chrono::system_clock::to_time_t(sys_now);
    std::tm local_tm{};
    localtime_s(&local_tm, &now_time);

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%H:%M:%S")
        << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}
