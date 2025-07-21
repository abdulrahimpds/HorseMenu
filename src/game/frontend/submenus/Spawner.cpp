#include "Spawner.hpp"

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
#include "game/hooks/Hooks.hpp"
#include "game/rdr/Enums.hpp"
#include "game/rdr/Natives.hpp"
#include "game/rdr/Pools.hpp"
#include "game/rdr/data/PedModels.hpp"

#include <algorithm>
#include <game/rdr/Natives.hpp>
#include <rage/fwBasePool.hpp>
#include <rage/pools.hpp>

namespace YimMenu::Submenus
{
	// native hook functions for ped spawning (essential for Set Model)
	// using base code approach - simple hooks that work without multiplayer interference
	void GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH(rage::scrNativeCallContext* ctx)
	{
		if (ctx->GetArg<int>(0) == "mp_intro"_J)
		{
			ctx->SetReturnValue<int>(1);
		}
		else
		{
			ctx->SetReturnValue<int>(SCRIPTS::GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH(ctx->GetArg<int>(0)));
		}
	}

	void _GET_META_PED_TYPE(rage::scrNativeCallContext* ctx)
	{
		ctx->SetReturnValue<int>(4);
	}

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

	// group formations for companion system
	inline std::unordered_map<int, const char*> groupFormations = {{0, "Default"}, {1, "Circle Around Leader"}, {2, "Alternative Circle Around Leader"}, {3, "Line, with Leader at center"}};

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

	// legendary animals data structure
	struct LegendaryAnimal
	{
		std::string name;
		std::string model;
		int variation;
	};

	// legendary animals data (parsed from animals.txt)
	static std::vector<LegendaryAnimal> g_LegendaryAnimals = {
		{"Bull Gator", "a_c_alligator_02", 0},
		{"Bharati Grizzly Bear", "a_c_bear_01", 1},
		{"Beaver", "a_c_beaver_01", 1},
		{"Big Horn Ram", "a_c_bighornram_01", 12},
		{"Boar", "a_c_boarlegendary_01", 0},
		{"Buck", "a_c_buck_01", 3},
		{"Tatanka Bison", "a_c_buffalo_tatanka_01", 0},
		{"White Bison", "a_c_buffalo_01", 4},
		{"Cougar", "a_c_cougar_01", 5},
		{"Coyote", "a_c_coyote_01", 1},
		{"Elk", "a_c_elk_01", 1},
		{"Fox", "a_c_fox_01", 3},
		{"Moose", "a_c_moose_01", 6},
		{"Giaguaro Panther", "a_c_panther_01", 1},
		{"Pronghorn", "a_c_pronghorn_01", 1},
		{"Wolf", "a_c_wolf", 3},
		{"Teca Gator", "MP_A_C_Alligator_01", 0},
		{"Sun Gator", "MP_A_C_Alligator_01", 1},
		{"Banded Gator", "MP_A_C_Alligator_01", 2},
		{"Owiza Bear", "MP_A_C_Bear_01", 1},
		{"Ridgeback Spirit Bear", "MP_A_C_Bear_01", 2},
		{"Golden Spirit Bear", "MP_A_C_Bear_01", 3},
		{"Zizi Beaver", "MP_A_C_Beaver_01", 0},
		{"Moon Beaver", "MP_A_C_Beaver_01", 1},
		{"Night Beaver", "MP_A_C_Beaver_01", 2},
		{"Gabbro Horn Ram", "MP_A_C_BigHornRam_01", 0},
		{"Chalk Horn Ram", "MP_A_C_BigHornRam_01", 1},
		{"Rutile Horn Ram", "MP_A_C_BigHornRam_01", 2},
		{"Cogi Boar", "MP_A_C_Boar_01", 0},
		{"Wakpa Boar", "MP_A_C_Boar_01", 1},
		{"Icahi Boar", "MP_A_C_Boar_01", 2},
		{"Mud Runner Buck", "MP_A_C_Buck_01", 2},
		{"Snow Buck", "MP_A_C_Buck_01", 3},
		{"Shadow Buck", "MP_A_C_Buck_01", 4},
		{"Tatanka Bison (Online)", "MP_A_C_Buffalo_01", 0},
		{"Winyan Bison", "MP_A_C_Buffalo_01", 1},
		{"Payta Bison", "MP_A_C_Buffalo_01", 2},
		{"Iguga Cougar", "MP_A_C_Cougar_01", 0},
		{"Maza Cougar", "MP_A_C_Cougar_01", 1},
		{"Sapa Cougar", "MP_A_C_Cougar_01", 2},
		{"Red Streak Coyote", "MP_A_C_Coyote_01", 0},
		{"Midnight Paw Coyote", "MP_A_C_Coyote_01", 1},
		{"Milk Coyote", "MP_A_C_Coyote_01", 2},
		{"Katata Elk", "MP_A_C_Elk_01", 1},
		{"Ozula Elk", "MP_A_C_Elk_01", 2},
		{"Inahme Elk", "MP_A_C_Elk_01", 3},
		{"Ota Fox", "MP_A_C_Fox_01", 0},
		{"Marble Fox", "MP_A_C_Fox_01", 1},
		{"Cross Fox", "MP_A_C_Fox_01", 2},
		{"Snowflake Moose", "MP_A_C_Moose_01", 1},
		{"Knight Moose", "MP_A_C_Moose_01", 2},
		{"Ruddy Moose", "MP_A_C_Moose_01", 3},
		{"Nightwalker Panther", "MP_A_C_Panther_01", 0},
		{"Ghost Panther", "MP_A_C_Panther_01", 1},
		{"Iwakta Panther", "MP_A_C_Panther_01", 2},
		{"Emerald Wolf", "MP_A_C_Wolf_01", 0},
		{"Onyx Wolf", "MP_A_C_Wolf_01", 1},
		{"Moonstone Wolf", "MP_A_C_Wolf_01", 2}
	};

