#include "core/commands/LoopedCommand.hpp"
#include "game/backend/Self.hpp"

namespace YimMenu::Features
{
    class HorseGodmode : public LoopedCommand
    {
        using LoopedCommand::LoopedCommand;

        virtual void OnTick() override
        {
            // Determine the current horse Ped (mounted or last used)
            Ped horse = Self::GetMount();
            if (!horse.IsValid())
            {
                horse = Self::GetPed().GetLastMount();  // Get last used mount if not currently mounted
            }

            // If a horse entity is available, apply full invincibility
            if (horse.IsValid())
            {
                horse.SetInvincible(true);
                // Disable critical hits and other lethal damage effects
                horse.SetTargetActionDisableFlag(13, true);
                horse.SetTargetActionDisableFlag(16, true);
                horse.SetTargetActionDisableFlag(17, true);
            }
        }

        virtual void OnDisable() override
        {
            // Find the relevant horse Ped as above
            Ped horse = Self::GetMount();
            if (!horse.IsValid())
            {
                horse = Self::GetPed().GetLastMount();
            }

            // If found, remove invincibility and restore flags so the horse can take damage normally
            if (horse.IsValid())
            {
                horse.SetInvincible(false);
                horse.SetTargetActionDisableFlag(13, false);
                horse.SetTargetActionDisableFlag(16, false);
                horse.SetTargetActionDisableFlag(17, false);
            }
        }
    };

    static HorseGodmode _HorseGodmode{
        "horsegodmode", "Godmode", "Makes your horse completely invincible."
    };
}
