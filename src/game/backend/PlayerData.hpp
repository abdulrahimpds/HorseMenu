#pragma once
#include "Detections.hpp"
#include "core/misc/RateLimiter.hpp"
#include "game/rdr/Player.hpp"
#include <unordered_set>
#include <chrono>

namespace YimMenu
{
	class PlayerData
	{
	public:
		std::unordered_set<Detection> m_Detections{};
		Player m_SpectatingPlayer{nullptr};
		bool m_UseSessionSplitKick{};
		bool m_BlockExplosions{};
		bool m_BlockParticles{};
		bool m_GhostMode{};

		RateLimiter m_VehicleFloodLimit{10s, 10};
		RateLimiter m_LargeVehicleFloodLimit{15s, 5};
		RateLimiter m_TickerMessageRateLimit{5s, 3};

		// Connection-level fuzzer attack protection
		int m_FuzzerAttackCount{0};                                    // Total attacks from this player
		bool m_ConnectionBlocked{false};                               // Is connection currently blocked?
		std::chrono::steady_clock::time_point m_BlockedUntil{};       // When to unblock the connection
		RateLimiter m_FuzzerAttackRateLimit{60s, 20};                 // 20 attacks per minute rate limit

		std::optional<std::uint64_t> m_PeerIdToSpoofTo{};
	};
}