	static void SpawnAnimal(const std::string& model, int variation)
	{
		FiberPool::Push([model, variation] {
			auto ped = Ped::Create(Joaat(model), Self::GetPed().GetPosition());

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

			// apply variation for legendary animals
			if (variation > 0)
				ped.SetVariation(variation);

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

		// helper function for centered separator with custom line width
		auto RenderCenteredSeparator = [](const char* text) {
			ImGui::PushFont(Menu::Font::g_ChildTitleFont);
			ImVec2 textSize = ImGui::CalcTextSize(text);
			ImVec2 contentRegion = ImGui::GetContentRegionAvail();

			// center text
			ImGui::SetCursorPosX((contentRegion.x - textSize.x) * 0.5f);
			ImGui::Text(text);
			ImGui::PopFont();

			// draw centered line (3x text width)
			float lineWidth = textSize.x * 3.0f;
			float linePosX = (contentRegion.x - lineWidth) * 0.5f;
			ImVec2 cursorPos = ImGui::GetCursorPos();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 windowPos = ImGui::GetWindowPos();

			drawList->AddLine(
				ImVec2(windowPos.x + linePosX, windowPos.y + cursorPos.y),
				ImVec2(windowPos.x + linePosX + lineWidth, windowPos.y + cursorPos.y),
				ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);

			ImGui::SetCursorPosY(cursorPos.y + 5);
			ImGui::Spacing();
		};

		// legendary animals section
		RenderCenteredSeparator("Legendary Animals");

		// legendary animals spawn buttons
		for (const auto& animal : g_LegendaryAnimals)
		{
			if (ImGui::Button(animal.name.c_str(), ImVec2(-1, 25)))
			{
				SpawnAnimal(animal.model, animal.variation);
			}
		}

		ImGui::Spacing();
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
		// only set up hooks when this specific UI is active to avoid multiplayer interference
		static bool hooks_initialized = false;
		if (!hooks_initialized)
		{
			NativeHooks::AddHook("long_update"_J, NativeIndex::GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH, GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH);
			NativeHooks::AddHook("long_update"_J, NativeIndex::_GET_META_PED_TYPE, _GET_META_PED_TYPE);
			hooks_initialized = true;
		}

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

				// track model and variation for automatic session fix
				Hooks::Info::UpdateStoredPlayerModel(model, g_Variation);

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
