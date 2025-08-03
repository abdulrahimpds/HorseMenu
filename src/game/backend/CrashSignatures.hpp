#pragma once
#include <unordered_set>
#include <cstdint>

namespace YimMenu::CrashSignatures
{
	// crash signature database - known corrupted memory addresses from crash logs
	// these addresses have been confirmed to cause crashes when passed to RDR2 natives
	static const std::unordered_set<uintptr_t> KnownCrashAddresses = {
		// from most_relevant-cout.log
		0x24FC7516440,  // lines 161, 216, 271, 326, 381, 436, 491
		0x24FC751644A,  // lines 217, 272, 327, 382, 437
		0xF089413058,   // line 51
		0xF089412FC8,   // line 106
		
		// from cout2.log
		0xFFFFFFFFFFFFFFFF, // lines 107, 162, 256, 311, 495, 550 - most common crash signature
		0x90042,        // line 52
		0x10DE38DEB8,   // line 218
		0x10DE38DE28,   // line 273
		0x144E21D28,    // line 328
		0x144E21C98,    // line 383
		
		// from cout3.log
		0x1A600008AFC,  // line 376
		0x1A600008A6C,  // line 422
		
		// from cout4.log
		0x40,           // line 52
		0x42,           // lines 107, 163, 273, 328, 383
		0x43,           // line 218
		0x72B8,         // line 415
		0x108C,         // line 446
		0x23E8,         // line 478
		0x9C,           // line 510
		0x2440,         // lines 543, 644, 678
		0x2430,         // lines 576, 610
		0x2BD8,         // line 712
		0x2C10,         // line 746
		0x70,           // lines 778, 810

		// new signatures from recent crash logs (2024-01-XX session)
		0x0,            // null pointer access - very common crash pattern
		0x10,           // small offset access - common in multiple crashes
		0x7FF7000000CD, // corrupted memory access - successfully prevented by exception handler

		// new signatures from crash that bypassed exception handler
		0x8000098,      // large offset crash - caused actual game crash
		0x8000008,      // large offset crash - caused actual game crash
		0x8000000,      // base address pattern - prevent similar attacks

		// from cout6.log - comprehensive crash signature collection
		0x20,           // small offset access pattern
		0xC78E2FF270,   // corrupted high memory address
		0xC0,           // small offset access
		0x30,           // small offset access - very common
		0x24,           // small offset access
		0xA6,           // small offset access
		0xA8,           // small offset access
		0xAC,           // small offset access
		0x4,            // very small offset access
		0x8,            // very small offset access
		0x18,           // small offset access
		0x38,           // small offset access
		0x3C,           // small offset access
		0x120,          // medium offset access
		0x2C,           // small offset access
		0x28,           // small offset access
		0x34,           // small offset access
		0x44,           // small offset access
		0x14,           // small offset access
		0x1C,           // small offset access
		0x48,           // small offset access
		0x50,           // small offset access
		0x60,           // small offset access
		0xB8,           // medium offset access
		0xE0,           // medium offset access
		0xCCAEEFD6D0,   // corrupted high memory address
		0xC44B2822DC,   // corrupted high memory address
		0xC6AF0DB600,   // corrupted high memory address
		0xCCB50DD9D8,   // corrupted high memory address

		0x100815F12F4, // from crash log 2025-08-03 - attempted to read from invalid memory causing crash
		0x100815F1264, // from crash log 2025-08-03 - attempted to read from invalid memory causing crash

		0x248,  // from crash log 2025-08-03 – attempted to read from 0x248 causing game termination
		0x1b8,  // from crash log 2025-08-03 – attempted to read from 0x1b8 causing game termination

		0x1A0B8AE88C8, // from crash log 2025-08-03 - attempted to read from 0x1A0B8AE88C8 causing game termination
		0x1A0B8AE8838, // from crash log 2025-08-03 - attempted to read from 0x1A0B8AE8838 causing game termination
		0x2949,        // from crash log 2025-08-03 - attempted to execute at 0x2949 causing game termination

		// add new crash signatures here as they are discovered
		// format: 0x1234567890, // from crash log file - description
	};
	
	// check if a memory address is known to cause crashes
	inline bool IsKnownCrashAddress(uintptr_t address)
	{
		return KnownCrashAddresses.find(address) != KnownCrashAddresses.end();
	}
	
	// check if a pointer is known to cause crashes
	inline bool IsKnownCrashPointer(void* ptr)
	{
		if (!ptr) return false; // null pointers are handled elsewhere
		return IsKnownCrashAddress(reinterpret_cast<uintptr_t>(ptr));
	}
	
	// check if an entity handle corresponds to a known crash address
	inline bool IsKnownCrashHandle(int handle)
	{
		if (handle <= 0) return false; // invalid handles are handled elsewhere
		return IsKnownCrashAddress(static_cast<uintptr_t>(handle));
	}
}
