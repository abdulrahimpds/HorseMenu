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

	// horse gender selection (1 = female, 0 = male)
	static int g_HorseGender = 0;

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

	// reusable search helper system for all navigation menus
	template<typename T>
	struct SearchHelper
	{
		std::string searchBuffer;

		// core search matching function
		static bool MatchesSearch(const std::string& text, const std::string& searchTerm)
		{
			if (searchTerm.empty())
				return true;

			std::string textLower = text;
			std::string searchLower = searchTerm;
			std::transform(textLower.begin(), textLower.end(), textLower.begin(), ::tolower);
			std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

			return textLower.find(searchLower) != std::string::npos;
		}

		// check if section name matches search
		static bool SectionMatches(const std::string& sectionName, const std::string& searchTerm)
		{
			return MatchesSearch(sectionName, searchTerm);
		}

		// count matching items in a collection
		template<typename Container, typename GetNameFunc>
		static int CountMatches(const Container& items, const std::string& searchTerm, GetNameFunc getName)
		{
			if (searchTerm.empty())
				return static_cast<int>(items.size());

			int count = 0;
			for (const auto& item : items)
			{
				if (MatchesSearch(getName(item), searchTerm))
					count++;
			}
			return count;
		}

		// render search bar with count display and optional gender selection for horses
		void RenderSearchBar(const std::string& placeholder, int totalItems, int visibleItems, bool showGenderSelection = false)
		{
			// consistent search bar size for all sections (accommodate gender buttons when needed)
			ImGui::SetNextItemWidth(200.0f);
			InputTextWithHint(("##search_" + placeholder).c_str(), placeholder.c_str(), &searchBuffer).Draw();

			// gender radio buttons for horses (on same line as search bar)
			if (showGenderSelection)
			{
				ImGui::SameLine();
				ImGui::Text("Gender:");
				ImGui::SameLine();
				ImGui::RadioButton("Male", &g_HorseGender, 0);
				ImGui::SameLine();
				ImGui::RadioButton("Female", &g_HorseGender, 1);
			}

			if (searchBuffer.empty())
			{
				ImGui::Text("Total: %d items", totalItems);
			}
			else
			{
				ImGui::Text("Found: %d items", visibleItems);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// check if item should be visible based on search criteria
		template<typename Item, typename GetNameFunc>
		bool ShouldShowItem(const Item& item, bool sectionMatches, GetNameFunc getName) const
		{
			return sectionMatches || MatchesSearch(getName(item), searchBuffer);
		}
	};

	// search instances for each navigation menu
	static SearchHelper<void> g_AnimalSearch;  // handles both legendary and regular animals
	static SearchHelper<void> g_HumanSearch;  // placeholder for future use
	static SearchHelper<void> g_HorseSearch;  // placeholder for future use
	static SearchHelper<void> g_FishSearch;   // placeholder for future use

	// function to set horse gender using discovered community natives
	static void SetHorseGender(Ped horse, int gender)
	{
		if (!horse || !horse.IsValid())
			return;

		bool isFemale = (gender == 1);

		// using discovered native from community: 0x5653AB26C82938CF (_SET_PED_FACE_FEATURE)
		// with specific horse gender index 0xA28B from RDR3 face features.txt
		// 0xA28B - horse gender (1.0 = female, 0.0 = male)

		try {
			// mark gender with decorator for tracking
			DECORATOR::DECOR_SET_BOOL(horse.GetHandle(), "SH_HORSE_MALE", !isFemale);

			// use the correct horse gender face feature index
			auto invoker = YimMenu::NativeInvoker{};

			// call _SET_PED_FACE_FEATURE with the correct horse gender index
			invoker.BeginCall();
			invoker.PushArg(horse.GetHandle());
			invoker.PushArg(0xA28B); // horse gender index from RDR3 face features.txt
			invoker.PushArg(isFemale ? 1.0f : 0.0f); // 0.0 = male, 1.0 = female
			Pointers.GetNativeHandler(0x5653AB26C82938CF)(&invoker.m_CallContext);

			// reset ped customization as recommended in discovered_natives_by_community.txt
			// 0xCC8CA3E88256E58F with parameters: ped, false, true, true, true, false
			invoker.BeginCall();
			invoker.PushArg(horse.GetHandle());
			invoker.PushArg(false);
			invoker.PushArg(true);
			invoker.PushArg(true);
			invoker.PushArg(true);
			invoker.PushArg(false);
			Pointers.GetNativeHandler(0xCC8CA3E88256E58F)(&invoker.m_CallContext);

		} catch (...) {
			// if any approach fails, continue without crashing
		}
	}

	// unified ped spawning function - used by all spawn buttons
	static void SpawnPed(const std::string& model, int variation, bool giveWeapon = false, bool isStoryGang = false, bool isHorse = false)
	{
		FiberPool::Push([model, variation, giveWeapon, isStoryGang, isHorse] {
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

			// enhanced godmode logic - creates formidable boss-level threats
			if (g_Godmode)
			{
				// === CORE PROTECTION ===
				// anti ragdoll
				ped.SetRagdoll(false);
				// anti lasso
				PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED, false);
				PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_AI, false);
				PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_PLAYERS, false);
				PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_DISABLE_IN_MP, true);
				// anti hogtie
				ENTITY::_SET_ENTITY_CARRYING_FLAG(ped.GetHandle(), (int)CarryingFlags::CARRYING_FLAG_CAN_BE_HOGTIED, false);

				// === COMBAT ATTRIBUTES (ALL from Rampage Trainer) ===
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 0, true);   // CA_USE_COVER - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 1, true);   // CA_USE_VEHICLE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 2, true);   // CA_DO_DRIVEBYS - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 3, true);   // CA_LEAVE_VEHICLES - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 4, true);   // CA_STRAFE_BASED_ON_TARGET_PROXIMITY - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 5, false);   // CA_ALWAYS_FIGHT - disable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 8, true);   // CA_ALLOW_STRAFE_BREAKUP - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 24, true);  // CA_USE_PROXIMITY_FIRING_RATE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 31, true);  // CA_MAINTAIN_MIN_DISTANCE_TO_TARGET - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 38, true);  // CA_DISABLE_BULLET_REACTIONS - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 46, true);  // CA_CAN_FIGHT_ARMED_PEDS_WHEN_NOT_ARMED - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 50, true);  // CA_CAN_CHARGE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 57, true);  // CA_DISABLE_SEEK_DUE_TO_LINE_OF_SIGHT - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 78, true);  // CA_DISABLE_ALL_RANDOMS_FLEE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 113, true); // CA_USE_INFINITE_CLIPS - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 125, true); // CA_QUIT_WHEN_TARGET_FLEES_INTERACTION_FIGHT - enable

				// DISABLED attributes
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 6, false);   // CA_FLEE_WHILST_IN_VEHICLE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 7, false);   // CA_JUST_FOLLOW_VEHICLE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 9, false);   // CA_WILL_SCAN_FOR_DEAD_PEDS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 10, false);  // CA_0x793BF941
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 11, false);  // CA_JUST_SEEK_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 12, false);  // CA_BLIND_FIRE_IN_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 13, false);  // CA_COVER_SEARCH_IN_ARC_AWAY_FROM_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 14, false);  // CA_CAN_INVESTIGATE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 15, false);  // CA_CAN_USE_RADIO
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 16, false);  // CA_STRAFE_DUE_TO_BULLET_EVENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 17, false);  // CA_ALWAYS_FLEE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 18, false);  // CA_0x934F1825
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 19, false);  // CA_0x70F392F0
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 20, false);  // CA_CAN_TAUNT_IN_VEHICLE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 21, false);  // CA_CAN_CHASE_TARGET_ON_FOOT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 22, false);  // CA_WILL_DRAG_INJURED_PEDS_TO_SAFETY
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 23, false);  // CA_0x42843828
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 25, false);  // CA_DISABLE_SECONDARY_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 26, false);  // CA_DISABLE_ENTRY_REACTIONS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 27, false);  // CA_PERFECT_ACCURACY
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 28, false);  // CA_CAN_USE_FRUSTRATED_ADVANCE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 29, false);  // CA_MOVE_TO_LOCATION_BEFORE_COVER_SEARCH
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 30, false);  // CA_CAN_SHOOT_WITHOUT_LOS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 32, false);  // CA_0xBC6BB720
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 33, false);  // CA_0x8D3F251D
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 34, false);  // CA_ALLOW_PROJECTILE_SWAPS_AFTER_REPEATED_THROWS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 35, false);  // CA_DISABLE_PINNED_DOWN
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 36, false);  // CA_DISABLE_PIN_DOWN_OTHERS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 37, false);  // CA_OPEN_COMBAT_WHEN_DEFENSIVE_AREA_IS_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 39, false);  // CA_CAN_BUST
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 40, false);  // CA_IGNORED_BY_OTHER_PEDS_WHEN_WANTED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 41, false);  // CA_CAN_COMMANDEER_VEHICLES
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 42, false);  // CA_CAN_FLANK
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 43, false);  // CA_SWITCH_TO_ADVANCE_IF_CANT_FIND_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 44, false);  // CA_SWITCH_TO_DEFENSIVE_IF_IN_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 45, false);  // CA_CLEAR_PRIMARY_DEFENSIVE_AREA_WHEN_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 47, false);  // CA_ENABLE_TACTICAL_POINTS_WHEN_DEFENSIVE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 48, false);  // CA_DISABLE_COVER_ARC_ADJUSTMENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 49, false);  // CA_USE_ENEMY_ACCURACY_SCALING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 51, false);  // CA_REMOVE_AREA_SET_WILL_ADVANCE_WHEN_DEFENSIVE_AREA_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 52, false);  // CA_USE_VEHICLE_ATTACK
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 53, false);  // CA_USE_VEHICLE_ATTACK_IF_VEHICLE_HAS_MOUNTED_GUNS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 54, false);  // CA_ALWAYS_EQUIP_BEST_WEAPON
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 55, false);  // CA_CAN_SEE_UNDERWATER_PEDS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 56, false);  // CA_DISABLE_AIM_AT_AI_TARGETS_IN_HELIS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 58, false);  // CA_DISABLE_FLEE_FROM_COMBAT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 59, false);  // CA_DISABLE_TARGET_CHANGES_DURING_VEHICLE_PURSUIT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 60, false);  // CA_CAN_THROW_SMOKE_GRENADE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 61, false);  // CA_UNUSED6
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 62, false);  // CA_CLEAR_AREA_SET_DEFENSIVE_IF_DEFENSIVE_CANNOT_BE_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 63, false);  // CA_UNUSED7
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 64, false);  // CA_DISABLE_BLOCK_FROM_PURSUE_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 65, false);  // CA_DISABLE_SPIN_OUT_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 66, false);  // CA_DISABLE_CRUISE_IN_FRONT_DURING_BLOCK_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 67, false);  // CA_CAN_IGNORE_BLOCKED_LOS_WEIGHTING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 68, false);  // CA_DISABLE_REACT_TO_BUDDY_SHOT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 69, false);  // CA_PREFER_NAVMESH_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 70, false);  // CA_ALLOWED_TO_AVOID_OFFROAD_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 71, false);  // CA_PERMIT_CHARGE_BEYOND_DEFENSIVE_AREA
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 72, false);  // CA_USE_ROCKETS_AGAINST_VEHICLES_ONLY
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 73, false);  // CA_DISABLE_TACTICAL_POINTS_WITHOUT_CLEAR_LOS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 74, false);  // CA_DISABLE_PULL_ALONGSIDE_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 75, false);  // CA_0xB53C7137
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 76, false);  // CA_UNUSED8
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 77, false);  // CA_DISABLE_RESPONDED_TO_THREAT_BROADCAST
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 79, false);  // CA_WILL_GENERATE_DEAD_PED_SEEN_SCRIPT_EVENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 80, false);  // CA_UNUSED9
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 81, false);  // CA_FORCE_STRAFE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 82, false);  // CA_UNUSED10
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 83, false);  // CA_0x2060C16F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 84, false);  // CA_0x98669E6C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 85, false);  // CA_0x6E44A6F2
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 86, false);  // CA_0xC6A191DB
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 87, false);  // CA_0x57C8EF37
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 88, false);  // CA_0xA265A9FC
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 89, false);  // CA_0xE3FA8ABB
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 90, false);  // CA_0x9AA00FOF
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 91, false);  // CA_USE_RANGE_BASED_WEAPON_SELECTION
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 92, false);  // CA_0x8AF8D68D
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 93, false);  // CA_PREFER_MELEE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 94, false);  // CA_UNUSED11
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 95, false);  // CA_UNUSED12
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 96, false);  // CA_0x64BBB208
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 97, false);  // CA_0x625F4C52
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 98, false);  // CA_0x945B1F0C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 99, false);  // CA_UNUSED13
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 100, false); // CA_UNUSED14
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 101, false); // CA_RESTRICT_IN_VEHICLE_AIMING_TO_CURRENT_SIDE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 102, false); // CA_UNUSED15
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 103, false); // CA_UNUSED16
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 104, false); // CA_UNUSED17
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 105, false); // CA_CAN_CRUISE_AND_BLOCK_IN_VEHICLE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 106, false); // CA_PREFER_AIR_COMBAT_WHEN_IN_AIRCRAFT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 107, false); // CA_ALLOW_DOG_FIGHTING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 108, false); // CA_PREFER_NON_AIRCRAFT_TARGETS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 109, false); // CA_PREFER_KNOWN_TARGETS_WHEN_COMBAT_CLOSEST_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 110, false); // CA_0x875B82F3
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 111, false); // CA_0x1CB77C49
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 112, false); // CA_0x8EB01547
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 114, false); // CA_CAN_EXECUTE_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 115, false); // CA_DISABLE_RETREAT_DUE_TO_TARGET_PROXIMITY
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 116, false); // CA_PREFER_DUAL_WIELD
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 117, false); // CA_WILL_CUT_FREE_HOGTIED_PEDS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 118, false); // CA_TRY_TO_FORCE_SURRENDER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 119, false); // CA_0x0136E7B6
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 120, false); // CA_0x797D7A1A
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 121, false); // CA_0x97B4A6E4
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 122, false); // CA_0x1FAAD7AF
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 123, false); // CA_0x492B880F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 124, false); // CA_0xBE151581
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 126, false); // CA_0xAC5E5497
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 127, false); // CA_0xE300164C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 128, false); // CA_0xC82D4787
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 129, false); // CA_0x31E0808F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 130, false); // CA_0x0A9A7130
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 131, false); // CA_PREVENT_UNSAFE_PROJECTILE_THROWS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 132, false); // CA_0xA55AD510
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 133, false); // CA_DISABLE_BLANK_SHOTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 134, false); // CA_0xA78BB3BD

				// === RELATIONSHIPS & HOSTILITY ===
				// set NO_RELATIONSHIP as requested (but NOT for Story Gang members)
				if (isStoryGang)
				{
					PED::SET_PED_ACCURACY(ped.GetHandle(), 95);

					// mark as Story Gang member for maintenance loop
					DECORATOR::DECOR_SET_INT(ped.GetHandle(), "SH_STORY_GANG", 1);

					// story gang relationship setup
					Hash storyGangRelationshipGroup = "REL_GANG_DUTCHS"_J; // use Dutch's gang relationship group

					// set up shared gang relationships (companion mode can override this)
					PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped.GetHandle(), storyGangRelationshipGroup);

					// make gang members friendly with each other (critical for preventing infighting)
					PED::SET_RELATIONSHIP_BETWEEN_GROUPS(0, storyGangRelationshipGroup, storyGangRelationshipGroup);
				}
				else
				{
					PED::SET_PED_ACCURACY(ped.GetHandle(), 85);
					PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped.GetHandle(), Joaat("NO_RELATIONSHIP"));
				}
			}

			ped.SetVisible(!g_Invis);

			if (g_Scale != 1.0f)
				ped.SetScale(g_Scale);

			// apply variation
			ped.SetVariation(variation);

			// apply horse gender if this is a horse (AFTER variation is set)
			if (isHorse)
			{
				SetHorseGender(ped, g_HorseGender);
			}

			// give weapon if requested and ped is not an animal
			if (giveWeapon && g_Armed && !ped.IsAnimal())
			{
				auto weapon = GetDefaultWeaponForPed(model);
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

				// === ENHANCED COMBAT ATTRIBUTES ===
				// core combat behavior - enable aggressive combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 0, true);   // CA_USE_COVER - enable tactical cover usage
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 1, true);   // CA_USE_VEHICLE - enable vehicle combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 2, true);   // CA_DO_DRIVEBYS - enable drive-by attacks
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 3, true);   // CA_LEAVE_VEHICLES - enable vehicle exit for combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 4, true);   // CA_STRAFE_BASED_ON_TARGET_PROXIMITY - enable tactical movement
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 5, true);   // CA_ALWAYS_FIGHT - enable aggressive combat stance
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 8, true);   // CA_ALLOW_STRAFE_BREAKUP - enable advanced movement
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 21, true);  // CA_CAN_CHASE_TARGET_ON_FOOT - enable pursuit
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 24, true);  // CA_USE_PROXIMITY_FIRING_RATE - enable smart firing
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 27, true);  // CA_PERFECT_ACCURACY - enable enhanced accuracy
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 31, true);  // CA_MAINTAIN_MIN_DISTANCE_TO_TARGET - enable tactical positioning
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 38, true);  // CA_DISABLE_BULLET_REACTIONS - reduce flinching
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 41, true);  // CA_CAN_COMMANDEER_VEHICLES - enable vehicle hijacking
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 42, true);  // CA_CAN_FLANK - enable flanking maneuvers
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 46, true);  // CA_CAN_FIGHT_ARMED_PEDS_WHEN_NOT_ARMED - enable unarmed combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 50, true);  // CA_CAN_CHARGE - enable charging attacks
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 57, true);  // CA_DISABLE_SEEK_DUE_TO_LINE_OF_SIGHT - ignore LOS limitations
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 58, true);  // CA_DISABLE_FLEE_FROM_COMBAT - never flee from combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 78, true);  // CA_DISABLE_ALL_RANDOMS_FLEE - disable all flee behaviors
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 93, true);  // CA_PREFER_MELEE - prefer melee combat for animals
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 113, true); // CA_USE_INFINITE_CLIPS - infinite ammo
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 114, true); // CA_CAN_EXECUTE_TARGET - enable execution moves
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 115, true); // CA_DISABLE_RETREAT_DUE_TO_TARGET_PROXIMITY - never retreat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 125, true); // CA_QUIT_WHEN_TARGET_FLEES_INTERACTION_FIGHT - continue fighting
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 91, true);  // CA_USE_RANGE_BASED_WEAPON_SELECTION

				// === DISABLED COMBAT ATTRIBUTES ===
				// disable flee behaviors - companions never flee
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 6, false);   // CA_FLEE_WHILST_IN_VEHICLE - disable vehicle fleeing
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 7, false);   // CA_JUST_FOLLOW_VEHICLE - disable passive following
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 17, false);  // CA_ALWAYS_FLEE - disable all fleeing

				// disable investigation and passive behaviors
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 9, false);   // CA_WILL_SCAN_FOR_DEAD_PEDS - focus on combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 10, false);  // CA_0x793BF941
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 11, false);  // CA_JUST_SEEK_COVER - don't just hide
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 12, false);  // CA_BLIND_FIRE_IN_COVER - use aimed shots
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 13, false);  // CA_COVER_SEARCH_IN_ARC_AWAY_FROM_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 14, false);  // CA_CAN_INVESTIGATE - focus on combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 15, false);  // CA_CAN_USE_RADIO - no radio calls
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 16, false);  // CA_STRAFE_DUE_TO_BULLET_EVENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 18, false);  // CA_0x934F1825
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 19, false);  // CA_0x70F392F0
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 20, false);  // CA_CAN_TAUNT_IN_VEHICLE - focus on combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 22, false);  // CA_WILL_DRAG_INJURED_PEDS_TO_SAFETY - focus on combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 23, false);  // CA_0x42843828
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 25, false);  // CA_DISABLE_SECONDARY_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 26, false);  // CA_DISABLE_ENTRY_REACTIONS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 28, false);  // CA_CAN_USE_FRUSTRATED_ADVANCE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 29, false);  // CA_MOVE_TO_LOCATION_BEFORE_COVER_SEARCH
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 30, false);  // CA_CAN_SHOOT_WITHOUT_LOS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 32, false);  // CA_0xBC6BB720
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 33, false);  // CA_0x8D3F251D
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 34, false);  // CA_ALLOW_PROJECTILE_SWAPS_AFTER_REPEATED_THROWS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 35, false);  // CA_DISABLE_PINNED_DOWN
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 36, false);  // CA_DISABLE_PIN_DOWN_OTHERS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 37, false);  // CA_OPEN_COMBAT_WHEN_DEFENSIVE_AREA_IS_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 39, false);  // CA_CAN_BUST - no arrests
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 40, false);  // CA_IGNORED_BY_OTHER_PEDS_WHEN_WANTED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 43, false);  // CA_SWITCH_TO_ADVANCE_IF_CANT_FIND_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 44, false);  // CA_SWITCH_TO_DEFENSIVE_IF_IN_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 45, false);  // CA_CLEAR_PRIMARY_DEFENSIVE_AREA_WHEN_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 47, false);  // CA_ENABLE_TACTICAL_POINTS_WHEN_DEFENSIVE
				// disable remaining attributes for focused combat behavior
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 48, false);  // CA_DISABLE_COVER_ARC_ADJUSTMENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 49, false);  // CA_USE_ENEMY_ACCURACY_SCALING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 51, false);  // CA_REMOVE_AREA_SET_WILL_ADVANCE_WHEN_DEFENSIVE_AREA_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 52, false);  // CA_USE_VEHICLE_ATTACK
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 53, false);  // CA_USE_VEHICLE_ATTACK_IF_VEHICLE_HAS_MOUNTED_GUNS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 54, false);  // CA_ALWAYS_EQUIP_BEST_WEAPON
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 55, false);  // CA_CAN_SEE_UNDERWATER_PEDS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 56, false);  // CA_DISABLE_AIM_AT_AI_TARGETS_IN_HELIS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 59, false);  // CA_DISABLE_TARGET_CHANGES_DURING_VEHICLE_PURSUIT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 60, false);  // CA_CAN_THROW_SMOKE_GRENADE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 61, false);  // CA_UNUSED6
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 62, false);  // CA_CLEAR_AREA_SET_DEFENSIVE_IF_DEFENSIVE_CANNOT_BE_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 63, false);  // CA_UNUSED7
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 64, false);  // CA_DISABLE_BLOCK_FROM_PURSUE_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 65, false);  // CA_DISABLE_SPIN_OUT_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 66, false);  // CA_DISABLE_CRUISE_IN_FRONT_DURING_BLOCK_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 67, false);  // CA_CAN_IGNORE_BLOCKED_LOS_WEIGHTING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 68, false);  // CA_DISABLE_REACT_TO_BUDDY_SHOT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 69, false);  // CA_PREFER_NAVMESH_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 70, false);  // CA_ALLOWED_TO_AVOID_OFFROAD_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 71, false);  // CA_PERMIT_CHARGE_BEYOND_DEFENSIVE_AREA
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 72, false);  // CA_USE_ROCKETS_AGAINST_VEHICLES_ONLY
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 73, false);  // CA_DISABLE_TACTICAL_POINTS_WITHOUT_CLEAR_LOS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 74, false);  // CA_DISABLE_PULL_ALONGSIDE_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 75, false);  // CA_0xB53C7137
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 76, false);  // CA_UNUSED8
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 77, false);  // CA_DISABLE_RESPONDED_TO_THREAT_BROADCAST
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 79, false);  // CA_WILL_GENERATE_DEAD_PED_SEEN_SCRIPT_EVENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 80, false);  // CA_UNUSED9
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 81, false);  // CA_FORCE_STRAFE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 82, false);  // CA_UNUSED10
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 83, false);  // CA_0x2060C16F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 84, false);  // CA_0x98669E6C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 85, false);  // CA_0x6E44A6F2
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 86, false);  // CA_0xC6A191DB
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 87, false);  // CA_0x57C8EF37
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 88, false);  // CA_0xA265A9FC
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 89, false);  // CA_0xE3FA8ABB
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 90, false);  // CA_0x9AA00FOF
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 92, false);  // CA_0x8AF8D68D
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 94, false);  // CA_UNUSED11
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 95, false);  // CA_UNUSED12
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 96, false);  // CA_0x64BBB208
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 97, false);  // CA_0x625F4C52
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 98, false);  // CA_0x945B1F0C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 99, false);  // CA_UNUSED13
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 100, false); // CA_UNUSED14
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 101, false); // CA_RESTRICT_IN_VEHICLE_AIMING_TO_CURRENT_SIDE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 102, false); // CA_UNUSED15
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 103, false); // CA_UNUSED16
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 104, false); // CA_UNUSED17
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 105, false); // CA_CAN_CRUISE_AND_BLOCK_IN_VEHICLE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 106, false); // CA_PREFER_AIR_COMBAT_WHEN_IN_AIRCRAFT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 107, false); // CA_ALLOW_DOG_FIGHTING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 108, false); // CA_PREFER_NON_AIRCRAFT_TARGETS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 109, false); // CA_PREFER_KNOWN_TARGETS_WHEN_COMBAT_CLOSEST_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 110, false); // CA_0x875B82F3
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 111, false); // CA_0x1CB77C49
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 112, false); // CA_0x8EB01547
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 116, false); // CA_PREFER_DUAL_WIELD
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 117, false); // CA_WILL_CUT_FREE_HOGTIED_PEDS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 118, false); // CA_TRY_TO_FORCE_SURRENDER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 119, false); // CA_0x0136E7B6
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 120, false); // CA_0x797D7A1A
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 121, false); // CA_0x97B4A6E4
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 122, false); // CA_0x1FAAD7AF
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 123, false); // CA_0x492B880F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 124, false); // CA_0xBE151581
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 126, false); // CA_0xAC5E5497
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 127, false); // CA_0xE300164C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 128, false); // CA_0xC82D4787
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 129, false); // CA_0x31E0808F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 130, false); // CA_0x0A9A7130
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 131, false); // CA_PREVENT_UNSAFE_PROJECTILE_THROWS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 132, false); // CA_0xA55AD510
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 133, false); // CA_DISABLE_BLANK_SHOTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 134, false); // CA_0xA78BB3BD

				// === ENHANCED GROUP AND RELATIONSHIP SETUP ===
				ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped.GetHandle(), true, true);
				PED::SET_PED_AS_GROUP_MEMBER(ped.GetHandle(), group);
				PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(ped.GetHandle(), YimMenu::Self::GetPlayer().GetId(), false);

				// === ENHANCED RELATIONSHIP SETUP ===
				// set companion to same relationship group as player
				PED::SET_PED_RELATIONSHIP_GROUP_HASH(
				    ped.GetHandle(), PED::GET_PED_RELATIONSHIP_GROUP_HASH(YimMenu::Self::GetPed().GetHandle()));

				// ensure positive relationship with player
				auto playerGroup = PED::GET_PED_RELATIONSHIP_GROUP_HASH(YimMenu::Self::GetPed().GetHandle());
				auto companionGroup = PED::GET_PED_RELATIONSHIP_GROUP_HASH(ped.GetHandle());

				// set mutual respect/like relationship
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, companionGroup, playerGroup); // LIKE
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, playerGroup, companionGroup); // LIKE

				// enhanced group formation and coordination
				PED::SET_GROUP_FORMATION(PED::GET_PED_GROUP_INDEX(ped.GetHandle()), g_Formation);
				PED::SET_GROUP_FORMATION_SPACING(PED::GET_PED_GROUP_INDEX(ped.GetHandle()), 1.0f, 1.0f, 1.0f);

				// mark as companion for tracking
				DECORATOR::DECOR_SET_INT(ped.GetHandle(), "SH_CMP_companion", 2);

				// === ENHANCED ANIMAL-SPECIFIC LOGIC ===
				if (ped.IsAnimal())
				{
					// completely disable all flee behaviors for animals
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 104, 0.0f); // flee_distance_base
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 105, 0.0f); // flee_distance_random
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 10, 0.0f);  // flee_speed_modifier
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 146, 0.0f); // flee_stamina_cost
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 113, 0.0f); // flee_from_gunshot_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 114, 0.0f); // flee_from_explosion_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 115, 0.0f); // flee_from_fire_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 116, 0.0f); // flee_from_predator_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 117, 0.0f); // flee_from_human_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 118, 0.0f); // flee_from_vehicle_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 119, 0.0f); // flee_from_horse_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 111, 0.0f); // flee_threshold
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 107, 0.0f); // general_flee_modifier

					// enhance aggression and combat for animals
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 120, 100.0f); // aggression_level
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 121, 100.0f); // combat_effectiveness
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 122, 0.0f);   // fear_level (no fear)
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 123, 100.0f); // territorial_behavior
				}

				// allow dynamic event responses for better AI behavior
				PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped.GetHandle(), false);

				// === ENHANCED FLEE ATTRIBUTES - DISABLE ALL FLEE BEHAVIORS ===
				// disable vehicle-related flee behaviors
				PED::SET_PED_FLEE_ATTRIBUTES(ped.GetHandle(), 65536, false);   // FA_FORCE_EXIT_VEHICLE - don't force exit
				PED::SET_PED_FLEE_ATTRIBUTES(ped.GetHandle(), 4194304, false); // FA_DISABLE_ENTER_VEHICLES - allow vehicle use
				PED::SET_PED_FLEE_ATTRIBUTES(ped.GetHandle(), 1048576, false); // FA_DISABLE_MOUNT_USAGE - allow mount usage

				// set comprehensive flee attributes to disable all flee behaviors
				PED::SET_PED_FLEE_ATTRIBUTES(ped.GetHandle(), 0, false);       // disable all base flee behaviors

				// === ENHANCED CONFIG FLAGS FOR COMPANIONS ===
				// core companion behavior flags
				ped.SetConfigFlag(PedConfigFlag::_0x16A14D9A, false);              // enable normal behavior
				ped.SetConfigFlag(PedConfigFlag::_DisableHorseFleeILO, true);      // disable horse flee
				ped.SetConfigFlag(PedConfigFlag::_0x74F95F2E, false);              // enable combat reactions
				ped.SetConfigFlag(PedConfigFlag::NeverLeavesGroup, true);          // stay with group
				ped.SetConfigFlag(PedConfigFlag::DisableHorseGunshotFleeResponse, true); // disable gunshot flee

				// enhanced combat and reaction flags
				ped.SetConfigFlag(PedConfigFlag::CanAttackFriendly, false);        // don't attack friendlies
				ped.SetConfigFlag(PedConfigFlag::AlwaysRespondToCriesForHelp, true); // respond to help calls
				ped.SetConfigFlag(PedConfigFlag::TreatAsPlayerDuringTargeting, false); // don't treat as player target
				ped.SetConfigFlag(PedConfigFlag::CowerInsteadOfFlee, false);       // fight instead of cowering
				ped.SetConfigFlag(PedConfigFlag::RunFromFiresAndExplosions, false); // don't flee from explosions

				// avoidance and movement flags
				ped.SetConfigFlag(PedConfigFlag::Avoidance_Ignore_All, false);     // enable smart avoidance
				ped.SetConfigFlag(PedConfigFlag::DisableShockingEvents, false);    // allow shock reactions
				ped.SetConfigFlag(PedConfigFlag::DisablePedAvoidance, false);      // enable ped avoidance
				ped.SetConfigFlag(PedConfigFlag::DisableExplosionReactions, false); // allow explosion reactions
				ped.SetConfigFlag(PedConfigFlag::DisableEvasiveStep, false);       // enable evasive movement
				ped.SetConfigFlag(PedConfigFlag::AlwaysSeeApproachingVehicles, true); // enhanced awareness
				ped.SetConfigFlag(PedConfigFlag::CanDiveAwayFromApproachingVehicles, true); // evasive maneuvers

				// group and relationship flags
				ped.SetConfigFlag(PedConfigFlag::KeepRelationshipGroupAfterCleanUp, true); // maintain relationships
				ped.SetConfigFlag(PedConfigFlag::DontEnterVehiclesInPlayersGroup, false); // allow vehicle entry

				// === ENHANCED AI BEHAVIOR SETTINGS ===
				// enhanced following behavior
				PED::SET_PED_KEEP_TASK(ped.GetHandle(), true);

				// enhanced combat coordination
				PED::SET_PED_COMBAT_RANGE(ped.GetHandle(), 2); // medium range combat
				PED::SET_PED_COMBAT_MOVEMENT(ped.GetHandle(), 2); // aggressive movement
				PED::SET_PED_COMBAT_ABILITY(ped.GetHandle(), 2); // professional combat ability
				if (isStoryGang)
				{
					PED::SET_PED_ACCURACY(ped.GetHandle(), 95); // legendary combat ability
				}
				else
				{
					PED::SET_PED_ACCURACY(ped.GetHandle(), 85); // high accuracy
				}
				
				// create companion blip for tracking
				auto blip = MAP::BLIP_ADD_FOR_ENTITY("BLIP_STYLE_COMPANION"_J, ped.GetHandle());
				MAP::BLIP_ADD_MODIFIER(blip, "BLIP_MODIFIER_COMPANION_DOG"_J);
			}
		});
	}

	// convenience wrapper for animal spawning (no weapons)
	static void SpawnAnimal(const std::string& model, int variation, bool isHorse = false)
	{
		SpawnPed(model, variation, false, false, isHorse);
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

	// horse data structure
	struct Horse
	{
		std::string name;
		std::string model;
		std::string section;
		int variation;
	};

	// horse data (parsed from horses.txt)
	static std::vector<Horse> g_AmericanPaintHorses = {
		{"Grey Overo", "A_C_HORSE_AMERICANPAINT_GREYOVERO", "American Paint", 0},
		{"Overo", "A_C_HORSE_AMERICANPAINT_OVERO", "American Paint", 0},
		{"Splashed White", "A_C_HORSE_AMERICANPAINT_SPLASHEDWHITE", "American Paint", 0},
		{"Tobiano", "A_C_HORSE_AMERICANPAINT_TOBIANO", "American Paint", 0}
	};

	static std::vector<Horse> g_AmericanStandardbredHorses = {
		{"Black", "A_C_HORSE_AMERICANSTANDARDBRED_BLACK", "American Standardbred", 0},
		{"Buckskin", "A_C_HORSE_AMERICANSTANDARDBRED_BUCKSKIN", "American Standardbred", 0},
		{"Light-Buckskin", "A_C_HORSE_AMERICANSTANDARDBRED_LIGHTBUCKSKIN", "American Standardbred", 0},
		{"Palomino Dapple", "A_C_HORSE_AMERICANSTANDARDBRED_PALOMINODAPPLE", "American Standardbred", 0},
		{"Silver Tail Buckskin", "A_C_HORSE_AMERICANSTANDARDBRED_SILVERTAILBUCKSKIN", "American Standardbred", 0}
	};

	static std::vector<Horse> g_AndalusianHorses = {
		{"Dark Bay", "A_C_HORSE_ANDALUSIAN_DARKBAY", "Andalusian", 0},
		{"Perlino", "A_C_HORSE_ANDALUSIAN_PERLINO", "Andalusian", 0},
		{"Rose Gray", "A_C_HORSE_ANDALUSIAN_ROSEGRAY", "Andalusian", 0}
	};

	static std::vector<Horse> g_AppaloosaHorses = {
		{"Black Snowflake", "A_C_HORSE_APPALOOSA_BLACKSNOWFLAKE", "Appaloosa", 0},
		{"Blanket", "A_C_HORSE_APPALOOSA_BLANKET", "Appaloosa", 0},
		{"Brown Leopard", "A_C_HORSE_APPALOOSA_BROWNLEOPARD", "Appaloosa", 0},
		{"Few-Spotted PC", "A_C_HORSE_APPALOOSA_FEWSPOTTED_PC", "Appaloosa", 0},
		{"Leopard", "A_C_HORSE_APPALOOSA_LEOPARD", "Appaloosa", 0},
		{"Leopard Blanket", "A_C_HORSE_APPALOOSA_LEOPARDBLANKET", "Appaloosa", 0}
	};

	static std::vector<Horse> g_ArabianHorses = {
		{"Black", "A_C_HORSE_ARABIAN_BLACK", "Arabian", 0},
		{"Grey", "A_C_HORSE_ARABIAN_GREY", "Arabian", 0},
		{"Red Chestnut", "A_C_HORSE_ARABIAN_REDCHESTNUT", "Arabian", 0},
		{"Red Chestnut PC", "A_C_HORSE_ARABIAN_REDCHESTNUT_PC", "Arabian", 0},
		{"Rose Grey Bay", "A_C_HORSE_ARABIAN_ROSEGREYBAY", "Arabian", 0},
		{"Warped Brindle", "A_C_HORSE_ARABIAN_WARPEDBRINDLE", "Arabian", 0},
		{"White", "A_C_HORSE_ARABIAN_WHITE", "Arabian", 0}
	};

	static std::vector<Horse> g_ArdennesHorses = {
		{"Bay Roan", "A_C_HORSE_ARDENNES_BAYROAN", "Ardennes", 0},
		{"Iron Grey Roan", "A_C_HORSE_ARDENNES_IRONGREYROAN", "Ardennes", 0},
		{"Strawberry Roan", "A_C_HORSE_ARDENNES_STRAWBERRYROAN", "Ardennes", 0}
	};

	static std::vector<Horse> g_BelgianHorses = {
		{"Blonde Chestnut", "A_C_HORSE_BELGIAN_BLONDCHESTNUT", "Belgian", 0},
		{"Mealy Chestnut", "A_C_HORSE_BELGIAN_MEALYCHESTNUT", "Belgian", 0}
	};

	static std::vector<Horse> g_BretonHorses = {
		{"Grullo Dun", "A_C_HORSE_BRETON_GRULLODUN", "Breton", 0},
		{"Mealy Dapple Bay", "A_C_HORSE_BRETON_MEALYDAPPLEBAY", "Breton", 0},
		{"Red Roan", "A_C_HORSE_BRETON_REDROAN", "Breton", 0},
		{"Seal Brown", "A_C_HORSE_BRETON_SEALBROWN", "Breton", 0},
		{"Sorrel", "A_C_HORSE_BRETON_SORREL", "Breton", 0},
		{"Steel Grey", "A_C_HORSE_BRETON_STEELGREY", "Breton", 0}
	};

	static std::vector<Horse> g_CriolloHorses = {
		{"Bay Brindle", "A_C_HORSE_CRIOLLO_BAYBRINDLE", "Criollo", 0},
		{"Bay Frame Overo", "A_C_HORSE_CRIOLLO_BAYFRAMEOVERO", "Criollo", 0},
		{"Blue Roan Overo", "A_C_HORSE_CRIOLLO_BLUEROANOVERO", "Criollo", 0},
		{"Dun", "A_C_HORSE_CRIOLLO_DUN", "Criollo", 0},
		{"Marble Sabino", "A_C_HORSE_CRIOLLO_MARBLESABINO", "Criollo", 0},
		{"Sorrel Overo", "A_C_HORSE_CRIOLLO_SORRELOVERO", "Criollo", 0}
	};

	static std::vector<Horse> g_DutchWarmbloodHorses = {
		{"Chocolate Roan", "A_C_HORSE_DUTCHWARMBLOOD_CHOCOLATEROAN", "Dutch Warmblood", 0},
		{"Seal Brown", "A_C_HORSE_DUTCHWARMBLOOD_SEALBROWN", "Dutch Warmblood", 0},
		{"Sooty Buckskin", "A_C_HORSE_DUTCHWARMBLOOD_SOOTYBUCKSKIN", "Dutch Warmblood", 0}
	};

	static std::vector<Horse> g_GangHorses = {
		{"Bill's Horse", "A_C_HORSE_GANG_BILL", "Gang", 0},
		{"Charles' Horse 1889", "A_C_HORSE_GANG_CHARLES", "Gang", 0},
		{"Charles' Horse 1907", "A_C_HORSE_GANG_CHARLES_ENDLESSSUMMER", "Gang", 0},
		{"Dutch's Horse", "A_C_HORSE_GANG_DUTCH", "Gang", 0},
		{"Hosea's Horse", "A_C_HORSE_GANG_HOSEA", "Gang", 0},
		{"Javier's Horse", "A_C_HORSE_GANG_JAVIER", "Gang", 0},
		{"John's Horse 1889", "A_C_HORSE_GANG_JOHN", "Gang", 0},
		{"John's Horse 1907", "A_C_HORSE_JOHN_ENDLESSSUMMER", "Gang", 0},
		{"Karen's Horse", "A_C_HORSE_GANG_KAREN", "Gang", 0},
		{"Kieran's Horse", "A_C_HORSE_GANG_KIERAN", "Gang", 0},
		{"Lenny's Horse", "A_C_HORSE_GANG_LENNY", "Gang", 0},
		{"Micah's Horse", "A_C_HORSE_GANG_MICAH", "Gang", 0},
		{"Sadie's Horse 1889", "A_C_HORSE_GANG_SADIE", "Gang", 0},
		{"Sadie's Horse 1907", "A_C_HORSE_GANG_SADIE_ENDLESSSUMMER", "Gang", 0},
		{"Sean's Horse", "A_C_HORSE_GANG_SEAN", "Gang", 0},
		{"Trelawney's Horse", "A_C_HORSE_GANG_TRELAWNEY", "Gang", 0},
		{"Uncle's Horse 1889", "A_C_HORSE_GANG_UNCLE", "Gang", 0},
		{"Uncle's Horse 1907", "A_C_HORSE_GANG_UNCLE_ENDLESSSUMMER", "Gang", 0},
		{"EagleFlies' Horse", "A_C_HORSE_EAGLEFLIES", "Gang", 0},
		{"Buell (Special)", "A_C_HORSE_BUELL_WARVETS", "Gang", 0}
	};

	static std::vector<Horse> g_GypsyCobHorses = {
		{"Polomino Blagdon", "A_C_HORSE_GYPSYCOB_PALOMINOBLAGDON", "Gypsy Cob", 0},
		{"Piebald", "A_C_HORSE_GYPSYCOB_PIEBALD", "Gypsy Cob", 0},
		{"Skewbald", "A_C_HORSE_GYPSYCOB_SKEWBALD", "Gypsy Cob", 0},
		{"Splashed Bay", "A_C_HORSE_GYPSYCOB_SPLASHEDBAY", "Gypsy Cob", 0},
		{"Splashed Piedbald", "A_C_HORSE_GYPSYCOB_SPLASHEDPIEBALD", "Gypsy Cob", 0},
		{"White Blagdon", "A_C_HORSE_GYPSYCOB_WHITEBLAGDON", "Gypsy Cob", 0}
	};

	static std::vector<Horse> g_HungarianHalfbredHorses = {
		{"Dapple Dark Gray", "A_C_HORSE_HUNGARIANHALFBRED_DARKDAPPLEGREY", "Hungarian Halfbred", 0},
		{"Flaxen Chestnut", "A_C_HORSE_HUNGARIANHALFBRED_FLAXENCHESTNUT", "Hungarian Halfbred", 0},
		{"Liver Chestnut", "A_C_HORSE_HUNGARIANHALFBRED_LIVERCHESTNUT", "Hungarian Halfbred", 0},
		{"Piebald Tobiano", "A_C_HORSE_HUNGARIANHALFBRED_PIEBALDTOBIANO", "Hungarian Halfbred", 0}
	};

	static std::vector<Horse> g_KentuckySaddlerHorses = {
		{"Black", "A_C_HORSE_KENTUCKYSADDLE_BLACK", "Kentucky Saddler", 0},
		{"Buttermilk Buckskin PC", "A_C_HORSE_KENTUCKYSADDLE_BUTTERMILKBUCKSKIN_PC", "Kentucky Saddler", 0},
		{"Chestnut Pinto", "A_C_HORSE_KENTUCKYSADDLE_CHESTNUTPINTO", "Kentucky Saddler", 0},
		{"Grey", "A_C_HORSE_KENTUCKYSADDLE_GREY", "Kentucky Saddler", 0},
		{"Silver Bay", "A_C_HORSE_KENTUCKYSADDLE_SILVERBAY", "Kentucky Saddler", 0}
	};

	static std::vector<Horse> g_KlardruberHorses = {
		{"Black", "A_C_HORSE_KLADRUBER_BLACK", "Klardruber", 0},
		{"Cremello", "A_C_HORSE_KLADRUBER_CREMELLO", "Klardruber", 0},
		{"Dapple Rose Grey", "A_C_HORSE_KLADRUBER_DAPPLEROSEGREY", "Klardruber", 0},
		{"Grey", "A_C_HORSE_KLADRUBER_GREY", "Klardruber", 0},
		{"Silver", "A_C_HORSE_KLADRUBER_SILVER", "Klardruber", 0},
		{"White", "A_C_HORSE_KLADRUBER_WHITE", "Klardruber", 0}
	};

	static std::vector<Horse> g_MissouriFoxTrotterHorses = {
		{"Amber Champagne", "A_C_HORSE_MISSOURIFOXTROTTER_AMBERCHAMPAGNE", "Missouri Fox Trotter", 0},
		{"Black Tovero", "A_C_HORSE_MISSOURIFOXTROTTER_BLACKTOVERO", "Missouri Fox Trotter", 0},
		{"Blue Roan", "A_C_HORSE_MISSOURIFOXTROTTER_BLUEROAN", "Missouri Fox Trotter", 0},
		{"Buckskin Brindle", "A_C_HORSE_MISSOURIFOXTROTTER_BUCKSKINBRINDLE", "Missouri Fox Trotter", 0},
		{"Dapple Grey", "A_C_HORSE_MISSOURIFOXTROTTER_DAPPLEGREY", "Missouri Fox Trotter", 0},
		{"Sable Champagne", "A_C_HORSE_MISSOURIFOXTROTTER_SABLECHAMPAGNE", "Missouri Fox Trotter", 0},
		{"Silver Dapple Pinto", "A_C_HORSE_MISSOURIFOXTROTTER_SILVERDAPPLEPINTO", "Missouri Fox Trotter", 0}
	};

	static std::vector<Horse> g_MorganHorses = {
		{"Bay", "A_C_HORSE_MORGAN_BAY", "Morgan", 0},
		{"Bay Roan", "A_C_HORSE_MORGAN_BAYROAN", "Morgan", 0},
		{"Flaxen Chestnut", "A_C_HORSE_MORGAN_FLAXENCHESTNUT", "Morgan", 0},
		{"Liver Chestnut PC", "A_C_HORSE_MORGAN_LIVERCHESTNUT_PC", "Morgan", 0},
		{"Palomino", "A_C_HORSE_MORGAN_PALOMINO", "Morgan", 0}
	};

	static std::vector<Horse> g_MustangHorses = {
		{"Black Overo", "A_C_HORSE_MUSTANG_BLACKOVERO", "Mustang", 0},
		{"Buckskin", "A_C_HORSE_MUSTANG_BUCKSKIN", "Mustang", 0},
		{"Chestnut Tovero", "A_C_HORSE_MUSTANG_CHESTNUTTOVERO", "Mustang", 0},
		{"Golden Dun", "A_C_HORSE_MUSTANG_GOLDENDUN", "Mustang", 0},
		{"Grullo Dun", "A_C_HORSE_MUSTANG_GRULLODUN", "Mustang", 0},
		{"Red Dun Overo", "A_C_HORSE_MUSTANG_REDDUNOVERO", "Mustang", 0},
		{"Tigerstriped Bay", "A_C_HORSE_MUSTANG_TIGERSTRIPEDBAY", "Mustang", 0},
		{"Wild Bay", "A_C_HORSE_MUSTANG_WILDBAY", "Mustang", 0}
	};

	static std::vector<Horse> g_NokotaHorses = {
		{"Blue Roan", "A_C_HORSE_NOKOTA_BLUEROAN", "Nokota", 0},
		{"Reverse Dapple Roan", "A_C_HORSE_NOKOTA_REVERSEDAPPLEROAN", "Nokota", 0},
		{"White Roan", "A_C_HORSE_NOKOTA_WHITEROAN", "Nokota", 0}
	};

	static std::vector<Horse> g_NorfolkRoadsterHorses = {
		{"Black", "A_C_HORSE_NORFOLKROADSTER_BLACK", "Norfolk Roadster", 0},
		{"Dappled Buckskin", "A_C_HORSE_NORFOLKROADSTER_DAPPLEDBUCKSKIN", "Norfolk Roadster", 0},
		{"Piebald Roan", "A_C_HORSE_NORFOLKROADSTER_PIEBALDROAN", "Norfolk Roadster", 0},
		{"Rose Gray", "A_C_HORSE_NORFOLKROADSTER_ROSEGREY", "Norfolk Roadster", 0},
		{"Speckled Gray", "A_C_HORSE_NORFOLKROADSTER_SPECKLEDGREY", "Norfolk Roadster", 0},
		{"Spotted Tricolor", "A_C_HORSE_NORFOLKROADSTER_SPOTTEDTRICOLOR", "Norfolk Roadster", 0}
	};

	static std::vector<Horse> g_ShireHorses = {
		{"Dark Bay", "A_C_HORSE_SHIRE_DARKBAY", "Shire", 0},
		{"Light Grey", "A_C_HORSE_SHIRE_LIGHTGREY", "Shire", 0},
		{"Raven Black", "A_C_HORSE_SHIRE_RAVENBLACK", "Shire", 0}
	};

	static std::vector<Horse> g_SuffolkPunchHorses = {
		{"Red Chestnut", "A_C_HORSE_SUFFOLKPUNCH_REDCHESTNUT", "Suffolk Punch", 0},
		{"Sorrel", "A_C_HORSE_SUFFOLKPUNCH_SORREL", "Suffolk Punch", 0}
	};

	static std::vector<Horse> g_TennesseeWalkerHorses = {
		{"Black Rabicano", "A_C_HORSE_TENNESSEEWALKER_BLACKRABICANO", "Tennessee Walker", 0},
		{"Chestnut", "A_C_HORSE_TENNESSEEWALKER_CHESTNUT", "Tennessee Walker", 0},
		{"Dapple Bay", "A_C_HORSE_TENNESSEEWALKER_DAPPLEBAY", "Tennessee Walker", 0},
		{"Flaxen Roan", "A_C_HORSE_TENNESSEEWALKER_FLAXENROAN", "Tennessee Walker", 0},
		{"Gold Palomino PC", "A_C_HORSE_TENNESSEEWALKER_GOLDPALOMINO_PC", "Tennessee Walker", 0},
		{"Mahogany Bay", "A_C_HORSE_TENNESSEEWALKER_MAHOGANYBAY", "Tennessee Walker", 0},
		{"Red Roan", "A_C_HORSE_TENNESSEEWALKER_REDROAN", "Tennessee Walker", 0}
	};

	static std::vector<Horse> g_ThoroughbredHorses = {
		{"Black Chestnut", "A_C_HORSE_THOROUGHBRED_BLACKCHESTNUT", "Thoroughbred", 0},
		{"Blood Bay", "A_C_HORSE_THOROUGHBRED_BLOODBAY", "Thoroughbred", 0},
		{"Brindle", "A_C_HORSE_THOROUGHBRED_BRINDLE", "Thoroughbred", 0},
		{"Dapple Gray", "A_C_HORSE_THOROUGHBRED_DAPPLEGREY", "Thoroughbred", 0},
		{"Reverse Dapple Black", "A_C_HORSE_THOROUGHBRED_REVERSEDAPPLEBLACK", "Thoroughbred", 0}
	};

	static std::vector<Horse> g_TurkomanHorses = {
		{"Black", "A_C_HORSE_TURKOMAN_BLACK", "Turkoman", 0},
		{"Chestnut", "A_C_HORSE_TURKOMAN_CHESTNUT", "Turkoman", 0},
		{"Dark Bay", "A_C_HORSE_TURKOMAN_DARKBAY", "Turkoman", 0},
		{"Gold", "A_C_HORSE_TURKOMAN_GOLD", "Turkoman", 0},
		{"Grey", "A_C_HORSE_TURKOMAN_GREY", "Turkoman", 0},
		{"Perlino", "A_C_HORSE_TURKOMAN_PERLINO", "Turkoman", 0},
		{"Silver", "A_C_HORSE_TURKOMAN_SILVER", "Turkoman", 0}
	};

	static std::vector<Horse> g_MiscellaneousHorses = {
		{"Donkey", "A_C_Donkey_01", "Miscellaneous", 0},
		{"Scrawny Nag", "A_C_HORSE_MP_MANGY_BACKUP", "Miscellaneous", 0},
		{"Mule", "A_C_HORSEMULE_01", "Miscellaneous", 0},
		{"Mule Painted", "A_C_HORSEMULEPAINTED_01", "Miscellaneous", 0},
		{"Murfree Blanket", "A_C_HORSE_MURFREEBROOD_MANGE_01", "Miscellaneous", 0},
		{"Murfree Blue Roan", "A_C_HORSE_MURFREEBROOD_MANGE_02", "Miscellaneous", 0},
		{"Murfree Black Rabicano", "A_C_HORSE_MURFREEBROOD_MANGE_03", "Miscellaneous", 0},
		{"Horse Winter", "A_C_HORSE_WINTER02_01", "Miscellaneous", 0},
		{"Unknown PC Horse", "P_C_HORSE_01", "Miscellaneous", 0},
		{"RDO Special Arabian", "MP_A_C_HORSECORPSE_01", "Miscellaneous", 0},
		{"RDO OwlHoot Victim", "MP_HORSE_OWLHOOTVICTIM_01", "Miscellaneous", 0}
	};

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

		// helper function for centered separator with custom line width
		auto RenderCenteredSeparator = [](const char* text) {
			ImGui::PushFont(Menu::Font::g_ChildTitleFont);
			ImVec2 textSize = ImGui::CalcTextSize(text);
			ImVec2 contentRegion = ImGui::GetContentRegionAvail();

			// center text
			ImGui::SetCursorPosX((contentRegion.x - textSize.x) * 0.5f);
			ImGui::Text(text);
			ImGui::PopFont();

			// draw centered line (3x text width) using screen coordinates
			float lineWidth = textSize.x * 3.0f;
			float linePosX = (contentRegion.x - lineWidth) * 0.5f;

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 windowPos = ImGui::GetWindowPos();
			ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();

			// use screen coordinates for proper positioning with scrolling
			drawList->AddLine(
				ImVec2(windowPos.x + linePosX, cursorScreenPos.y),
				ImVec2(windowPos.x + linePosX + lineWidth, cursorScreenPos.y),
				ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);

			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
			ImGui::Spacing();
		};

		// lambda function for getting horse names
		auto getHorseName = [](const Horse& horse) { return horse.name; };

		// calculate totals for all horse sections
		int totalHorses = static_cast<int>(g_AmericanPaintHorses.size() + g_AmericanStandardbredHorses.size() +
		                                  g_AndalusianHorses.size() + g_AppaloosaHorses.size() + g_ArabianHorses.size() +
		                                  g_ArdennesHorses.size() + g_BelgianHorses.size() + g_BretonHorses.size() +
		                                  g_CriolloHorses.size() + g_DutchWarmbloodHorses.size() + g_GangHorses.size() +
		                                  g_GypsyCobHorses.size() + g_HungarianHalfbredHorses.size() + g_KentuckySaddlerHorses.size() +
		                                  g_KlardruberHorses.size() + g_MissouriFoxTrotterHorses.size() + g_MorganHorses.size() +
		                                  g_MustangHorses.size() + g_NokotaHorses.size() + g_NorfolkRoadsterHorses.size() +
		                                  g_ShireHorses.size() + g_SuffolkPunchHorses.size() + g_TennesseeWalkerHorses.size() +
		                                  g_ThoroughbredHorses.size() + g_TurkomanHorses.size() + g_MiscellaneousHorses.size());

		// check section matches for search
		bool americanPaintMatches = g_HorseSearch.SectionMatches("American Paint", g_HorseSearch.searchBuffer);
		bool americanStandardbredMatches = g_HorseSearch.SectionMatches("American Standardbred", g_HorseSearch.searchBuffer);
		bool andalusianMatches = g_HorseSearch.SectionMatches("Andalusian", g_HorseSearch.searchBuffer);
		bool appaloosaMatches = g_HorseSearch.SectionMatches("Appaloosa", g_HorseSearch.searchBuffer);
		bool arabianMatches = g_HorseSearch.SectionMatches("Arabian", g_HorseSearch.searchBuffer);
		bool ardennesMatches = g_HorseSearch.SectionMatches("Ardennes", g_HorseSearch.searchBuffer);
		bool belgianMatches = g_HorseSearch.SectionMatches("Belgian", g_HorseSearch.searchBuffer);
		bool bretonMatches = g_HorseSearch.SectionMatches("Breton", g_HorseSearch.searchBuffer);
		bool criolloMatches = g_HorseSearch.SectionMatches("Criollo", g_HorseSearch.searchBuffer);
		bool dutchWarmbloodMatches = g_HorseSearch.SectionMatches("Dutch Warmblood", g_HorseSearch.searchBuffer);
		bool gangMatches = g_HorseSearch.SectionMatches("Gang", g_HorseSearch.searchBuffer);
		bool gypsyCobMatches = g_HorseSearch.SectionMatches("Gypsy Cob", g_HorseSearch.searchBuffer);
		bool hungarianHalfbredMatches = g_HorseSearch.SectionMatches("Hungarian Halfbred", g_HorseSearch.searchBuffer);
		bool kentuckySaddlerMatches = g_HorseSearch.SectionMatches("Kentucky Saddler", g_HorseSearch.searchBuffer);
		bool klardruberMatches = g_HorseSearch.SectionMatches("Klardruber", g_HorseSearch.searchBuffer);
		bool missouriFoxTrotterMatches = g_HorseSearch.SectionMatches("Missouri Fox Trotter", g_HorseSearch.searchBuffer);
		bool morganMatches = g_HorseSearch.SectionMatches("Morgan", g_HorseSearch.searchBuffer);
		bool mustangMatches = g_HorseSearch.SectionMatches("Mustang", g_HorseSearch.searchBuffer);
		bool nokotaMatches = g_HorseSearch.SectionMatches("Nokota", g_HorseSearch.searchBuffer);
		bool norfolkRoadsterMatches = g_HorseSearch.SectionMatches("Norfolk Roadster", g_HorseSearch.searchBuffer);
		bool shireMatches = g_HorseSearch.SectionMatches("Shire", g_HorseSearch.searchBuffer);
		bool suffolkPunchMatches = g_HorseSearch.SectionMatches("Suffolk Punch", g_HorseSearch.searchBuffer);
		bool tennesseeWalkerMatches = g_HorseSearch.SectionMatches("Tennessee Walker", g_HorseSearch.searchBuffer);
		bool thoroughbredMatches = g_HorseSearch.SectionMatches("Thoroughbred", g_HorseSearch.searchBuffer);
		bool turkomanMatches = g_HorseSearch.SectionMatches("Turkoman", g_HorseSearch.searchBuffer);
		bool miscellaneousMatches = g_HorseSearch.SectionMatches("Miscellaneous", g_HorseSearch.searchBuffer);

		// count visible horses in each section
		int americanPaintVisible = americanPaintMatches ? static_cast<int>(g_AmericanPaintHorses.size()) :
		                          g_HorseSearch.CountMatches(g_AmericanPaintHorses, g_HorseSearch.searchBuffer, getHorseName);
		int americanStandardbredVisible = americanStandardbredMatches ? static_cast<int>(g_AmericanStandardbredHorses.size()) :
		                                 g_HorseSearch.CountMatches(g_AmericanStandardbredHorses, g_HorseSearch.searchBuffer, getHorseName);
		int andalusianVisible = andalusianMatches ? static_cast<int>(g_AndalusianHorses.size()) :
		                       g_HorseSearch.CountMatches(g_AndalusianHorses, g_HorseSearch.searchBuffer, getHorseName);
		int appaloosaVisible = appaloosaMatches ? static_cast<int>(g_AppaloosaHorses.size()) :
		                      g_HorseSearch.CountMatches(g_AppaloosaHorses, g_HorseSearch.searchBuffer, getHorseName);
		int arabianVisible = arabianMatches ? static_cast<int>(g_ArabianHorses.size()) :
		                    g_HorseSearch.CountMatches(g_ArabianHorses, g_HorseSearch.searchBuffer, getHorseName);
		int ardennesVisible = ardennesMatches ? static_cast<int>(g_ArdennesHorses.size()) :
		                     g_HorseSearch.CountMatches(g_ArdennesHorses, g_HorseSearch.searchBuffer, getHorseName);
		int belgianVisible = belgianMatches ? static_cast<int>(g_BelgianHorses.size()) :
		                    g_HorseSearch.CountMatches(g_BelgianHorses, g_HorseSearch.searchBuffer, getHorseName);
		int bretonVisible = bretonMatches ? static_cast<int>(g_BretonHorses.size()) :
		                   g_HorseSearch.CountMatches(g_BretonHorses, g_HorseSearch.searchBuffer, getHorseName);
		int criolloVisible = criolloMatches ? static_cast<int>(g_CriolloHorses.size()) :
		                    g_HorseSearch.CountMatches(g_CriolloHorses, g_HorseSearch.searchBuffer, getHorseName);
		int dutchWarmbloodVisible = dutchWarmbloodMatches ? static_cast<int>(g_DutchWarmbloodHorses.size()) :
		                           g_HorseSearch.CountMatches(g_DutchWarmbloodHorses, g_HorseSearch.searchBuffer, getHorseName);
		int gangVisible = gangMatches ? static_cast<int>(g_GangHorses.size()) :
		                 g_HorseSearch.CountMatches(g_GangHorses, g_HorseSearch.searchBuffer, getHorseName);
		int gypsyCobVisible = gypsyCobMatches ? static_cast<int>(g_GypsyCobHorses.size()) :
		                     g_HorseSearch.CountMatches(g_GypsyCobHorses, g_HorseSearch.searchBuffer, getHorseName);
		int hungarianHalfbredVisible = hungarianHalfbredMatches ? static_cast<int>(g_HungarianHalfbredHorses.size()) :
		                              g_HorseSearch.CountMatches(g_HungarianHalfbredHorses, g_HorseSearch.searchBuffer, getHorseName);
		int kentuckySaddlerVisible = kentuckySaddlerMatches ? static_cast<int>(g_KentuckySaddlerHorses.size()) :
		                            g_HorseSearch.CountMatches(g_KentuckySaddlerHorses, g_HorseSearch.searchBuffer, getHorseName);
		int klardruberVisible = klardruberMatches ? static_cast<int>(g_KlardruberHorses.size()) :
		                       g_HorseSearch.CountMatches(g_KlardruberHorses, g_HorseSearch.searchBuffer, getHorseName);
		int missouriFoxTrotterVisible = missouriFoxTrotterMatches ? static_cast<int>(g_MissouriFoxTrotterHorses.size()) :
		                               g_HorseSearch.CountMatches(g_MissouriFoxTrotterHorses, g_HorseSearch.searchBuffer, getHorseName);
		int morganVisible = morganMatches ? static_cast<int>(g_MorganHorses.size()) :
		                   g_HorseSearch.CountMatches(g_MorganHorses, g_HorseSearch.searchBuffer, getHorseName);
		int mustangVisible = mustangMatches ? static_cast<int>(g_MustangHorses.size()) :
		                    g_HorseSearch.CountMatches(g_MustangHorses, g_HorseSearch.searchBuffer, getHorseName);
		int nokotaVisible = nokotaMatches ? static_cast<int>(g_NokotaHorses.size()) :
		                   g_HorseSearch.CountMatches(g_NokotaHorses, g_HorseSearch.searchBuffer, getHorseName);
		int norfolkRoadsterVisible = norfolkRoadsterMatches ? static_cast<int>(g_NorfolkRoadsterHorses.size()) :
		                            g_HorseSearch.CountMatches(g_NorfolkRoadsterHorses, g_HorseSearch.searchBuffer, getHorseName);
		int shireVisible = shireMatches ? static_cast<int>(g_ShireHorses.size()) :
		                  g_HorseSearch.CountMatches(g_ShireHorses, g_HorseSearch.searchBuffer, getHorseName);
		int suffolkPunchVisible = suffolkPunchMatches ? static_cast<int>(g_SuffolkPunchHorses.size()) :
		                         g_HorseSearch.CountMatches(g_SuffolkPunchHorses, g_HorseSearch.searchBuffer, getHorseName);
		int tennesseeWalkerVisible = tennesseeWalkerMatches ? static_cast<int>(g_TennesseeWalkerHorses.size()) :
		                            g_HorseSearch.CountMatches(g_TennesseeWalkerHorses, g_HorseSearch.searchBuffer, getHorseName);
		int thoroughbredVisible = thoroughbredMatches ? static_cast<int>(g_ThoroughbredHorses.size()) :
		                         g_HorseSearch.CountMatches(g_ThoroughbredHorses, g_HorseSearch.searchBuffer, getHorseName);
		int turkomanVisible = turkomanMatches ? static_cast<int>(g_TurkomanHorses.size()) :
		                     g_HorseSearch.CountMatches(g_TurkomanHorses, g_HorseSearch.searchBuffer, getHorseName);
		int miscellaneousVisible = miscellaneousMatches ? static_cast<int>(g_MiscellaneousHorses.size()) :
		                          g_HorseSearch.CountMatches(g_MiscellaneousHorses, g_HorseSearch.searchBuffer, getHorseName);

		// determine section visibility
		bool showAmericanPaint = americanPaintMatches || (americanPaintVisible > 0);
		bool showAmericanStandardbred = americanStandardbredMatches || (americanStandardbredVisible > 0);
		bool showAndalusian = andalusianMatches || (andalusianVisible > 0);
		bool showAppaloosa = appaloosaMatches || (appaloosaVisible > 0);
		bool showArabian = arabianMatches || (arabianVisible > 0);
		bool showArdennes = ardennesMatches || (ardennesVisible > 0);
		bool showBelgian = belgianMatches || (belgianVisible > 0);
		bool showBreton = bretonMatches || (bretonVisible > 0);
		bool showCriollo = criolloMatches || (criolloVisible > 0);
		bool showDutchWarmblood = dutchWarmbloodMatches || (dutchWarmbloodVisible > 0);
		bool showGang = gangMatches || (gangVisible > 0);
		bool showGypsyCob = gypsyCobMatches || (gypsyCobVisible > 0);
		bool showHungarianHalfbred = hungarianHalfbredMatches || (hungarianHalfbredVisible > 0);
		bool showKentuckySaddler = kentuckySaddlerMatches || (kentuckySaddlerVisible > 0);
		bool showKlardruber = klardruberMatches || (klardruberVisible > 0);
		bool showMissouriFoxTrotter = missouriFoxTrotterMatches || (missouriFoxTrotterVisible > 0);
		bool showMorgan = morganMatches || (morganVisible > 0);
		bool showMustang = mustangMatches || (mustangVisible > 0);
		bool showNokota = nokotaMatches || (nokotaVisible > 0);
		bool showNorfolkRoadster = norfolkRoadsterMatches || (norfolkRoadsterVisible > 0);
		bool showShire = shireMatches || (shireVisible > 0);
		bool showSuffolkPunch = suffolkPunchMatches || (suffolkPunchVisible > 0);
		bool showTennesseeWalker = tennesseeWalkerMatches || (tennesseeWalkerVisible > 0);
		bool showThoroughbred = thoroughbredMatches || (thoroughbredVisible > 0);
		bool showTurkoman = turkomanMatches || (turkomanVisible > 0);
		bool showMiscellaneous = miscellaneousMatches || (miscellaneousVisible > 0);

		int totalVisible = americanPaintVisible + americanStandardbredVisible + andalusianVisible + appaloosaVisible + arabianVisible +
		                  ardennesVisible + belgianVisible + bretonVisible + criolloVisible + dutchWarmbloodVisible + gangVisible +
		                  gypsyCobVisible + hungarianHalfbredVisible + kentuckySaddlerVisible + klardruberVisible + missouriFoxTrotterVisible +
		                  morganVisible + mustangVisible + nokotaVisible + norfolkRoadsterVisible + shireVisible + suffolkPunchVisible +
		                  tennesseeWalkerVisible + thoroughbredVisible + turkomanVisible + miscellaneousVisible;

		// render search bar with count and gender selection
		g_HorseSearch.RenderSearchBar("Search Horses", totalHorses, totalVisible, true);

		// american paint section
		if (showAmericanPaint)
		{
			RenderCenteredSeparator("American Paint");
			for (const auto& horse : g_AmericanPaintHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, americanPaintMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// american standardbred section
		if (showAmericanStandardbred)
		{
			RenderCenteredSeparator("American Standardbred");
			for (const auto& horse : g_AmericanStandardbredHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, americanStandardbredMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// andalusian section
		if (showAndalusian)
		{
			RenderCenteredSeparator("Andalusian");
			for (const auto& horse : g_AndalusianHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, andalusianMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// appaloosa section
		if (showAppaloosa)
		{
			RenderCenteredSeparator("Appaloosa");
			for (const auto& horse : g_AppaloosaHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, appaloosaMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// arabian section
		if (showArabian)
		{
			RenderCenteredSeparator("Arabian");
			for (const auto& horse : g_ArabianHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, arabianMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// ardennes section
		if (showArdennes)
		{
			RenderCenteredSeparator("Ardennes");
			for (const auto& horse : g_ArdennesHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, ardennesMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// belgian section
		if (showBelgian)
		{
			RenderCenteredSeparator("Belgian");
			for (const auto& horse : g_BelgianHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, belgianMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// breton section
		if (showBreton)
		{
			RenderCenteredSeparator("Breton");
			for (const auto& horse : g_BretonHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, bretonMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// criollo section
		if (showCriollo)
		{
			RenderCenteredSeparator("Criollo");
			for (const auto& horse : g_CriolloHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, criolloMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// dutch warmblood section
		if (showDutchWarmblood)
		{
			RenderCenteredSeparator("Dutch Warmblood");
			for (const auto& horse : g_DutchWarmbloodHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, dutchWarmbloodMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// gang section
		if (showGang)
		{
			RenderCenteredSeparator("Gang");
			for (const auto& horse : g_GangHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, gangMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// gypsy cob section
		if (showGypsyCob)
		{
			RenderCenteredSeparator("Gypsy Cob");
			for (const auto& horse : g_GypsyCobHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, gypsyCobMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// hungarian halfbred section
		if (showHungarianHalfbred)
		{
			RenderCenteredSeparator("Hungarian Halfbred");
			for (const auto& horse : g_HungarianHalfbredHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, hungarianHalfbredMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// kentucky saddler section
		if (showKentuckySaddler)
		{
			RenderCenteredSeparator("Kentucky Saddler");
			for (const auto& horse : g_KentuckySaddlerHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, kentuckySaddlerMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// klardruber section
		if (showKlardruber)
		{
			RenderCenteredSeparator("Klardruber");
			for (const auto& horse : g_KlardruberHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, klardruberMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// missouri fox trotter section
		if (showMissouriFoxTrotter)
		{
			RenderCenteredSeparator("Missouri Fox Trotter");
			for (const auto& horse : g_MissouriFoxTrotterHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, missouriFoxTrotterMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// morgan section
		if (showMorgan)
		{
			RenderCenteredSeparator("Morgan");
			for (const auto& horse : g_MorganHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, morganMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// mustang section
		if (showMustang)
		{
			RenderCenteredSeparator("Mustang");
			for (const auto& horse : g_MustangHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, mustangMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// nokota section
		if (showNokota)
		{
			RenderCenteredSeparator("Nokota");
			for (const auto& horse : g_NokotaHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, nokotaMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// norfolk roadster section
		if (showNorfolkRoadster)
		{
			RenderCenteredSeparator("Norfolk Roadster");
			for (const auto& horse : g_NorfolkRoadsterHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, norfolkRoadsterMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// shire section
		if (showShire)
		{
			RenderCenteredSeparator("Shire");
			for (const auto& horse : g_ShireHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, shireMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// suffolk punch section
		if (showSuffolkPunch)
		{
			RenderCenteredSeparator("Suffolk Punch");
			for (const auto& horse : g_SuffolkPunchHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, suffolkPunchMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// tennessee walker section
		if (showTennesseeWalker)
		{
			RenderCenteredSeparator("Tennessee Walker");
			for (const auto& horse : g_TennesseeWalkerHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, tennesseeWalkerMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// thoroughbred section
		if (showThoroughbred)
		{
			RenderCenteredSeparator("Thoroughbred");
			for (const auto& horse : g_ThoroughbredHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, thoroughbredMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// turkoman section
		if (showTurkoman)
		{
			RenderCenteredSeparator("Turkoman");
			for (const auto& horse : g_TurkomanHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, turkomanMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// miscellaneous section
		if (showMiscellaneous)
		{
			RenderCenteredSeparator("Miscellaneous");
			for (const auto& horse : g_MiscellaneousHorses)
			{
				if (g_HorseSearch.ShouldShowItem(horse, miscellaneousMatches, getHorseName))
				{
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
				}
			}
			ImGui::Spacing();
		}

		// show helpful message when no matches found
		bool anyVisible = showAmericanPaint || showAmericanStandardbred || showAndalusian || showAppaloosa || showArabian ||
		                 showArdennes || showBelgian || showBreton || showCriollo || showDutchWarmblood || showGang ||
		                 showGypsyCob || showHungarianHalfbred || showKentuckySaddler || showKlardruber || showMissouriFoxTrotter ||
		                 showMorgan || showMustang || showNokota || showNorfolkRoadster || showShire || showSuffolkPunch ||
		                 showTennesseeWalker || showThoroughbred || showTurkoman || showMiscellaneous;

		if (!anyVisible && !g_HorseSearch.searchBuffer.empty())
		{
			ImGui::Text("No horses or sections match your search");
			ImGui::Text("Try searching for: 'american', 'arabian', 'black', 'grey', 'gang', 'mustang', etc.");
		}
	}

	// animal data structures
	struct LegendaryAnimal
	{
		std::string name;
		std::string model;
		int variation;
	};

	struct RegularAnimal
	{
		std::string name;
		std::string model;
		int variation;
	};

	struct Dog
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
		{"Legendary Cougar", "a_c_cougar_01", 5},
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

	// regular animals data (parsed from animals.txt - lines 1-304)
	static std::vector<RegularAnimal> g_RegularAnimals = {
		{"American Alligator", "A_C_Alligator_01", 0},
		{"American Alligator (small)", "A_C_Alligator_03", 0},
		{"Nine-Banded Armadillo", "A_C_Armadillo_01", 0},
		{"American Badger", "A_C_Badger_01", 0},
		{"Little Brown Bat", "A_C_Bat_01", 0},
		{"American Black Bear", "A_C_BearBlack_01", 0},
		{"Grizzly Bear", "A_C_Bear_01", 0},
		{"North American Beaver", "A_C_Beaver_01", 0},
		{"Blue Jay", "A_C_BlueJay_01", 0},
		{"Wild Boar", "A_C_Boar_01", 0},
		{"Whitetail Buck", "A_C_Buck_01", 0},
		{"Whitetail Deer", "A_C_Deer_01", 0},
		{"American Bison", "A_C_Buffalo_01", 0},
		{"Angus Bull", "A_C_Bull_01", 0},
		{"Devon Bull", "A_C_Bull_01", 3},
		{"Hereford Bull", "A_C_Bull_01", 2},
		{"American Bullfrog", "A_C_FrogBull_01", 0},
		{"Northern Cardinal", "A_C_Cardinal_01", 0},
		{"American Domestic Cat", "A_C_Cat_01", 0},
		{"Cedar Waxwing", "A_C_CedarWaxwing_01", 0},
		{"Dominique Chicken", "A_C_Chicken_01", 0},
		{"Dominique Rooster", "A_C_Rooster_01", 0},
		{"Java Chicken", "A_C_Chicken_01", 2},
		{"Java Rooster", "A_C_Rooster_01", 1},
		{"Leghorn Chicken", "A_C_Chicken_01", 3},
		{"Leghorn Rooster", "A_C_Rooster_01", 2},
		{"Greater Prairie Chicken", "A_C_PrairieChicken_01", 0},
		{"Western Chipmunk", "A_C_Chipmunk_01", 0},
		{"Californian Condor", "A_C_CaliforniaCondor_01", 0},
		{"Cougar", "A_C_Cougar_01", 0},
		{"Double-crested Cormorant", "A_C_Cormorant_01", 0},
		{"Neotropic Cormorant", "A_C_Cormorant_01", 2},
		{"Florida Cracker Cow", "A_C_Cow", 0},
		{"California Valley Coyote", "A_C_Coyote_01", 0},
		{"Cuban Land Crab", "A_C_Crab_01", 0},
		{"Red Swamp Crayfish", "A_C_Crawfish_01", 0},
		{"Whooping Crane", "A_C_CraneWhooping_01", 0},
		{"Sandhill Crane", "A_C_CraneWhooping_01", 1},
		{"American Crow", "A_C_Crow_01", 0},
		{"Standard Donkey", "A_C_Donkey_01", 0},
		{"Mallard Duck", "A_C_Duck_01", 0},
		{"Pekin Duck", "A_C_Duck_01", 2},
		{"Bald Eagle", "A_C_Eagle_01", 0},
		{"Golden Eagle", "A_C_Eagle_01", 1},
		{"Reddish Egret", "A_C_Egret_01", 0},
		{"Little Egret", "A_C_Egret_01", 1},
		{"Snowy Egret", "A_C_Egret_01", 2},
		{"Rocky Mountain Bull Elk", "A_C_Elk_01", 0},
		{"Rocky Mountain Cow Elk", "A_C_Elk_01", 2},
		{"American Red Fox", "A_C_Fox_01", 0},
		{"American Gray Fox", "A_C_Fox_01", 1},
		{"Silver Fox", "A_C_Fox_01", 2},
		{"Banded Gila Monster", "A_C_GilaMonster_01", 0},
		{"Alpine Goat", "A_C_Goat_01", 0},
		{"Canada Goose", "A_C_GooseCanada_01", 0},
		{"Ferruginous Hawk", "A_C_Hawk_01", 0},
		{"Red-tailed Hawk", "A_C_Hawk_01", 2},
		{"Rough-legged Hawk", "A_C_Hawk_01", 1},
		{"Great Blue Heron", "A_C_Heron_01", 0},
		{"Tricolored Heron", "A_C_Heron_01", 2},
		{"Desert Iguana", "A_C_IguanaDesert_01", 0},
		{"Green Iguana", "A_C_Iguana_01", 0},
		{"Collared Peccary", "A_C_Javelina_01", 0},
		{"Lion", "A_C_LionMangy_01", 0},
		{"Common Loon", "A_C_Loon_01", 0},
		{"Pacific Loon", "A_C_Loon_01", 2},
		{"Yellow-billed Loon", "A_C_Loon_01", 1},
		{"Western Bull Moose", "A_C_Moose_01", 3},
		{"Western Moose", "A_C_Moose_01", 0},
		{"Mule", "A_C_HORSEMULE_01", 0},
		{"American Muskrat", "A_C_Muskrat_01", 0},
		{"Baltimore Oriole", "A_C_Oriole_01", 0},
		{"Hooded Oriole", "A_C_Oriole_01", 1},
		{"Californian Horned Owl", "A_C_Owl_01", 1},
		{"Coastal Horned Owl", "A_C_Owl_01", 2},
		{"Great Horned Owl", "A_C_Owl_01", 0},
		{"Angus Ox", "A_C_Ox_01", 0},
		{"Devon Ox", "A_C_Ox_01", 2},
		{"Panther", "A_C_Panther_01", 0},
		{"Florida Panther", "A_C_Panther_01", 4},
		{"Carolina Parakeet", "A_C_CarolinaParakeet_01", 0},
		{"Blue and Yellow Macaw", "A_C_Parrot_01", 0},
		{"Great Green Macaw", "A_C_Parrot_01", 1},
		{"Scarlet Macaw", "A_C_Parrot_01", 2},
		{"American White Pelican", "A_C_Pelican_01", 0},
		{"Brown Pelican", "A_C_Pelican_01", 1},
		{"Ring-necked Pheasant", "A_C_Pheasant_01", 0},
		{"Chinese Ring-necked Pheasant", "A_C_Pheasant_01", 2},
		{"Berkshire Pig", "A_C_Pig_01", 0},
		{"Big China Pig", "A_C_Pig_01", 3},
		{"Old Spot Pig", "A_C_Pig_01", 2},
		{"Band-tailed Pigeon", "A_C_Pigeon", 2},
		{"Rock Pigeon", "A_C_Pigeon", 0},
		{"Virginia Opossum", "A_C_Possum_01", 0},
		{"American Pronghorn Buck", "A_C_Pronghorn_01", 10},
		{"American Pronghorn Doe", "A_C_Pronghorn_01", 0},
		{"Sonoran Pronghorn Buck", "A_C_Pronghorn_01", 13},
		{"Sonoran Pronghorn Doe", "A_C_Pronghorn_01", 4},
		{"Baja California Pronghorn Buck", "A_C_Pronghorn_01", 16},
		{"Baja California Pronghorn Doe", "A_C_Pronghorn_01", 7},
		{"California Quail", "A_C_Quail_01", 0},
		{"Sierra Nevada Bighorn Ram", "A_C_BigHornRam_01", 9},
		{"Sierra Nevada Bighorn Sheep", "A_C_BigHornRam_01", 0},
		{"Desert Bighorn Ram", "A_C_BigHornRam_01", 16},
		{"Desert Bighorn Sheep", "A_C_BigHornRam_01", 6},
		{"Rocky Mountain Bighorn Ram", "A_C_BigHornRam_01", 13},
		{"Rocky Mountain Bighorn Sheep", "A_C_BigHornRam_01", 3},
		{"Black-tailed Jackrabbit", "A_C_Rabbit_01", 0},
		{"North American Raccoon", "A_C_Raccoon_01", 0},
		{"Black Rat", "A_C_Rat_01", 4},
		{"Brown Rat", "A_C_Rat_01", 0},
		{"Western Raven", "A_C_Raven_01", 0},
		{"Red-footed Booby", "A_C_RedFootedBooby_01", 0},
		{"American Robin", "A_C_Robin_01", 0},
		{"Roseate Spoonbill", "A_C_RoseateSpoonbill_01", 0},
		{"Herring Gull", "A_C_Seagull_01", 0},
		{"Laughing Gull", "A_C_Seagull_01", 1},
		{"Ring-billed Gull", "A_C_Seagull_01", 2},
		{"Merino Sheep", "A_C_Sheep_01", 0},
		{"Striped Skunk", "A_C_Skunk_01", 0},
		{"Red Boa Snake", "A_C_SnakeRedBoa_01", 0},
		{"Rainbow Boa Snake", "A_C_SnakeRedBoa_01", 2},
		{"Sunglow Boa Snake", "A_C_SnakeRedBoa_01", 1},
		{"Diamondback Rattlesnake", "A_C_Snake_01", 0},
		{"Fer-de-Lance Snake", "A_C_SnakeFerDeLance_01", 0},
		{"Black-tailed Rattlesnake", "A_C_SnakeBlackTailRattle_01", 0},
		{"Timber Rattlesnake", "A_C_Snake_01", 2},
		{"Northern Copperhead Snake", "A_C_SnakeFerDeLance_01", 2},
		{"Southern Copperhead Snake", "A_C_SnakeFerDeLance_01", 1},
		{"Midland Water Snake", "A_C_SnakeWater_01", 0},
		{"Cottonmouth Snake", "A_C_SnakeWater_01", 1},
		{"Northern Water Snake", "A_C_SnakeWater_01", 2},
		{"Scarlet Tanager Songbird", "A_C_SongBird_01", 1},
		{"Western Tanager Songbird", "A_C_SongBird_01", 0},
		{"Eurasian Tree Sparrow", "A_C_Sparrow_01", 3},
		{"American Tree Sparrow", "A_C_Sparrow_01", 0},
		{"Golden Crowned Sparrow", "A_C_Sparrow_01", 2},
		{"American Red Squirrel", "A_C_Squirrel_01", 1},
		{"Western Gray Squirrel", "A_C_Squirrel_01", 0},
		{"Black Squirrel", "A_C_Squirrel_01", 2},
		{"Western Toad", "A_C_Toad_01", 0},
		{"Sonoran Desert Toad", "A_C_Toad_01", 3},
		{"Eastern Wild Turkey", "A_C_Turkey_01", 0},
		{"Rio Grande Wild Turkey", "A_C_TurkeyWild_01", 0},
		{"Alligator Snapping Turtle", "A_C_TurtleSnapping_01", 0},
		{"Eastern Turkey Vulture", "A_C_Vulture_01", 4},
		{"Western Turkey Vulture", "A_C_Vulture_01", 0},
		{"Gray Wolf", "A_C_Wolf", 0},
		{"Timber Wolf", "A_C_Wolf_Medium", 0},
		{"Gray Wolf (small)", "A_C_Wolf_Small", 0},
		{"Red-bellied Woodpecker", "A_C_Woodpecker_01", 0},
		{"Pileated Woodpecker", "A_C_Woodpecker_02", 0}
	};

	// dogs data (parsed from animals.txt - lines 382-408)
	static std::vector<Dog> g_Dogs = {
		{"American Foxhound", "A_C_DOGAMERICANFOXHOUND_01", 0},
		{"Australian Shepherd", "A_C_DOGAUSTRALIANSHEPERD_01", 0},
		{"Bluetick Coonhound", "A_C_DOGBLUETICKCOONHOUND_01", 0},
		{"Catahoula Cur", "A_C_DOGCATAHOULACUR_01", 0},
		{"Ches Bay Retriever", "A_C_DOGCHESBAYRETRIEVER_01", 0},
		{"Hobo", "A_C_DOGHOBO_01", 0},
		{"Hound", "A_C_DOGHOUND_01", 0},
		{"Husky", "A_C_DOGHUSKY_01", 0},
		{"Labrador", "A_C_DOGLAB_01", 0},
		{"Lion (dog)", "A_C_DOGLION_01", 0},
		{"Poodle", "A_C_DOGPOODLE_01", 0},
		{"Rough Collie", "A_C_DOGCOLLIE_01", 0},
		{"Rufus", "A_C_DOGRUFUS_01", 0},
		{"Street", "A_C_DOGSTREET_01", 0}
	};

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

			// draw centered line (3x text width) using screen coordinates
			float lineWidth = textSize.x * 3.0f;
			float linePosX = (contentRegion.x - lineWidth) * 0.5f;

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 windowPos = ImGui::GetWindowPos();
			ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();

			// use screen coordinates for proper positioning with scrolling
			drawList->AddLine(
				ImVec2(windowPos.x + linePosX, cursorScreenPos.y),
				ImVec2(windowPos.x + linePosX + lineWidth, cursorScreenPos.y),
				ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);

			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
			ImGui::Spacing();
		};

		// lambda functions for getting names
		auto getLegendaryName = [](const LegendaryAnimal& animal) { return animal.name; };
		auto getRegularName = [](const RegularAnimal& animal) { return animal.name; };
		auto getDogName = [](const Dog& dog) { return dog.name; };

		// check section matches
		bool legendaryMatches = g_AnimalSearch.SectionMatches("Legendary Animals", g_AnimalSearch.searchBuffer) ||
		                       g_AnimalSearch.SectionMatches("Legendary", g_AnimalSearch.searchBuffer);
		bool regularMatches = g_AnimalSearch.SectionMatches("Regular Animals", g_AnimalSearch.searchBuffer) ||
		                     g_AnimalSearch.SectionMatches("Animals", g_AnimalSearch.searchBuffer);
		bool dogMatches = g_AnimalSearch.SectionMatches("Dogs", g_AnimalSearch.searchBuffer);

		// count visible animals in each section
		int legendaryVisible = legendaryMatches ? static_cast<int>(g_LegendaryAnimals.size()) :
		                      g_AnimalSearch.CountMatches(g_LegendaryAnimals, g_AnimalSearch.searchBuffer, getLegendaryName);
		int regularVisible = regularMatches ? static_cast<int>(g_RegularAnimals.size()) :
		                    g_AnimalSearch.CountMatches(g_RegularAnimals, g_AnimalSearch.searchBuffer, getRegularName);
		int dogVisible = dogMatches ? static_cast<int>(g_Dogs.size()) :
		                g_AnimalSearch.CountMatches(g_Dogs, g_AnimalSearch.searchBuffer, getDogName);

		// determine section visibility
		bool showLegendarySection = legendaryMatches || (legendaryVisible > 0);
		bool showRegularSection = regularMatches || (regularVisible > 0);
		bool showDogSection = dogMatches || (dogVisible > 0);

		// calculate totals
		int totalAnimals = static_cast<int>(g_LegendaryAnimals.size() + g_RegularAnimals.size() + g_Dogs.size());
		int totalVisible = legendaryVisible + regularVisible + dogVisible;

		// render search bar with count
		g_AnimalSearch.RenderSearchBar("Search Animals", totalAnimals, totalVisible);

		// legendary animals section
		if (showLegendarySection)
		{
			RenderCenteredSeparator("Legendary Animals");

			for (const auto& animal : g_LegendaryAnimals)
			{
				if (g_AnimalSearch.ShouldShowItem(animal, legendaryMatches, getLegendaryName))
				{
					if (ImGui::Button(animal.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(animal.model, animal.variation);
					}
				}
			}

			ImGui::Spacing();
		}

		// regular animals section
		if (showRegularSection)
		{
			RenderCenteredSeparator("Regular Animals");

			for (const auto& animal : g_RegularAnimals)
			{
				if (g_AnimalSearch.ShouldShowItem(animal, regularMatches, getRegularName))
				{
					if (ImGui::Button(animal.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(animal.model, animal.variation);
					}
				}
			}

			ImGui::Spacing();
		}

		// dogs section
		if (showDogSection)
		{
			RenderCenteredSeparator("Dogs");

			for (const auto& dog : g_Dogs)
			{
				if (g_AnimalSearch.ShouldShowItem(dog, dogMatches, getDogName))
				{
					if (ImGui::Button(dog.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(dog.model, dog.variation);
					}
				}
			}

			ImGui::Spacing();
		}

		// show helpful message when no matches found
		if (!showLegendarySection && !showRegularSection && !showDogSection && !g_AnimalSearch.searchBuffer.empty())
		{
			ImGui::Text("No animals or sections match your search");
			ImGui::Text("Try searching for: 'legendary', 'regular animals', 'dogs', 'bear', 'wolf', etc.");
		}
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
			SpawnPed(g_PedModelBuffer, g_Variation, true); // true = give weapon if armed is enabled
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
			// story gang members with their specific variations
			struct StoryGangMember
			{
				std::string model;
				int variation;
			};

			std::vector<StoryGangMember> storyGang = {
				{"cs_dutch", 4},                    // Dutch van der Linde
				{"cs_johnmarston", 6},              // John Marston
				{"cs_hoseamatthews", 8},            // Hosea Matthews
				{"cs_billwilliamson", 1},           // Bill Williamson
				{"cs_javierescuella", 20},          // Javier Escuella
				{"cs_micahbell", 1},                // Micah Bell
				{"cs_mrsadler", 17},                // Sadie Adler
				{"cs_charlessmith", 15},            // Charles Smith
				{"cs_mollyoshea", 5},               // Molly O'Shea
				{"cs_susangrimshaw", 7},            // Susan Grimshaw
				{"cs_abigailroberts", 3},           // Abigail Roberts Marston
				{"cs_marybeth", 5},                 // Mary-Beth Gaskill
				{"cs_karen", 9},                    // Karen Jones
				{"cs_uncle", 2},                    // Uncle
				{"cs_sean", 0}                      // Sean
			};

			// spawn each gang member using SpawnPed helper function
			// note: variations are fixed and won't be affected by the global variation setting
			for (const auto& member : storyGang)
			{
				SpawnPed(member.model, member.variation, true, true); // isStoryGang = true to preserve attributes
			}

			// add infinite health/stamina logic to the gang members we just spawned
			// wait a bit to ensure all gang members are spawned first
			FiberPool::Push([storyGang] {
				ScriptMgr::Yield(100ms); // wait for all spawns to complete

				// get the last N peds from the spawned list (where N = gang size)
				int gangSize = static_cast<int>(storyGang.size());
				if (g_SpawnedPeds.size() >= gangSize)
				{
					// apply infinite health/stamina to the last N spawned peds
					for (int i = g_SpawnedPeds.size() - gangSize; i < g_SpawnedPeds.size(); i++)
					{
						Ped ped = g_SpawnedPeds[i];

						// initial health/stamina fill
						ped.SetHealth(ped.GetMaxHealth());
						ped.SetStamina(ped.GetMaxStamina());
						ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), (int)AttributeCore::ATTRIBUTE_CORE_HEALTH, 100);
						ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), (int)AttributeCore::ATTRIBUTE_CORE_STAMINA, 100);
						ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), (int)AttributeCore::ATTRIBUTE_CORE_DEADEYE, 100);

						PED::SET_PED_ACCURACY(ped.GetHandle(), 95);

						// mark as Story Gang member for maintenance loop
						DECORATOR::DECOR_SET_INT(ped.GetHandle(), "SH_STORY_GANG", 1);

						// story gang relationship setup - use a custom gang relationship group
						Hash storyGangRelationshipGroup = "STORY_GANG_FRIENDLY"_J; // custom relationship group for story gang

						// set up shared gang relationships
						PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped.GetHandle(), storyGangRelationshipGroup);

						// make gang members friendly with each other using LIKE relationship (value 2)
						PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, storyGangRelationshipGroup, storyGangRelationshipGroup);

						// also set up relationship with player to avoid conflicts
						auto playerGroup = PED::GET_PED_RELATIONSHIP_GROUP_HASH(YimMenu::Self::GetPed().GetHandle());
						PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, storyGangRelationshipGroup, playerGroup);
						PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, playerGroup, storyGangRelationshipGroup);

						// continuous maintenance loop for this gang member
						FiberPool::Push([ped] () mutable {
							while (ped.IsValid() && !ped.IsDead())
							{
								// maintain health bar - same as player logic
								auto health_bar = ped.GetHealth();
								if (health_bar < ped.GetMaxHealth())
									ped.SetHealth(ped.GetMaxHealth());

								// maintain health core - same as player logic
								auto health_core = ATTRIBUTE::_GET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), (int)AttributeCore::ATTRIBUTE_CORE_HEALTH);
								if (health_core < 100)
									ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), (int)AttributeCore::ATTRIBUTE_CORE_HEALTH, 100);

								// maintain stamina bar - same as player logic
								auto stamina_bar = ped.GetStamina();
								auto max_stamina = PED::_GET_PED_MAX_STAMINA(ped.GetHandle());
								if (stamina_bar < max_stamina)
									PED::_CHANGE_PED_STAMINA(ped.GetHandle(), max_stamina - stamina_bar);

								// maintain stamina core - same as player logic
								auto stamina_core = ATTRIBUTE::_GET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), (int)AttributeCore::ATTRIBUTE_CORE_STAMINA);
								if (stamina_core < 100)
									ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), (int)AttributeCore::ATTRIBUTE_CORE_STAMINA, 100);

								// maintain deadeye core - same as player logic
								auto deadeye_core = ATTRIBUTE::_GET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), (int)AttributeCore::ATTRIBUTE_CORE_DEADEYE);
								if (deadeye_core < 100)
									ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), (int)AttributeCore::ATTRIBUTE_CORE_DEADEYE, 100);

								// maintain never flee attributes - prevent game from resetting them
								PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 78, true);  // CA_DISABLE_ALL_RANDOMS_FLEE
								PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 17, false); // CA_ALWAYS_FLEE
								PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 58, true);  // CA_DISABLE_FLEE_FROM_COMBAT

								// maintain story gang relationships - prevent game from resetting them
								Hash storyGangRelationshipGroup = "STORY_GANG_FRIENDLY"_J;
								PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped.GetHandle(), storyGangRelationshipGroup);
								PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, storyGangRelationshipGroup, storyGangRelationshipGroup);

								// maintain companion relationship - prevent them from forgetting to follow
								if (g_Companion)
								{
									int group = PED::GET_PED_GROUP_INDEX(YimMenu::Self::GetPed().GetHandle());
									if (PED::DOES_GROUP_EXIST(group))
									{
										// refresh group membership
										PED::SET_PED_AS_GROUP_MEMBER(ped.GetHandle(), group);
										// refresh group formation
										PED::SET_GROUP_FORMATION(group, g_Formation);
										// DO NOT override story gang relationships - they must stay in STORY_GANG_FRIENDLY group

										// additional companion maintenance - prevent AI override
										PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(ped.GetHandle(), YimMenu::Self::GetPlayer().GetId(), false);
										PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped.GetHandle(), false);

										// refresh companion config flags
										ped.SetConfigFlag(PedConfigFlag::_0x16A14D9A, false);
										ped.SetConfigFlag(PedConfigFlag::_DisableHorseFleeILO, true);
										ped.SetConfigFlag(PedConfigFlag::_0x74F95F2E, false);
										ped.SetConfigFlag(PedConfigFlag::Avoidance_Ignore_All, false);
										ped.SetConfigFlag(PedConfigFlag::DisableShockingEvents, false);
										ped.SetConfigFlag(PedConfigFlag::DisablePedAvoidance, false);
										ped.SetConfigFlag(PedConfigFlag::DisableExplosionReactions, false);
										ped.SetConfigFlag(PedConfigFlag::DisableEvasiveStep, false);
										ped.SetConfigFlag(PedConfigFlag::DisableHorseGunshotFleeResponse, true);

										// handle stuck/idle peds - check distance and movement
										Vector3 playerPos = YimMenu::Self::GetPed().GetPosition();
										Vector3 pedPos = ped.GetPosition();
										float distance = sqrt(pow(playerPos.x - pedPos.x, 2) + pow(playerPos.y - pedPos.y, 2) + pow(playerPos.z - pedPos.z, 2));

										// if ped is too far away (>30 meters) or seems idle, refresh their task
										if (distance > 30.0f || (!PED::IS_PED_IN_COMBAT(ped.GetHandle(), 0) && distance > 10.0f))
										{
											// clear current tasks and refresh AI
											TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped.GetHandle(), true, true);
											PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped.GetHandle(), false);
											// re-assign to group to trigger follow behavior
											PED::SET_PED_AS_GROUP_MEMBER(ped.GetHandle(), group);

											TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(ped.GetHandle(), YimMenu::Self::GetPed().GetHandle(), 0.0f, 0.0f, 0.0f, 1.0f, -1, 2.5f, true, false, false, false, false, false);
										}
									}
								}

								// keep clean - same as player logic
								PED::_SET_PED_DAMAGE_CLEANLINESS(ped.GetHandle(), (int)PedDamageCleanliness::PED_DAMAGE_CLEANLINESS_PERFECT);
								PED::CLEAR_PED_WETNESS(ped.GetHandle());
								PED::CLEAR_PED_ENV_DIRT(ped.GetHandle());
								PED::CLEAR_PED_BLOOD_DAMAGE(ped.GetHandle());
								PED::CLEAR_PED_DAMAGE_DECAL_BY_ZONE(ped.GetHandle(), 10, "ALL");

								ScriptMgr::Yield(1000ms); // check every 1 second - less aggressive to prevent AI interference
							}
						});
					}
				}
			});
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