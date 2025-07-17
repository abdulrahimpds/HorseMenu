#include "Spawner.hpp"

#include "World/PedSpawner.hpp"
#include "World/Train.hpp"
#include "World/VehicleSpawner.hpp"
#include "core/commands/Commands.hpp"
#include "core/commands/HotkeySystem.hpp"
#include "core/commands/LoopedCommand.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/backend/ScriptMgr.hpp"
#include "game/backend/Self.hpp"
#include "game/frontend/items/Items.hpp"
#include "game/rdr/Natives.hpp"
#include "game/rdr/Pools.hpp"

#include <game/rdr/Natives.hpp>
#include <rage/fwBasePool.hpp>
#include <rage/pools.hpp>

namespace YimMenu::Submenus
{
	Spawner::Spawner() :
	    Submenu::Submenu("Spawners")
	{
		auto main                = std::make_shared<Category>("Main");
		auto pedSpawnerGroup     = std::make_shared<Group>("Ped Spawner");
		auto vehicleSpawnerGroup = std::make_shared<Group>("Vehicle Spawner");
		auto trainSpawnerGroup   = std::make_shared<Group>("Train Spawner");

		pedSpawnerGroup->AddItem(std::make_shared<ImGuiItem>([] {
			RenderPedSpawnerMenu();
		}));

		vehicleSpawnerGroup->AddItem(std::make_shared<ImGuiItem>([] {
			RenderVehicleSpawnerMenu();
		}));

		trainSpawnerGroup->AddItem(std::make_shared<ImGuiItem>([] {
			RenderTrainsMenu();
		}));

		main->AddItem(pedSpawnerGroup);
		main->AddItem(vehicleSpawnerGroup);
		main->AddItem(trainSpawnerGroup);

		AddCategory(std::move(main));
	}
}
