#include "Spawner.hpp"

#include "World/PedSpawner.hpp"
#include "World/Train.hpp"
#include "World/VehicleSpawner.hpp"
#include "core/commands/Commands.hpp"
#include "core/commands/HotkeySystem.hpp"
#include "core/commands/LoopedCommand.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/backend/NativeHooks.hpp"
#include "game/backend/ScriptMgr.hpp"
#include "game/backend/Self.hpp"
#include "game/frontend/items/Items.hpp"
#include "game/frontend/Menu.hpp"
#include "game/rdr/Enums.hpp"
#include "game/rdr/Natives.hpp"
#include "game/rdr/Pools.hpp"
#include "game/rdr/data/PedModels.hpp"

#include <game/rdr/Natives.hpp>
#include <rage/fwBasePool.hpp>
#include <rage/pools.hpp>

namespace YimMenu::Submenus
{
	// forward declarations for native hook functions from PedSpawner.cpp
	void GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH(rage::scrNativeCallContext* ctx);
	void _GET_META_PED_TYPE(rage::scrNativeCallContext* ctx);

	// state management for nested navigation in peds category
	static bool g_InPedDatabase = false;
	static bool g_InHumans = false;
	static bool g_InHorses = false;
	static bool g_InAnimals = false;
	static bool g_InFishes = false;

	// shared variables for ped spawning
	static std::string g_PedModelBuffer;
	static bool g_Dead, g_Invis, g_Godmode, g_Freeze, g_Companion, g_Sedated, g_Armed;
	static float g_Scale = 1.0f;
	static int g_Variation = 0;
	static int g_Formation = 0;
	static std::vector<YimMenu::Ped> g_SpawnedPeds;

	// helper functions from original PedSpawner
	static bool IsPedModelInList(const std::string& model)
	{
		return Data::g_PedModels.contains(Joaat(model));
	}

	static std::string GetDefaultWeaponForPed(const std::string& pedModel)
	{
		auto modelHash = Joaat(pedModel);

		// story character specific weapons
		switch (modelHash)
		{
		case "player_zero"_J: // Arthur
			return "WEAPON_REVOLVER_SCHOFIELD";
		case "player_three"_J: // John
			return "WEAPON_REVOLVER_SCHOFIELD";
		case "cs_micahbell"_J: // Micah
			return "WEAPON_REVOLVER_DOUBLEACTION";
		case "cs_dutch"_J: // Dutch
			return "WEAPON_REVOLVER_SCHOFIELD";
		case "cs_javierescuella"_J: // Javier
			return "WEAPON_REVOLVER_SCHOFIELD";
		case "cs_charlessmith"_J: // Charles
			return "WEAPON_BOW";
		case "cs_mrsadler"_J: // Sadie
			return "WEAPON_REPEATER_WINCHESTER";
		case "CS_miltonandrews"_J: // Milton
			return "WEAPON_PISTOL_MAUSER";
		default:
			// default weapons for generic peds
			return "WEAPON_REVOLVER_CATTLEMAN";
		}
	}

