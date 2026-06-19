// Copyright Epic Games, Inc. All Rights Reserved.

#include "TouchMe.h"

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogTouchMeRuntimeTrace);

static TAutoConsoleVariable<int32> CVarTouchMeRuntimeTrace(
	TEXT("tm.RuntimeTrace"),
	0,
	TEXT("Enables the very chatty TouchMe runtime trace logs."),
	ECVF_Default);

bool IsTouchMeRuntimeTraceEnabled()
{
	return CVarTouchMeRuntimeTrace.GetValueOnAnyThread() != 0;
}

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, TouchMe, "TouchMe" );
