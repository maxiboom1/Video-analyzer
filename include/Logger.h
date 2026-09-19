#pragma once
#include <string>
#include <vector>

void AddLog(const std::string& text);
std::string CurrentTimestamp();

std::vector<std::string> Logger_Snapshot();
void Logger_Clear();
