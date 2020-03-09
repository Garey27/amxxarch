#pragma once

#ifdef _WIN32 // WINDOWS
	#pragma warning(disable : 4005)
#else
	#ifdef __FUNCTION__
		#undef __FUNCTION__
	#endif
	#define __FUNCTION__ __func__
#endif // _WIN32

#define OBS_NONE	0
#define MAX_PATH	260

#include <vector>
#include <deque>
#include <thread>
#include <chrono>
#include <extdll.h>
#include <cbase.h>

#include "osdep.h"			// win32 vsnprintf, etc
#include "sdk_util.h"

#include <eiface.h>
#include <meta_api.h>

// regamedll API
#include <regamedll_api.h>
#include "mod_regamedll_api.h"
#include <mapinfo.h>
#include <studio.h>

// rehlds API
#include <rehlds_api.h>
#include "engine_rehlds_api.h"
#include "main.h"


#include "struct.h"

#undef DLLEXPORT
#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#define NOINLINE __declspec(noinline)
#else
#define DLLEXPORT __attribute__((visibility("default")))
#define NOINLINE __attribute__((noinline))
#define WINAPI		/* */
#endif

extern int UTIL_ReadFlags(const char* c);
