#pragma once
#include "Storage/Spoofing.hpp"
#include "common.hpp"
#include "core/frontend/Notifications.hpp"
#include "game/backend/ScriptMgr.hpp"
#include "game/pointers/Pointers.hpp"
#include "game/rdr/Entity.hpp"
#include "game/rdr/Vehicle.hpp"
#include "game/rdr/Natives.hpp"
#include "game/rdr/Player.hpp"
#include "util/Joaat.hpp"

#include <entity/CDynamicEntity.hpp>
#include <network/CNetObjectMgr.hpp>
#include <network/netObject.hpp>
#include <cmath>

#include "game/backend/CrashSignatures.hpp"


// TODO: remove this file!!!

namespace YimMenu::Teleport
{
	inline bool LoadGroundAtCoords(rage::fvector3& location)
	{
		constexpr float max_ground_check = 1000.f;
		constexpr int max_attempts       = 300;
		float ground_z                   = location.z;
		int current_attempts             = 0;
		bool found_ground;
		float height;

		do
		{
			found_ground = MISC::GET_GROUND_Z_FOR_3D_COORD(location.x, location.y, max_ground_check, &ground_z, FALSE);
			STREAMING::REQUEST_COLLISION_AT_COORD(location.x, location.y, location.z);

			if (current_attempts % 10 == 0)
			{
				location.z += 25.f;
			}

			++current_attempts;

			ScriptMgr::Yield();
		} while (!found_ground && current_attempts < max_attempts);

		if (!found_ground)
		{
			return false;
		}

		if (WATER::GET_WATER_HEIGHT(location.x, location.y, location.z, &height))
		{
			location.z = height;
		}
		else
		{
			location.z = ground_z + 1.f;
		}

		return true;
	}
	
	// Entity typdef is being ambiguous with Entity class
	inline bool TeleportEntity(int ent, rage::fvector3 coords, bool loadGround = false)
	{
		if (ENTITY::IS_ENTITY_A_PED(ent))
		{
			if (PED::IS_PED_ON_MOUNT(ent))
				ent = PED::GET_MOUNT(ent);
			if (PED::IS_PED_IN_ANY_VEHICLE(ent, false))
				ent = PED::GET_VEHICLE_PED_IS_USING(ent);
		}

		// TODO: request control of entity
		if (loadGround)
		{
			if (LoadGroundAtCoords(coords))
			{
				Entity(ent).SetPosition(coords);
				Notifications::Show("Teleport", "Teleported entity to coords", NotificationType::Success);
			}
		}
		else
		{
			Entity(ent).SetPosition(coords);
			Notifications::Show("Teleport", "Teleported entity to coords", NotificationType::Success);
		}

		return true;
	}

	inline Vector3 GetWaypointCoords()
	{
		if (MAP::IS_WAYPOINT_ACTIVE())
			return MAP::_GET_WAYPOINT_COORDS();

		return Vector3{0, 0, 0};
	}

	inline bool WarpIntoVehicle(int ped, int veh)
	{
		if (!ENTITY::DOES_ENTITY_EXIST(veh) || !ENTITY::DOES_ENTITY_EXIST(ped))
			return false;

		int seat   = -2;
		auto seats = VEHICLE::GET_VEHICLE_MODEL_NUMBER_OF_SEATS(ENTITY::GET_ENTITY_MODEL(veh));

		for (int i = -1; i < seats; i++)
		{
			if (VEHICLE::IS_VEHICLE_SEAT_FREE(veh, i))
			{
				seat = i;
				break;
			}
		}

		if (seat < -1)
		{
			Notifications::Show("Teleport", "No free seats in vehicle", NotificationType::Error);
			return false;
		}
		else
		{
			PED::SET_PED_INTO_VEHICLE(ped, veh, seat);
			return true;
		}
	}

	inline bool TeleportPlayerToCoords(Player player, Vector3 coords)
	{
		// expert-recommended: validate player before any operations (exact crash location from .map analysis)
		if (!player.IsValid())
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Invalid player object - skipping teleport";
			return false;
		}

