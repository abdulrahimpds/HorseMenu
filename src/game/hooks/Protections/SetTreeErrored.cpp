#pragma once
#include "game/hooks/Hooks.hpp"
#include "core/hooking/DetourHook.hpp"
#include "game/backend/Protections.hpp"
#include "core/frontend/Notifications.hpp"
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace YimMenu::Hooks
{
	// rate-limit spam and still forward to the original to keep engine state consistent
	static std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_last_log_time;
	static std::unordered_map<std::string, int> g_error_counts;
	static std::mutex g_log_mutex;

	void Protections::SetTreeErrored(rage::netSyncTree* tree, bool errored)
	{
		if (errored)
		{
			std::lock_guard<std::mutex> _(g_log_mutex);
			auto now = std::chrono::steady_clock::now();
			constexpr const char* key = "sync_tree_error";
			auto it = g_last_log_time.find(key);
			if (it != g_last_log_time.end())
			{
				auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
				if (secs < 5)
				{
					g_error_counts[key]++;
					// still forward to original to avoid retry loops
					BaseHook::Get<Protections::SetTreeErrored, DetourHook<decltype(&Protections::SetTreeErrored)>>()->Original()(tree, errored);
					return;
				}
			}

			int count = g_error_counts[key];
			if (count > 0)
			{
				LOGF(SYNC, WARNING, "rage::netSyncTree::SetErrored called (x{} times in last 5s)", count + 1);
			}
			else
			{
				LOGF(SYNC, WARNING, "rage::netSyncTree::SetErrored called");
			}
			g_last_log_time[key] = now;
			g_error_counts[key] = 0;
		}

		// always forward to original so the engine actually marks/clears the error flag
		BaseHook::Get<Protections::SetTreeErrored, DetourHook<decltype(&Protections::SetTreeErrored)>>()->Original()(tree, errored);
	}
}