#pragma once
#include <string>
#include <vector>

void AddLog(const std::string& text);
std::string CurrentTimestamp();

extern std::vector<std::string> g_logs;
