#pragma once
#include "CrashSignatures.hpp"
#include "game/rdr/Natives.hpp"
#include <AsyncLogger/Logger.hpp>

namespace YimMenu::SafeNatives
{
	// safe wrapper for PED::SET_PED_CAN_RAGDOLL that checks for crash signatures
	inline void SET_PED_CAN_RAGDOLL_SAFE(int ped, bool toggle)
	{
		// check if this handle is a known crash signature
		if (CrashSignatures::IsKnownCrashHandle(ped))
		{
			LOG(WARNING) << "SET_PED_CAN_RAGDOLL: Blocked known crash signature handle " << ped;
			return;
		}
		
		// proceed with normal native call
		PED::SET_PED_CAN_RAGDOLL(ped, toggle);
	}
	
	// add more safe native wrappers here as needed
	// inline void SOME_OTHER_NATIVE_SAFE(params...) { ... }
}
