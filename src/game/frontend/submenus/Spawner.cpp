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

	// unified ped spawning function - used by all spawn buttons
	static void SpawnPed(const std::string& model, int variation, bool giveWeapon = false)
	{
		FiberPool::Push([model, variation, giveWeapon] {
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
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 5, false);   // CA_ALWAYS_FIGHT - enable
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
				// set NO_RELATIONSHIP as requested
				PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped.GetHandle(), Joaat("NO_RELATIONSHIP"));
			}

			ped.SetVisible(!g_Invis);

			if (g_Scale != 1.0f)
				ped.SetScale(g_Scale);

			// apply variation
			ped.SetVariation(variation);

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

				// === COMBAT ATTRIBUTES (ALL from Rampage Trainer) ===
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 0, true);   // CA_USE_COVER - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 1, true);   // CA_USE_VEHICLE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 2, true);   // CA_DO_DRIVEBYS - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 3, true);   // CA_LEAVE_VEHICLES - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 4, true);   // CA_STRAFE_BASED_ON_TARGET_PROXIMITY - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 5, false);   // CA_ALWAYS_FIGHT - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 8, true);   // CA_ALLOW_STRAFE_BREAKUP - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 24, true);  // CA_USE_PROXIMITY_FIRING_RATE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 27, true);  // CA_PERFECT_ACCURACY - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 31, true);  // CA_MAINTAIN_MIN_DISTANCE_TO_TARGET - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 38, true);  // CA_DISABLE_BULLET_REACTIONS - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 46, true);  // CA_CAN_FIGHT_ARMED_PEDS_WHEN_NOT_ARMED - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 50, true);  // CA_CAN_CHARGE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 57, true);  // CA_DISABLE_SEEK_DUE_TO_LINE_OF_SIGHT - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 58, true);  // CA_DISABLE_FLEE_FROM_COMBAT
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

	// convenience wrapper for animal spawning (no weapons)
	static void SpawnAnimal(const std::string& model, int variation)
	{
		SpawnPed(model, variation, false);
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

		// render search bar with count display
		void RenderSearchBar(const std::string& placeholder, int totalItems, int visibleItems)
		{
			InputTextWithHint(("##search_" + placeholder).c_str(), placeholder.c_str(), &searchBuffer).Draw();

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