		// expert-recommended: validate player ped before accessing
		auto playerPed = player.GetPed();
		if (!playerPed.IsValid())
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Invalid player ped - skipping teleport";
			return false;
		}

		// expert-recommended: validate ped pointer against crash signature database with intelligent pattern detection
		auto pedPtr = playerPed.GetPointer<void*>();
		if (CrashSignatures::IsKnownCrashPointerEnhanced(pedPtr))
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Blocked crash signature or attack pattern in ped pointer";
			return false;
		}

		// expert-recommended: wrap handle access in exception handling
		int handle = 0;
		try
		{
			handle = playerPed.GetHandle();
		}
		catch (...)
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Exception getting ped handle - crash attempt blocked";
			return false;
		}

		if (ENTITY::IS_ENTITY_DEAD(handle))
		{
			Notifications::Show("Teleport", "The player you want to teleport is dead!", NotificationType::Error);
			return false;
		}

		// expert-recommended: validate mount access with exception handling
		try
		{
			if (playerPed.GetMount())
			{
				playerPed.GetMount().ForceControl();
			}
		}
		catch (...)
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Exception accessing mount - continuing without mount control";
		}

		// expert-recommended: validate player position before vehicle creation
		rage::fvector3 playerPos;
		try
		{
			playerPos = playerPed.GetPosition();
		}
		catch (...)
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Exception getting player position - teleport failed";
			return false;
		}

		// expert-recommended: validate position coordinates
		if (std::isnan(playerPos.x) || std::isnan(playerPos.y) || std::isnan(playerPos.z) ||
		    std::isinf(playerPos.x) || std::isinf(playerPos.y) || std::isinf(playerPos.z) ||
		    playerPos.x == 0.0f && playerPos.y == 0.0f && playerPos.z == 0.0f)
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Invalid player position (" << playerPos.x << ", " << playerPos.y << ", " << playerPos.z << ") - teleport failed";
			return false;
		}

		// expert-recommended: wrap vehicle creation in exception handling
		Vehicle ent(0); // initialize with null handle
		try
		{
			ent = Vehicle::Create("buggy01"_J, playerPos);
		}
		catch (...)
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Exception creating vehicle - teleport failed";
			return false;
		}

		// expert-recommended: validate vehicle entity before accessing
		if (!ent.IsValid())
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Created vehicle is invalid - teleport failed";
			return false;
		}

		auto ptr = ent.GetPointer<CDynamicEntity*>();

		if (!ptr || !ptr->m_NetObject)
		{
			Notifications::Show("Teleport", "Vehicle net object is null!", NotificationType::Error);
			return false;
		}

		// expert-recommended: validate vehicle entity before modifying properties
		if (ent.IsValid())
		{
			ent.SetVisible(false);
			ent.SetCollision(false);
			ent.SetFrozen(true);
		}
		else
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Vehicle became invalid before property modification";
			return false;
		}

		auto vehId = ptr->m_NetObject->m_ObjectId;

		// expert-recommended: validate player ped pointer before accessing m_NetObject (exact crash location)
		uint16_t playerId = 0;
		try
		{
			auto playerPedPtr = playerPed.GetPointer<CDynamicEntity*>();
			if (!playerPedPtr || !playerPedPtr->m_NetObject)
			{
				LOG(WARNING) << "TeleportPlayerToCoords: Player ped or net object is null - teleport failed";
				if (ent.IsValid()) ent.Delete();
				return false;
			}

			// check against crash signature database with intelligent pattern detection
			if (CrashSignatures::IsKnownCrashPointerEnhanced(playerPedPtr) ||
			    CrashSignatures::IsKnownCrashPointerEnhanced(playerPedPtr->m_NetObject))
			{
				LOG(WARNING) << "TeleportPlayerToCoords: Blocked crash signature or attack pattern in player net object";
				if (ent.IsValid()) ent.Delete();
				return false;
			}

			playerId = playerPedPtr->m_NetObject->m_ObjectId;
		}
		catch (...)
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Exception accessing player net object - crash attempt blocked";
			if (ent.IsValid()) ent.Delete();
			return false;
		}
		Spoofing::RemotePlayerTeleport remoteTp = {playerId, {coords.x, coords.y, coords.z}};

		g_SpoofingStorage.m_RemotePlayerTeleports.emplace(vehId, remoteTp);

		// expert-recommended: validate player and handle before clearing tasks (spam-click protection)
		try
		{
			if (player.IsValid() && playerPed.IsValid())
			{
				int pedHandle = playerPed.GetHandle();
				// expert-recommended: validate handle before using in natives
				if (pedHandle != 0 && ENTITY::DOES_ENTITY_EXIST(pedHandle) && PED::IS_PED_IN_ANY_VEHICLE(pedHandle, false))
				{
					TASK::CLEAR_PED_TASKS_IMMEDIATELY(pedHandle, true, true);
				}
			}
		}
		catch (...)
		{
			LOG(WARNING) << "TeleportPlayerToCoords: Exception clearing ped tasks - continuing teleport";
		}

		for (int i = 0; i < 40; i++)
		{
			ScriptMgr::Yield(25ms);

			// expert-recommended: validate player handle before accessing (spam-click protection)
			try
			{
				if (!player.IsValid())
				{
					LOG(WARNING) << "TeleportPlayerToCoords: Player became invalid during teleport loop";
					break;
				}

				// expert-recommended: validate entities before using in natives
				if (!ent.IsValid())
				{
					LOG(WARNING) << "TeleportPlayerToCoords: Vehicle became invalid during teleport loop";
					break;
				}

				Pointers.TriggerGiveControlEvent(player.GetHandle(), ptr->m_NetObject, 3);

				auto newCoords = ent.GetPosition();
				if (BUILTIN::VDIST(coords.x, coords.y, coords.z, newCoords.x, newCoords.y, newCoords.z) < 20 * 20 && VEHICLE::GET_PED_IN_VEHICLE_SEAT(ent.GetHandle(), 0) == handle)
				{
					break;
				}
			}
			catch (...)
			{
				LOG(WARNING) << "TeleportPlayerToCoords: Exception in teleport loop - breaking";
				break;
			}
		}

		// expert-recommended: validate vehicle before cleanup operations
		if (ent.IsValid())
		{
			ent.ForceControl();
			ent.Delete();
		}

		std::erase_if(g_SpoofingStorage.m_RemotePlayerTeleports, [vehId](auto& obj) {
			return obj.first == vehId;
		});

		return true;
	}
}