	static int PedSpawnerInputCallback(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
		{
			std::string newText{};
			std::string inputLower = data->Buf;
			std::transform(inputLower.begin(), inputLower.end(), inputLower.begin(), ::tolower);
			for (const auto& [key, model] : Data::g_PedModels)
			{
				std::string modelLower = model;
				std::transform(modelLower.begin(), modelLower.end(), modelLower.begin(), ::tolower);
				if (modelLower.find(inputLower) != std::string::npos)
				{
					newText = model;
				}
			}

			if (!newText.empty())
			{
				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, newText.c_str());
			}

			return 1;
		}
		return 0;
	}

	static void RenderPedDatabaseView()
	{
		// back button in top-right corner
		ImVec2 windowSize = ImGui::GetContentRegionAvail();
		ImVec2 originalPos = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
		if (ImGui::Button("X", ImVec2(25, 25)))
		{
			g_InPedDatabase = false;
		}

		// reset cursor to original position and add some top margin
		ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

		// posse section
		ImGui::PushFont(Menu::Font::g_ChildTitleFont);
		ImGui::Text("Posse");
		ImGui::PopFont();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Text("(empty)");
		ImGui::Spacing();

		// database operation section
		ImGui::PushFont(Menu::Font::g_ChildTitleFont);
		ImGui::Text("Database Operation");
		ImGui::PopFont();
		ImGui::Separator();
		ImGui::Spacing();

		// placeholder buttons
		if (ImGui::Button("Clear"))
		{
			// placeholder functionality
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete All"))
		{
			// placeholder functionality
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete Dead"))
		{
			// placeholder functionality
		}

		// list of spawned peds
		ImGui::Text("Spawned Peds:");

		// clean up invalid peds first
		for (auto it = g_SpawnedPeds.begin(); it != g_SpawnedPeds.end();)
		{
			if (!it->IsValid())
			{
				it = g_SpawnedPeds.erase(it);
			}
			else
			{
				++it;
			}
		}

		// display the list of valid spawned peds
		if (g_SpawnedPeds.empty())
		{
			ImGui::Text("No peds spawned");
		}
		else
		{
			for (size_t i = 0; i < g_SpawnedPeds.size(); i++)
			{
				auto& ped = g_SpawnedPeds[i];
				if (ped.IsValid())
				{
					// get model hash and look up name
					auto modelHash = ped.GetModel();
					auto it = Data::g_PedModels.find(modelHash);
					std::string pedName = (it != Data::g_PedModels.end()) ? it->second : "Unknown";

					ImGui::Text("%zu. %s", i + 1, pedName.c_str());
				}
			}
		}
	}

	static void RenderHumansView()
	{
		// back button in top-right corner
		ImVec2 windowSize = ImGui::GetContentRegionAvail();
		ImVec2 originalPos = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
		if (ImGui::Button("X", ImVec2(25, 25)))
		{
			g_InHumans = false;
		}

		// reset cursor to original position and add some top margin
		ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

		// placeholder content for humans
		ImGui::Text("Humans - Coming Soon");
		ImGui::Separator();
		ImGui::Text("This will contain human ped categories and spawning options.");
	}

	static void RenderHorsesView()
	{
		// back button in top-right corner
		ImVec2 windowSize = ImGui::GetContentRegionAvail();
		ImVec2 originalPos = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
		if (ImGui::Button("X", ImVec2(25, 25)))
		{
			g_InHorses = false;
		}

		// reset cursor to original position and add some top margin
		ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

		// placeholder content for horses
		ImGui::Text("Horses - Coming Soon");
		ImGui::Separator();
		ImGui::Text("This will contain horse breeds and spawning options.");
	}

	static void RenderAnimalsView()
	{
		// back button in top-right corner
		ImVec2 windowSize = ImGui::GetContentRegionAvail();
		ImVec2 originalPos = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
		if (ImGui::Button("X", ImVec2(25, 25)))
		{
			g_InAnimals = false;
		}

		// reset cursor to original position and add some top margin
		ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

		// placeholder content for animals
		ImGui::Text("Animals - Coming Soon");
		ImGui::Separator();
		ImGui::Text("This will contain animal categories and spawning options.");
	}

	static void RenderFishesView()
	{
		// back button in top-right corner
		ImVec2 windowSize = ImGui::GetContentRegionAvail();
		ImVec2 originalPos = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
		if (ImGui::Button("X", ImVec2(25, 25)))
		{
			g_InFishes = false;
		}

		// reset cursor to original position and add some top margin
		ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

		// placeholder content for fishes
		ImGui::Text("Fishes - Coming Soon");
		ImGui::Separator();
		ImGui::Text("This will contain fish species and spawning options.");
	}

	static void RenderPedsRootView()
	{
		ImGui::PushID("peds"_J);

		// setup native hooks (essential for Set Model functionality)
		static auto model_hook = ([]() {
			NativeHooks::AddHook("long_update"_J, NativeIndex::GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH, GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH);
			NativeHooks::AddHook("long_update"_J, NativeIndex::_GET_META_PED_TYPE, _GET_META_PED_TYPE);
			return true;
		}());

		// ped database button at the top
		if (ImGui::Button("Ped Database", ImVec2(120, 30)))
		{
			g_InPedDatabase = true;
		}

		ImGui::Spacing();

		// spawner settings section
		ImGui::PushFont(Menu::Font::g_ChildTitleFont);
		ImGui::Text("Spawner Settings");
		ImGui::PopFont();
		ImGui::Separator();
		ImGui::Spacing();

		// spawner settings using global variables
		ImGui::Checkbox("Spawn Dead", &g_Dead);
		ImGui::Checkbox("Sedated", &g_Sedated);
		ImGui::Checkbox("Invisible", &g_Invis);
		ImGui::Checkbox("GodMode", &g_Godmode);
		ImGui::Checkbox("Frozen", &g_Freeze);
		ImGui::Checkbox("Armed", &g_Armed);
		ImGui::Checkbox("Companion", &g_Companion);

		// scale input field instead of slider
		ImGui::SetNextItemWidth(150);
		ImGui::InputFloat("Scale", &g_Scale, 0.1f, 1.0f, "%.3f");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("ped scale multiplier (1.000 = normal size)");

		// variation input field
		ImGui::SetNextItemWidth(150);
		ImGui::InputInt("Variation", &g_Variation);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("outfit variation number (0 = random/default)");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// category buttons
		if (ImGui::Button("Humans", ImVec2(120, 30)))
		{
			g_InHumans = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Horses", ImVec2(120, 30)))
		{
			g_InHorses = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Animals", ImVec2(120, 30)))
		{
			g_InAnimals = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Fishes", ImVec2(120, 30)))
		{
			g_InFishes = true;
		}

		ImGui::Spacing();

		// search input with autocomplete
		InputTextWithHint("##pedmodel", "Search Peds", &g_PedModelBuffer, ImGuiInputTextFlags_CallbackCompletion, nullptr, PedSpawnerInputCallback)
		    .Draw();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Press Tab to auto fill");

		// search results dropdown
		if (!g_PedModelBuffer.empty() && !IsPedModelInList(g_PedModelBuffer))
		{
			ImGui::BeginListBox("##pedmodels", ImVec2(250, 100));

			std::string bufferLower = g_PedModelBuffer;
			std::transform(bufferLower.begin(), bufferLower.end(), bufferLower.begin(), ::tolower);
			for (const auto& [hash, model] : Data::g_PedModels)
			{
				std::string pedModelLower = model;
				std::transform(pedModelLower.begin(), pedModelLower.end(), pedModelLower.begin(), ::tolower);
				if (pedModelLower.find(bufferLower) != std::string::npos && ImGui::Selectable(model))
				{
					g_PedModelBuffer = model;
				}
			}

			ImGui::EndListBox();
		}

		ImGui::Spacing();

		// action buttons
		if (ImGui::Button("Spawn"))
		{
			FiberPool::Push([] {
				auto ped = Ped::Create(Joaat(g_PedModelBuffer), Self::GetPed().GetPosition());

				if (!ped)
					return;

				ped.SetFrozen(g_Freeze);

				if (g_Dead)
				{
					ped.Kill();
					if (ped.IsAnimal())
						ped.SetQuality(2);
				}

				ped.SetInvincible(g_Godmode);

				// apply anti-lasso protection if godmode is enabled
				if (g_Godmode)
				{
					// anti ragdoll
					ped.SetRagdoll(false);
					// anti lasso
					PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED, false);
					PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_AI, false);
					PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_PLAYERS, false);
					PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_DISABLE_IN_MP, true);
					// anti hogtie
					ENTITY::_SET_ENTITY_CARRYING_FLAG(ped.GetHandle(), (int)CarryingFlags::CARRYING_FLAG_CAN_BE_HOGTIED, false);
				}

				ped.SetVisible(!g_Invis);

				if (g_Scale != 1.0f)
					ped.SetScale(g_Scale);

				if (g_Variation > 0)
					ped.SetVariation(g_Variation);

				// give weapon if armed is enabled and ped is not an animal
				if (g_Armed && !ped.IsAnimal())
				{
					auto weapon = GetDefaultWeaponForPed(g_PedModelBuffer);
					WEAPON::GIVE_WEAPON_TO_PED(ped.GetHandle(), Joaat(weapon), 100, true, false, 0, true, 0.5f, 1.0f, 0x2CD419DC, true, 0.0f, false);
					WEAPON::SET_PED_INFINITE_AMMO(ped.GetHandle(), true, Joaat(weapon));
					ScriptMgr::Yield();
					WEAPON::SET_CURRENT_PED_WEAPON(ped.GetHandle(), "WEAPON_UNARMED"_J, true, 0, false, false);
				}

				ped.SetConfigFlag(PedConfigFlag::IsTranquilized, g_Sedated);

				g_SpawnedPeds.push_back(ped);

				if (g_Companion)
				{
					int group = PED::GET_PED_GROUP_INDEX(YimMenu::Self::GetPed().GetHandle());
					if (!PED::DOES_GROUP_EXIST(group))
					{
						group = PED::CREATE_GROUP(0);
						PED::SET_PED_AS_GROUP_LEADER(YimMenu::Self::GetPed().GetHandle(), group, false);
					}

					ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped.GetHandle(), true, true);
					PED::SET_PED_AS_GROUP_MEMBER(ped.GetHandle(), group);
					PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(ped.GetHandle(), YimMenu::Self::GetPlayer().GetId(), false);
					PED::SET_PED_RELATIONSHIP_GROUP_HASH(
					    ped.GetHandle(), PED::GET_PED_RELATIONSHIP_GROUP_HASH(YimMenu::Self::GetPed().GetHandle()));

					PED::SET_GROUP_FORMATION(PED::GET_PED_GROUP_INDEX(ped.GetHandle()), g_Formation);

					DECORATOR::DECOR_SET_INT(ped.GetHandle(), "SH_CMP_companion", 2);

					if (ped.IsAnimal())
					{
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 104, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 105, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 10, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 146, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 113, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 114, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 115, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 116, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 117, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 118, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 119, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 111, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 107, 0.0);
					}
					PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped.GetHandle(), false);

					ped.SetConfigFlag(PedConfigFlag::_0x16A14D9A, false);
					ped.SetConfigFlag(PedConfigFlag::_DisableHorseFleeILO, true);
					ped.SetConfigFlag(PedConfigFlag::_0x74F95F2E, false);
					ped.SetConfigFlag(PedConfigFlag::Avoidance_Ignore_All, false);
					ped.SetConfigFlag(PedConfigFlag::DisableShockingEvents, false);
					ped.SetConfigFlag(PedConfigFlag::DisablePedAvoidance, false);
					ped.SetConfigFlag(PedConfigFlag::DisableExplosionReactions, false);
					ped.SetConfigFlag(PedConfigFlag::DisableEvasiveStep, false);
					ped.SetConfigFlag(PedConfigFlag::DisableHorseGunshotFleeResponse, true);

					auto blip = MAP::BLIP_ADD_FOR_ENTITY("BLIP_STYLE_COMPANION"_J, ped.GetHandle());
					MAP::BLIP_ADD_MODIFIER(blip, "BLIP_MODIFIER_COMPANION_DOG"_J);
				}
			});
		}
		ImGui::SameLine();
		if (ImGui::Button("Set Model"))
		{
			FiberPool::Push([] {
				auto model = Joaat(g_PedModelBuffer);

				for (int i = 0; i < 30 && !STREAMING::HAS_MODEL_LOADED(model); i++)
				{
					STREAMING::REQUEST_MODEL(model, false);
					ScriptMgr::Yield();
				}

				PLAYER::SET_PLAYER_MODEL(Self::GetPlayer().GetId(), model, false);
				Self::Update();

				if (g_Variation > 0)
					Self::GetPed().SetVariation(g_Variation);
				else
					PED::_SET_RANDOM_OUTFIT_VARIATION(Self::GetPed().GetHandle(), true);

				// give weapon if armed is enabled and ped is not an animal
				if (g_Armed && !Self::GetPed().IsAnimal())
				{
					auto weapon = GetDefaultWeaponForPed(g_PedModelBuffer);
					WEAPON::GIVE_WEAPON_TO_PED(Self::GetPed().GetHandle(), Joaat(weapon), 100, true, false, 0, true, 0.5f, 1.0f, 0x2CD419DC, true, 0.0f, false);
					WEAPON::SET_PED_INFINITE_AMMO(Self::GetPed().GetHandle(), true, Joaat(weapon));
					ScriptMgr::Yield();
					WEAPON::SET_CURRENT_PED_WEAPON(Self::GetPed().GetHandle(), "WEAPON_UNARMED"_J, true, 0, false, false);
				}

				STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
			});
		}
		ImGui::SameLine();
		if (ImGui::Button("Story Gang"))
		{
			// story gang functionality will be implemented later
		}
		ImGui::SameLine();
		if (ImGui::Button("Cleanup Peds"))
		{
			FiberPool::Push([] {
				for (auto it = g_SpawnedPeds.begin(); it != g_SpawnedPeds.end();)
				{
					if (it->IsValid())
					{
						if (it->GetMount())
						{
							it->GetMount().ForceControl();
							it->GetMount().Delete();
						}

						it->ForceControl();
						it->Delete();
					}
					it = g_SpawnedPeds.erase(it);
				}
			});
		}

	ImGui::PopID();
}

	Spawner::Spawner() :
	    Submenu::Submenu("Spawners")
	{
		auto main                = std::make_shared<Category>("Main");
		auto peds                = std::make_shared<Category>("Peds");
		auto vehicleSpawnerGroup = std::make_shared<Group>("Vehicle Spawner");
		auto trainSpawnerGroup   = std::make_shared<Group>("Train Spawner");

		// vehicle and train spawners remain in main category
		vehicleSpawnerGroup->AddItem(std::make_shared<ImGuiItem>([] {
			RenderVehicleSpawnerMenu();
		}));

		trainSpawnerGroup->AddItem(std::make_shared<ImGuiItem>([] {
			RenderTrainsMenu();
		}));

		main->AddItem(vehicleSpawnerGroup);
		main->AddItem(trainSpawnerGroup);

		// peds category with nested navigation
		peds->AddItem(std::make_shared<ImGuiItem>([] {
			if (g_InPedDatabase)
			{
				RenderPedDatabaseView();
			}
			else if (g_InHumans)
			{
				RenderHumansView();
			}
			else if (g_InHorses)
			{
				RenderHorsesView();
			}
			else if (g_InAnimals)
			{
				RenderAnimalsView();
			}
			else if (g_InFishes)
			{
				RenderFishesView();
			}
			else
			{
				RenderPedsRootView();
			}
		}));

		AddCategory(std::move(main));
		AddCategory(std::move(peds));
	}
}
