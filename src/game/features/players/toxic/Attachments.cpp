#include "game/backend/ScriptMgr.hpp"
#include "game/backend/Self.hpp"
#include "game/backend/Players.hpp"
#include "game/commands/PlayerCommand.hpp"
#include "game/rdr/Natives.hpp"
#include "core/commands/LoopedCommand.hpp"
#include "core/frontend/Notifications.hpp"
#include "AttachmentTracker.hpp"

namespace YimMenu::Features
{
	// global variables to track attachment state
	int g_AttachedToPlayerHandle = 0;
	int g_AttachedToPlayerId = -1;
	static void ClearPedTasks(int ped)
	{
		PED::SET_PED_SHOULD_PLAY_IMMEDIATE_SCENARIO_EXIT(ped);
		TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped, true, true);
		ScriptMgr::Yield(50ms);
	}

	static void PlayAnim(std::string anim_dict, std::string anim_name)
	{
		while (!STREAMING::HAS_ANIM_DICT_LOADED(anim_dict.c_str()))
		{
			STREAMING::REQUEST_ANIM_DICT(anim_dict.c_str());
			ScriptMgr::Yield();
		}

		TASK::TASK_PLAY_ANIM(Self::GetPed().GetHandle(), anim_dict.c_str(), anim_name.c_str(), 1.0f, 1.0f, -1, 1, 0.0f, false, 0, false, "", false);
	}

	static void AttachToEntity(int target, float PosX, float PosY, float PosZ, float RotX, float RotY, float RotZ)
	{
		ENTITY::ATTACH_ENTITY_TO_ENTITY(Self::GetPed().GetHandle(), target, 0, PosX, PosY, PosZ, RotX, RotY, RotZ, false, false, false, true, 0, true, false, false);

		// track attachment state for monitoring system
		g_AttachedToPlayerHandle = target;
		g_AttachedToPlayerId = -1;

		// find player id from handle for tracking
		for (auto& [id, player] : Players::GetPlayers())
		{
			if (player.GetPed().IsValid() && player.GetPed().GetHandle() == target)
			{
				g_AttachedToPlayerId = id;
				break;
			}
		}
	}

	class Spank : public PlayerCommand
	{
		using PlayerCommand::PlayerCommand;

		virtual void OnCall(Player player) override
		{
			if (player.GetPed().IsValid())
			{
				ClearPedTasks(player.GetPed().GetHandle());
				AttachToEntity(player.GetPed().GetHandle(), 0.0, -0.8, -0.5, 0.0, 15.0, -30.0);
				PlayAnim("script_re@peep_tom@spanking_cowboy", "spanking_idle_female");
			}
		}
	};

	class RideOnShoulders : public PlayerCommand
	{
		using PlayerCommand::PlayerCommand;

		virtual void OnCall(Player player) override
		{
			if (player.GetPed().IsValid())
			{
				ClearPedTasks(player.GetPed().GetHandle());
				AttachToEntity(player.GetPed().GetHandle(), 0.0, 0.0, 0.7, 0.0, 0.0, 10.0);
				PlayAnim("script_story@gry1@ig@ig_15_archibald_sit_reading", "held_1hand_archibald");
			}
		}
	};

	class TouchPlayer : public PlayerCommand
	{
		using PlayerCommand::PlayerCommand;

		virtual void OnCall(Player player) override
		{
			if (player.GetPed().IsValid())
			{
				ClearPedTasks(player.GetPed().GetHandle());
				AttachToEntity(player.GetPed().GetHandle(), 0.0, 0.8, 0.0, -8.0, 0.0, 150.0);
				PlayAnim("script_re@unpaid_debt@paid", "slap_loop_attacker");
			}
		}
	};

	class Slap : public PlayerCommand
	{
		using PlayerCommand::PlayerCommand;

		virtual void OnCall(Player player) override
		{
			if (player.GetPed().IsValid())
			{
				ClearPedTasks(player.GetPed().GetHandle());
				AttachToEntity(player.GetPed().GetHandle(), 0.0, 0.6, 0.0, 0.0, 0.0, 180.0);
				PlayAnim("mech_melee@unarmed@posse@ambient@healthy@noncombat", "slap_back_drunk_vs_drunk_light_v1_att");
			}
		}
	};

	class CancelAttachment : public Command
	{
		using Command::Command;

		virtual void OnCall() override
		{
			if (ENTITY::IS_ENTITY_ATTACHED_TO_ANY_PED(Self::GetPed().GetHandle()))
				ENTITY::DETACH_ENTITY(Self::GetPed().GetHandle(), true, true);
			ClearPedTasks(Self::GetPed().GetHandle());

			// clear attachment tracking state
			g_AttachedToPlayerHandle = 0;
			g_AttachedToPlayerId = -1;
		}
	};

	// attachment monitoring system to prevent invisibility issues
	class AttachmentMonitor : public LoopedCommand
	{
		using LoopedCommand::LoopedCommand;

		virtual void OnTick() override
		{
			// only monitor if we're attached to someone
			if (g_AttachedToPlayerId == -1 || g_AttachedToPlayerHandle == 0)
				return;

			auto selfPed = Self::GetPed();
			if (!selfPed.IsValid())
				return;

			// check if we're still attached
			if (!ENTITY::IS_ENTITY_ATTACHED_TO_ANY_PED(selfPed.GetHandle()))
			{
				g_AttachedToPlayerHandle = 0;
				g_AttachedToPlayerId = -1;
				return;
			}

			// check if the player we're attached to still exists and is valid
			bool playerStillValid = false;
			for (auto& [id, player] : Players::GetPlayers())
			{
				if (id == g_AttachedToPlayerId && player.IsValid() && player.GetPed().IsValid())
				{
					if (player.GetPed().GetHandle() == g_AttachedToPlayerHandle)
					{
						playerStillValid = true;
						break;
					}
				}
			}

			// if player is no longer valid or left the session, detach and refresh
			if (!playerStillValid)
			{
				ENTITY::DETACH_ENTITY(selfPed.GetHandle(), true, true);
				ClearPedTasks(selfPed.GetHandle());

				g_AttachedToPlayerHandle = 0;
				g_AttachedToPlayerId = -1;

				RefreshPlayerModel();

				Notifications::Show("Attachment", "automatically detached due to player leaving session", NotificationType::Info);
			}
		}

	private:
		void RefreshPlayerModel()
		{
			auto selfPlayer = Self::GetPlayer();
			auto selfPed = Self::GetPed();

			if (!selfPlayer.IsValid() || !selfPed.IsValid())
				return;

			auto currentModel = selfPed.GetModel();

			// force visibility refresh to fix local rendering
			selfPed.SetVisible(false);
			ScriptMgr::Yield(50ms);
			selfPed.SetVisible(true);

			// force network sync refresh
			if (selfPed.IsNetworked())
			{
				selfPed.ForceSync();
			}

			// additional refresh by reapplying current model
			if (STREAMING::HAS_MODEL_LOADED(currentModel))
			{
				PLAYER::SET_PLAYER_MODEL(selfPlayer.GetId(), currentModel, false);
				Self::Update();
			}
		}
	};

	static Spank _Spank{"spank", "Spank", "Spanks the target player"};
	static RideOnShoulders _RideOnShoulders{"rideonshoulders", "Ride on Shoulders", "Ride on a players shoulders"};
	static TouchPlayer _TouchPlayer{"touchplayer", "Touch Player", "Touches the other player..."};
	static Slap _Slap{"slap", "Slap", "Slaps the player"};
	static CancelAttachment _CancelAttachment{"cancelattachment", "Cancel Attachment", "Cancels current attachment"};
	static AttachmentMonitor _AttachmentMonitor{"attachmentmonitor", "Attachment Monitor", "automatically handles attachment cleanup to prevent invisibility issues"};
}