// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "StaticMeshResources.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundBase.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "../Gun/Gun.h"
#include "../Player/TMPlayerState.h"
#include "../TMGameplayStatics.h"
#include "../TouchMe.h"

#include <initializer_list>

namespace
{
	AGun* TMResolveActiveGun(const ATMCharacter* Character);
	bool TMIsAimingForWeaponBoneLock(const ATMCharacter* Character, const UAnimInstance* AnimInstance);

	bool TMIsTraceNoiseFunction(const FString& Name)
	{
		return Name.Contains(TEXT("Tick"))
			|| Name.Contains(TEXT("UpdateAnimation"))
			|| Name.Contains(TEXT("EvaluateGraphExposedInputs"))
			|| Name.Contains(TEXT("AnimGraph"))
			|| Name.Contains(TEXT("BlueprintThreadSafeUpdateAnimation"));
	}

	bool TMIsAimBridgeNoiseName(const FString& UpperName)
	{
		return UpperName.Contains(TEXT("AIMOFFSET"))
			|| UpperName.Contains(TEXT("AIM_OFFSET"))
			|| UpperName.Contains(TEXT("AIMINGROTATION"))
			|| UpperName.Contains(TEXT("AIMING_ROTATION"))
			|| UpperName.Contains(TEXT("AIMYAW"))
			|| UpperName.Contains(TEXT("AIM_YAW"))
			|| UpperName.Contains(TEXT("AIMSWEEP"))
			|| UpperName.Contains(TEXT("AIM_SWEEP"))
			|| UpperName.Contains(TEXT("UPDATEAIM"))
			|| UpperName.Contains(TEXT("UPDATE_AIM"))
			|| UpperName.Contains(TEXT("CALCULATEAIM"))
			|| UpperName.Contains(TEXT("CALCULATE_AIM"))
			|| UpperName.Contains(TEXT("GETAIM"))
			|| UpperName.Contains(TEXT("GET_AIM"));
	}

	bool GTMDebugLocalHandsScaleEnabled = true;
	bool GTMDebugLocalHandsScaleConsoleOverrideSet = false;
	float GTMDebugLocalHandsScale = 1.5f;
	FVector GTMDebugKrissNoAimOffsetPulseOffset = FVector::ZeroVector;
	double GTMDebugKrissNoAimOffsetPulseEndTime = 0.0;
	bool GTMDebugKrissNoAimOffsetPulseWasActive = false;
	const TCHAR* TMProjectMasterSoundClassPath = TEXT("/Game/MP_System_V3/Game/Sounds/SC_Master_MPS.SC_Master_MPS");
	const TCHAR* TMEngineMasterSoundClassPath = TEXT("/Engine/EngineSounds/Master.Master");
	const TCHAR* TMHeadshotHitmarkerSoundPaths[] =
	{
		TEXT("/Game/Battle_Royale_Game/Cues/Collects/Collect_Notification_Headshot_Gun_Shot_Ding_Perk_Touch_Hit_1_Cue.Collect_Notification_Headshot_Gun_Shot_Ding_Perk_Touch_Hit_1_Cue"),
		TEXT("/Game/Battle_Royale_Game/Cues/Collects/Collect_Notification_Headshot_Gun_Shot_Ding_Perk_Touch_Hit_2_Cue.Collect_Notification_Headshot_Gun_Shot_Ding_Perk_Touch_Hit_2_Cue")
	};
	const FName TMMPCameraFPSocketName(TEXT("Camera_FP"));
	const FName TMItemReleaseComponentName(TEXT("ItemRelease"));
	const FString TMCameraProxyMeshComponentPrefix(TEXT("CameraProxyMeshComponent"));
	const FString TMDrawFrustumComponentPrefix(TEXT("DrawFrustumComponent"));

	static TAutoConsoleVariable<float> CVarTMViewmodelLowerCm(
		TEXT("tm.ViewmodelLowerCm"),
		9.0f,
		TEXT("Moves the local weapon and hands lower in the camera view, in centimeters. 0 disables it."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelForwardCm(
		TEXT("tm.ViewmodelForwardCm"),
		15.0f,
		TEXT("Moves the local weapon and hands forward/backward in the camera view, in centimeters. Positive moves the camera forward."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelAimLowerCm(
		TEXT("tm.ViewmodelAimLowerCm"),
		5.23f,
		TEXT("Moves the local weapon and hands lower in ADS, in centimeters. Only applies while aiming."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelAimForwardCm(
		TEXT("tm.ViewmodelAimForwardCm"),
		12.0f,
		TEXT("Moves the local weapon and hands forward/backward in ADS, in centimeters. Only applies while aiming."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelAimRightCm(
		TEXT("tm.ViewmodelAimRightCm"),
		0.75f,
		TEXT("Moves the local weapon and hands right/left in ADS, in centimeters. Only applies while aiming."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelPitchDeg(
		TEXT("tm.ViewmodelPitchDeg"),
		0.0f,
		TEXT("Rotates the local weapon and hands forward/backward around pitch, in degrees. Positive pitches up."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelYawDeg(
		TEXT("tm.ViewmodelYawDeg"),
		0.0f,
		TEXT("Rotates the local weapon and hands left/right around yaw, in degrees."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelRollDeg(
		TEXT("tm.ViewmodelRollDeg"),
		0.0f,
		TEXT("Rolls the local weapon and hands around the view axis, in degrees."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelAimPitchDeg(
		TEXT("tm.ViewmodelAimPitchDeg"),
		0.0f,
		TEXT("Rotates the local weapon and hands forward/backward around pitch in ADS, in degrees. Only applies while aiming."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelAimYawDeg(
		TEXT("tm.ViewmodelAimYawDeg"),
		0.0f,
		TEXT("Rotates the local weapon and hands left/right around yaw in ADS, in degrees. Only applies while aiming."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelAimRollDeg(
		TEXT("tm.ViewmodelAimRollDeg"),
		0.0f,
		TEXT("Rolls the local weapon and hands around the view axis in ADS, in degrees. Only applies while aiming."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarTMViewmodelAimCenterReticle(
		TEXT("tm.ViewmodelAimCenterReticle"),
		1,
		TEXT("Centers the visible optic reticle/dot on the camera line while aiming."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelAimCenterMaxCm(
		TEXT("tm.ViewmodelAimCenterMaxCm"),
		5.0f,
		TEXT("Maximum per-frame camera correction for centering the visible ADS reticle, in centimeters."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelAimReticleMinForwardCm(
		TEXT("tm.ViewmodelAimReticleMinForwardCm"),
		24.0f,
		TEXT("Minimum forward eye relief from the camera to the visible ADS reticle, in centimeters."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMViewmodelAimReticleMaxPullBackCm(
		TEXT("tm.ViewmodelAimReticleMaxPullBackCm"),
		25.0f,
		TEXT("Maximum camera pull-back used to keep the visible ADS reticle out of the camera, in centimeters."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarTMShootADSUseReticleRay(
		TEXT("tm.ShootADSUseReticleRay"),
		1,
		TEXT("Uses the visible ADS reticle/dot as the camera aim ray while aiming."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarTMShootADSNoRandomSpread(
		TEXT("tm.ShootADSNoRandomSpread"),
		1,
		TEXT("Disables random bullet spread while aiming so ADS hits the visible reticle line."),
		ECVF_Default);

	struct FTMViewmodelConsoleProfile
	{
		float LowerCm = 9.0f;
		float ForwardCm = 15.0f;
		float AimLowerCm = 5.23f;
		float AimForwardCm = 12.0f;
		float AimRightCm = 0.75f;
		float PitchDeg = 0.0f;
		float YawDeg = 0.0f;
		float RollDeg = 0.0f;
		float AimPitchDeg = 0.0f;
		float AimYawDeg = 0.0f;
		float AimRollDeg = 0.0f;
		int32 AimCenterReticle = 1;
		float AimCenterMaxCm = 5.0f;
		float AimReticleMinForwardCm = 24.0f;
		float AimReticleMaxPullBackCm = 25.0f;
	};

	struct FTMViewmodelConsoleProfileRuntimeState
	{
		FString ActiveWeaponKey;
		bool bHasActiveWeaponKey = false;
		bool bHasInitialized = false;
	};

	TMap<FString, FTMViewmodelConsoleProfile> GTMViewmodelConsoleProfiles;
	TSet<FString> GTMViewmodelConsoleProfileConfigLookups;
	TSet<FString> GTMViewmodelConsoleProfileConfigWriteKeys;
	TWeakObjectPtr<const ATMCharacter> GTMViewmodelConsoleProfileCharacter;
	FTMViewmodelConsoleProfileRuntimeState GTMViewmodelConsoleProfileState;

	struct FTMPendingImpactHeadshot
	{
		FVector Location = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		FVector HeadshotSoundLocation = FVector::ZeroVector;
		const UPhysicalMaterial* PhysicalMaterial = nullptr;
		double TimeSeconds = 0.0;
		bool bHeadshot = false;
		bool bHasHeadshotSoundLocation = false;
	};

	TMap<TWeakObjectPtr<ATMCharacter>, TArray<FTMPendingImpactHeadshot>> GTMPendingImpactHeadshots;
	constexpr double TMPendingImpactHeadshotMaxAgeSeconds = 2.0;
	constexpr double TMPendingImpactLocationToleranceSq = 2500.0;
	constexpr int32 TMPendingImpactHeadshotMaxPerCharacter = 8;

	bool TMNearlyEqualViewmodelValue(const float Left, const float Right)
	{
		return FMath::IsNearlyEqual(Left, Right, 0.001f);
	}

	bool TMViewmodelConsoleProfilesEqual(const FTMViewmodelConsoleProfile& Left, const FTMViewmodelConsoleProfile& Right)
	{
		return TMNearlyEqualViewmodelValue(Left.LowerCm, Right.LowerCm)
			&& TMNearlyEqualViewmodelValue(Left.ForwardCm, Right.ForwardCm)
			&& TMNearlyEqualViewmodelValue(Left.AimLowerCm, Right.AimLowerCm)
			&& TMNearlyEqualViewmodelValue(Left.AimForwardCm, Right.AimForwardCm)
			&& TMNearlyEqualViewmodelValue(Left.AimRightCm, Right.AimRightCm)
			&& TMNearlyEqualViewmodelValue(Left.PitchDeg, Right.PitchDeg)
			&& TMNearlyEqualViewmodelValue(Left.YawDeg, Right.YawDeg)
			&& TMNearlyEqualViewmodelValue(Left.RollDeg, Right.RollDeg)
			&& TMNearlyEqualViewmodelValue(Left.AimPitchDeg, Right.AimPitchDeg)
			&& TMNearlyEqualViewmodelValue(Left.AimYawDeg, Right.AimYawDeg)
			&& TMNearlyEqualViewmodelValue(Left.AimRollDeg, Right.AimRollDeg)
			&& Left.AimCenterReticle == Right.AimCenterReticle
			&& TMNearlyEqualViewmodelValue(Left.AimCenterMaxCm, Right.AimCenterMaxCm)
			&& TMNearlyEqualViewmodelValue(Left.AimReticleMinForwardCm, Right.AimReticleMinForwardCm)
			&& TMNearlyEqualViewmodelValue(Left.AimReticleMaxPullBackCm, Right.AimReticleMaxPullBackCm);
	}

	FTMViewmodelConsoleProfile TMReadViewmodelConsoleProfileFromCVars()
	{
		FTMViewmodelConsoleProfile Profile;
		Profile.LowerCm = CVarTMViewmodelLowerCm.GetValueOnGameThread();
		Profile.ForwardCm = CVarTMViewmodelForwardCm.GetValueOnGameThread();
		Profile.AimLowerCm = CVarTMViewmodelAimLowerCm.GetValueOnGameThread();
		Profile.AimForwardCm = CVarTMViewmodelAimForwardCm.GetValueOnGameThread();
		Profile.AimRightCm = CVarTMViewmodelAimRightCm.GetValueOnGameThread();
		Profile.PitchDeg = CVarTMViewmodelPitchDeg.GetValueOnGameThread();
		Profile.YawDeg = CVarTMViewmodelYawDeg.GetValueOnGameThread();
		Profile.RollDeg = CVarTMViewmodelRollDeg.GetValueOnGameThread();
		Profile.AimPitchDeg = CVarTMViewmodelAimPitchDeg.GetValueOnGameThread();
		Profile.AimYawDeg = CVarTMViewmodelAimYawDeg.GetValueOnGameThread();
		Profile.AimRollDeg = CVarTMViewmodelAimRollDeg.GetValueOnGameThread();
		Profile.AimCenterReticle = CVarTMViewmodelAimCenterReticle.GetValueOnGameThread();
		Profile.AimCenterMaxCm = CVarTMViewmodelAimCenterMaxCm.GetValueOnGameThread();
		Profile.AimReticleMinForwardCm = CVarTMViewmodelAimReticleMinForwardCm.GetValueOnGameThread();
		Profile.AimReticleMaxPullBackCm = CVarTMViewmodelAimReticleMaxPullBackCm.GetValueOnGameThread();
		return Profile;
	}

	void TMSetFloatConsoleVariable(const TCHAR* Name, const float Value)
	{
		if (IConsoleVariable* ConsoleVariable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			ConsoleVariable->Set(Value, ECVF_SetByConsole);
		}
	}

	void TMSetIntConsoleVariable(const TCHAR* Name, const int32 Value)
	{
		if (IConsoleVariable* ConsoleVariable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			ConsoleVariable->Set(Value, ECVF_SetByConsole);
		}
	}

	void TMApplyViewmodelConsoleProfileToCVars(const FTMViewmodelConsoleProfile& Profile)
	{
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelLowerCm"), Profile.LowerCm);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelForwardCm"), Profile.ForwardCm);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelAimLowerCm"), Profile.AimLowerCm);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelAimForwardCm"), Profile.AimForwardCm);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelAimRightCm"), Profile.AimRightCm);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelPitchDeg"), Profile.PitchDeg);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelYawDeg"), Profile.YawDeg);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelRollDeg"), Profile.RollDeg);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelAimPitchDeg"), Profile.AimPitchDeg);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelAimYawDeg"), Profile.AimYawDeg);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelAimRollDeg"), Profile.AimRollDeg);
		TMSetIntConsoleVariable(TEXT("tm.ViewmodelAimCenterReticle"), Profile.AimCenterReticle);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelAimCenterMaxCm"), Profile.AimCenterMaxCm);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelAimReticleMinForwardCm"), Profile.AimReticleMinForwardCm);
		TMSetFloatConsoleVariable(TEXT("tm.ViewmodelAimReticleMaxPullBackCm"), Profile.AimReticleMaxPullBackCm);
	}

	FString TMGetViewmodelConsoleProfilesIniPath()
	{
		return FPaths::Combine(FPaths::GeneratedConfigDir(), TEXT("TouchMeViewmodelProfiles.ini"));
	}

	FString TMSanitizeViewmodelProfileSectionToken(FString Token)
	{
		for (TCHAR& Character : Token)
		{
			if (!FChar::IsAlnum(Character))
			{
				Character = TEXT('_');
			}
		}
		return Token;
	}

	FString TMGetViewmodelConsoleProfileSection(const FString& WeaponKey)
	{
		return FString::Printf(TEXT("TMViewmodelConsoleProfile.%s"), *TMSanitizeViewmodelProfileSectionToken(WeaponKey));
	}

	FString TMGetViewmodelConsoleProfileConfigWriteKey(const FString& WeaponKey)
	{
		return TMGetViewmodelConsoleProfilesIniPath() + TEXT("|") + WeaponKey;
	}

	FString TMGetViewmodelConsoleProfileWeaponKey(const AGun* ActiveGun)
	{
		const UClass* WeaponClass = ActiveGun ? ActiveGun->GetClass() : nullptr;
		return WeaponClass ? WeaponClass->GetPathName() : FString();
	}

	bool TMReadViewmodelConsoleProfileFromConfig(const FString& WeaponKey, FTMViewmodelConsoleProfile& OutProfile)
	{
		if (!GConfig || WeaponKey.IsEmpty())
		{
			return false;
		}

		const FString IniPath = TMGetViewmodelConsoleProfilesIniPath();
		if (!FPaths::FileExists(IniPath))
		{
			return false;
		}

		GConfig->LoadFile(IniPath);

		const FString Section = TMGetViewmodelConsoleProfileSection(WeaponKey);
		FString StoredWeaponKey;
		if (!GConfig->GetString(*Section, TEXT("WeaponClassPath"), StoredWeaponKey, IniPath) || StoredWeaponKey != WeaponKey)
		{
			return false;
		}

		bool bReadAnyValue = false;
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("LowerCm"), OutProfile.LowerCm, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("ForwardCm"), OutProfile.ForwardCm, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("AimLowerCm"), OutProfile.AimLowerCm, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("AimForwardCm"), OutProfile.AimForwardCm, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("AimRightCm"), OutProfile.AimRightCm, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("PitchDeg"), OutProfile.PitchDeg, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("YawDeg"), OutProfile.YawDeg, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("RollDeg"), OutProfile.RollDeg, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("AimPitchDeg"), OutProfile.AimPitchDeg, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("AimYawDeg"), OutProfile.AimYawDeg, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("AimRollDeg"), OutProfile.AimRollDeg, IniPath);
		bReadAnyValue |= GConfig->GetInt(*Section, TEXT("AimCenterReticle"), OutProfile.AimCenterReticle, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("AimCenterMaxCm"), OutProfile.AimCenterMaxCm, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("AimReticleMinForwardCm"), OutProfile.AimReticleMinForwardCm, IniPath);
		bReadAnyValue |= GConfig->GetFloat(*Section, TEXT("AimReticleMaxPullBackCm"), OutProfile.AimReticleMaxPullBackCm, IniPath);
		return bReadAnyValue;
	}

	void TMSaveViewmodelConsoleProfileToConfig(const FString& WeaponKey, const FTMViewmodelConsoleProfile& Profile)
	{
		if (WeaponKey.IsEmpty())
		{
			return;
		}

		GTMViewmodelConsoleProfiles.Add(WeaponKey, Profile);

		const FString IniPath = TMGetViewmodelConsoleProfilesIniPath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(IniPath), true);

		TArray<FString> WeaponKeys;
		GTMViewmodelConsoleProfiles.GenerateKeyArray(WeaponKeys);
		WeaponKeys.Sort();

		FString Output = TEXT("; TouchMe viewmodel console profiles\n\n");
		for (const FString& SavedWeaponKey : WeaponKeys)
		{
			const FTMViewmodelConsoleProfile* SavedProfile = GTMViewmodelConsoleProfiles.Find(SavedWeaponKey);
			if (!SavedProfile)
			{
				continue;
			}

			Output += FString::Printf(TEXT("[%s]\n"), *TMGetViewmodelConsoleProfileSection(SavedWeaponKey));
			Output += FString::Printf(TEXT("WeaponClassPath=%s\n"), *SavedWeaponKey);
			Output += FString::Printf(TEXT("LowerCm=%.6f\n"), SavedProfile->LowerCm);
			Output += FString::Printf(TEXT("ForwardCm=%.6f\n"), SavedProfile->ForwardCm);
			Output += FString::Printf(TEXT("AimLowerCm=%.6f\n"), SavedProfile->AimLowerCm);
			Output += FString::Printf(TEXT("AimForwardCm=%.6f\n"), SavedProfile->AimForwardCm);
			Output += FString::Printf(TEXT("AimRightCm=%.6f\n"), SavedProfile->AimRightCm);
			Output += FString::Printf(TEXT("PitchDeg=%.6f\n"), SavedProfile->PitchDeg);
			Output += FString::Printf(TEXT("YawDeg=%.6f\n"), SavedProfile->YawDeg);
			Output += FString::Printf(TEXT("RollDeg=%.6f\n"), SavedProfile->RollDeg);
			Output += FString::Printf(TEXT("AimPitchDeg=%.6f\n"), SavedProfile->AimPitchDeg);
			Output += FString::Printf(TEXT("AimYawDeg=%.6f\n"), SavedProfile->AimYawDeg);
			Output += FString::Printf(TEXT("AimRollDeg=%.6f\n"), SavedProfile->AimRollDeg);
			Output += FString::Printf(TEXT("AimCenterReticle=%d\n"), SavedProfile->AimCenterReticle);
			Output += FString::Printf(TEXT("AimCenterMaxCm=%.6f\n"), SavedProfile->AimCenterMaxCm);
			Output += FString::Printf(TEXT("AimReticleMinForwardCm=%.6f\n"), SavedProfile->AimReticleMinForwardCm);
			Output += FString::Printf(TEXT("AimReticleMaxPullBackCm=%.6f\n\n"), SavedProfile->AimReticleMaxPullBackCm);
			GTMViewmodelConsoleProfileConfigWriteKeys.Add(TMGetViewmodelConsoleProfileConfigWriteKey(SavedWeaponKey));
		}

		FFileHelper::SaveStringToFile(Output, *IniPath);
	}

	FTMViewmodelConsoleProfile TMGetOrCreateViewmodelConsoleProfile(const FString& WeaponKey, const bool bCaptureCurrentIfMissing)
	{
		if (const FTMViewmodelConsoleProfile* ExistingProfile = GTMViewmodelConsoleProfiles.Find(WeaponKey))
		{
			return *ExistingProfile;
		}

		FTMViewmodelConsoleProfile Profile;
		if (!GTMViewmodelConsoleProfileConfigLookups.Contains(WeaponKey))
		{
			GTMViewmodelConsoleProfileConfigLookups.Add(WeaponKey);
			if (TMReadViewmodelConsoleProfileFromConfig(WeaponKey, Profile))
			{
				GTMViewmodelConsoleProfiles.Add(WeaponKey, Profile);
				GTMViewmodelConsoleProfileConfigWriteKeys.Add(TMGetViewmodelConsoleProfileConfigWriteKey(WeaponKey));
				return Profile;
			}
		}

		Profile = bCaptureCurrentIfMissing ? TMReadViewmodelConsoleProfileFromCVars() : FTMViewmodelConsoleProfile();
		GTMViewmodelConsoleProfiles.Add(WeaponKey, Profile);
		TMSaveViewmodelConsoleProfileToConfig(WeaponKey, Profile);
		return Profile;
	}

	void TMSaveCurrentViewmodelConsoleProfileForWeapon(const FString& WeaponKey)
	{
		if (WeaponKey.IsEmpty())
		{
			return;
		}

		const FTMViewmodelConsoleProfile CurrentProfile = TMReadViewmodelConsoleProfileFromCVars();
		const FTMViewmodelConsoleProfile* ExistingProfile = GTMViewmodelConsoleProfiles.Find(WeaponKey);
		if (ExistingProfile
			&& TMViewmodelConsoleProfilesEqual(*ExistingProfile, CurrentProfile)
			&& GTMViewmodelConsoleProfileConfigWriteKeys.Contains(TMGetViewmodelConsoleProfileConfigWriteKey(WeaponKey)))
		{
			return;
		}

		GTMViewmodelConsoleProfiles.Add(WeaponKey, CurrentProfile);
		GTMViewmodelConsoleProfileConfigLookups.Add(WeaponKey);
		TMSaveViewmodelConsoleProfileToConfig(WeaponKey, CurrentProfile);
	}

	void TMUpdateViewmodelConsoleWeaponProfile(const ATMCharacter* Character)
	{
		if (!Character || !Character->IsLocallyControlled())
		{
			return;
		}

		if (GTMViewmodelConsoleProfileCharacter.Get() != Character)
		{
			if (GTMViewmodelConsoleProfileState.bHasActiveWeaponKey)
			{
				TMSaveCurrentViewmodelConsoleProfileForWeapon(GTMViewmodelConsoleProfileState.ActiveWeaponKey);
			}

			GTMViewmodelConsoleProfileCharacter = Character;
			GTMViewmodelConsoleProfileState = FTMViewmodelConsoleProfileRuntimeState();
		}

		const FString NewWeaponKey = TMGetViewmodelConsoleProfileWeaponKey(TMResolveActiveGun(Character));
		if (NewWeaponKey.IsEmpty())
		{
			if (GTMViewmodelConsoleProfileState.bHasActiveWeaponKey)
			{
				TMSaveCurrentViewmodelConsoleProfileForWeapon(GTMViewmodelConsoleProfileState.ActiveWeaponKey);
				GTMViewmodelConsoleProfileState.ActiveWeaponKey.Reset();
				GTMViewmodelConsoleProfileState.bHasActiveWeaponKey = false;
			}
			return;
		}

		if (!GTMViewmodelConsoleProfileState.bHasActiveWeaponKey || GTMViewmodelConsoleProfileState.ActiveWeaponKey != NewWeaponKey)
		{
			if (GTMViewmodelConsoleProfileState.bHasActiveWeaponKey)
			{
				TMSaveCurrentViewmodelConsoleProfileForWeapon(GTMViewmodelConsoleProfileState.ActiveWeaponKey);
			}

			const bool bCaptureCurrentIfMissing = !GTMViewmodelConsoleProfileState.bHasInitialized;
			const FTMViewmodelConsoleProfile NewProfile = TMGetOrCreateViewmodelConsoleProfile(NewWeaponKey, bCaptureCurrentIfMissing);
			TMApplyViewmodelConsoleProfileToCVars(NewProfile);
			GTMViewmodelConsoleProfileState.ActiveWeaponKey = NewWeaponKey;
			GTMViewmodelConsoleProfileState.bHasActiveWeaponKey = true;
			GTMViewmodelConsoleProfileState.bHasInitialized = true;
			return;
		}

		TMSaveCurrentViewmodelConsoleProfileForWeapon(NewWeaponKey);
	}

	void TMFlushViewmodelConsoleWeaponProfile(const ATMCharacter* Character)
	{
		if (GTMViewmodelConsoleProfileCharacter.Get() != Character || !GTMViewmodelConsoleProfileState.bHasActiveWeaponKey)
		{
			return;
		}

		TMSaveCurrentViewmodelConsoleProfileForWeapon(GTMViewmodelConsoleProfileState.ActiveWeaponKey);
		GTMViewmodelConsoleProfileState.ActiveWeaponKey.Reset();
		GTMViewmodelConsoleProfileState.bHasActiveWeaponKey = false;
	}

	struct FTMDebugBoneScaleDelegate
	{
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		FDelegateHandle Handle;
		bool bIncludeForearmDescendants = false;
		bool bIncludeWeaponBone = false;
	};

	TArray<FTMDebugBoneScaleDelegate> GTMDebugBoneScaleDelegates;
	TSet<uint32> GTMDebugBoneScaleReportedMeshIds;

	bool TMTextLooksLikeReticleSlot(const FString& Text)
	{
		const FString UpperText = Text.ToUpper();
		return UpperText.Contains(TEXT("RETICLE"))
			|| UpperText.Contains(TEXT("DOTSIGHTDOT"))
			|| UpperText.Contains(TEXT("SIGHTDOT"))
			|| UpperText.Contains(TEXT("SIGHT_DOT"))
			|| UpperText.Contains(TEXT("I_DOT"));
	}

	bool TMMaterialSlotLooksLikeReticle(
		const UStaticMeshComponent* Component,
		const UStaticMesh* StaticMesh,
		const int32 MaterialIndex)
	{
		if (!Component || !StaticMesh || MaterialIndex < 0)
		{
			return false;
		}

		const TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();
		if (StaticMaterials.IsValidIndex(MaterialIndex)
			&& TMTextLooksLikeReticleSlot(StaticMaterials[MaterialIndex].MaterialSlotName.ToString()))
		{
			return true;
		}

		if (const UMaterialInterface* ComponentMaterial = Component->GetMaterial(MaterialIndex))
		{
			if (TMTextLooksLikeReticleSlot(ComponentMaterial->GetPathName()))
			{
				return true;
			}
		}

		if (StaticMaterials.IsValidIndex(MaterialIndex))
		{
			if (const UMaterialInterface* MeshMaterial = StaticMaterials[MaterialIndex].MaterialInterface)
			{
				if (TMTextLooksLikeReticleSlot(MeshMaterial->GetPathName()))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool TMTryGetKnownReticleLocalCenter(const UStaticMesh* StaticMesh, FVector& OutLocalCenter)
	{
		if (!StaticMesh)
		{
			return false;
		}

		const FString MeshPath = StaticMesh->GetPathName().ToUpper();
		if (MeshPath.Contains(TEXT("SM_DOTSIGHT")))
		{
			OutLocalCenter = FVector(4.8730764389, 0.0000027418, 4.1800785065);
			return true;
		}

		return false;
	}

	bool TMGetStaticMeshMaterialSectionLocalCenter(
		const UStaticMeshComponent* Component,
		const int32 MaterialIndex,
		FVector& OutLocalCenter)
	{
		if (!Component || MaterialIndex < 0)
		{
			return false;
		}

		const UStaticMesh* StaticMesh = Component->GetStaticMesh();
		if (!StaticMesh)
		{
			return false;
		}

		if (TMTryGetKnownReticleLocalCenter(StaticMesh, OutLocalCenter))
		{
			return true;
		}

		const FStaticMeshRenderData* RenderData = StaticMesh->GetRenderData();
		if (RenderData && RenderData->LODResources.Num() > 0)
		{
			const FStaticMeshLODResources& LODResources = RenderData->LODResources[0];
			const FPositionVertexBuffer& PositionVertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
			const FIndexArrayView Indices = LODResources.IndexBuffer.GetArrayView();
			const uint32 VertexCount = PositionVertexBuffer.GetNumVertices();
			FBox LocalBounds(EForceInit::ForceInit);

			for (const FStaticMeshSection& Section : LODResources.Sections)
			{
				if (Section.MaterialIndex != MaterialIndex)
				{
					continue;
				}

				const uint32 SectionFirstIndex = Section.FirstIndex;
				const uint32 SectionIndexCount = Section.NumTriangles * 3;
				for (uint32 SectionIndexOffset = 0; SectionIndexOffset < SectionIndexCount; ++SectionIndexOffset)
				{
					const int32 IndexBufferIndex = static_cast<int32>(SectionFirstIndex + SectionIndexOffset);
					if (IndexBufferIndex < 0 || IndexBufferIndex >= Indices.Num())
					{
						continue;
					}

					const uint32 VertexIndex = Indices[IndexBufferIndex];
					if (VertexIndex >= VertexCount)
					{
						continue;
					}

					LocalBounds += FVector(PositionVertexBuffer.VertexPosition(VertexIndex));
				}
			}

			if (LocalBounds.IsValid)
			{
				OutLocalCenter = LocalBounds.GetCenter();
				return true;
			}
		}

		return TMTryGetKnownReticleLocalCenter(StaticMesh, OutLocalCenter);
	}

	bool TMGetVisibleReticleWorldLocation(const AGun* ActiveGun, FVector& OutWorldLocation)
	{
		if (!ActiveGun)
		{
			return false;
		}

		TArray<UStaticMeshComponent*> StaticMeshComponents;
		ActiveGun->GetComponents(StaticMeshComponents);
		for (const UStaticMeshComponent* Component : StaticMeshComponents)
		{
			if (!Component || !Component->IsVisible())
			{
				continue;
			}

			const UStaticMesh* StaticMesh = Component->GetStaticMesh();
			if (!StaticMesh)
			{
				continue;
			}

			const int32 MaterialCount = Component->GetNumMaterials();
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				if (!TMMaterialSlotLooksLikeReticle(Component, StaticMesh, MaterialIndex))
				{
					continue;
				}

				FVector LocalCenter = FVector::ZeroVector;
				if (!TMGetStaticMeshMaterialSectionLocalCenter(Component, MaterialIndex, LocalCenter))
				{
					continue;
				}

				OutWorldLocation = Component->GetComponentTransform().TransformPosition(LocalCenter);
				return true;
			}
		}

		return false;
	}

	bool TMGetADSCenterTargetWorldLocation(const ATMCharacter* Character, FVector& OutWorldLocation)
	{
		AGun* ActiveGun = TMResolveActiveGun(Character);
		if (!ActiveGun)
		{
			return false;
		}

		if (TMGetVisibleReticleWorldLocation(ActiveGun, OutWorldLocation))
		{
			return true;
		}

		FTransform ADSSocketWorldTransform = FTransform::Identity;
		if (ActiveGun->GetADSSocketWorldTransform(ADSSocketWorldTransform))
		{
			OutWorldLocation = ADSSocketWorldTransform.GetLocation();
			return true;
		}

		return false;
	}

	void TMCenterADSTargetOnCameraLine(const ATMCharacter* Character, UCameraComponent* CameraComponent)
	{
		if (!Character || !CameraComponent || CVarTMViewmodelAimCenterReticle.GetValueOnGameThread() == 0)
		{
			return;
		}

		FVector TargetWorldLocation = FVector::ZeroVector;
		if (!TMGetADSCenterTargetWorldLocation(Character, TargetWorldLocation))
		{
			return;
		}

		const FTransform CameraWorldTransform = CameraComponent->GetComponentTransform();
		const FVector CameraWorldLocation = CameraWorldTransform.GetLocation();
		const FVector CameraRight = CameraWorldTransform.GetUnitAxis(EAxis::Y);
		const FVector CameraUp = CameraWorldTransform.GetUnitAxis(EAxis::Z);
		const FVector CameraForward = CameraWorldTransform.GetUnitAxis(EAxis::X);
		const FVector CameraToTarget = TargetWorldLocation - CameraWorldLocation;
		FVector CameraPlaneOffset =
			CameraRight * FVector::DotProduct(CameraToTarget, CameraRight)
			+ CameraUp * FVector::DotProduct(CameraToTarget, CameraUp);

		const float MaxCorrectionCm = FMath::Max(0.0f, CVarTMViewmodelAimCenterMaxCm.GetValueOnGameThread());
		if (MaxCorrectionCm > 0.0f && CameraPlaneOffset.SizeSquared() > FMath::Square(MaxCorrectionCm))
		{
			CameraPlaneOffset = FVector::ZeroVector;
		}

		FVector CameraDepthOffset = FVector::ZeroVector;
		const float TargetForwardCm = FVector::DotProduct(CameraToTarget, CameraForward);
		const float MinForwardCm = FMath::Max(0.0f, CVarTMViewmodelAimReticleMinForwardCm.GetValueOnGameThread());
		if (MinForwardCm > 0.0f && TargetForwardCm > UE_SMALL_NUMBER && TargetForwardCm < MinForwardCm)
		{
			const float MaxPullBackCm = FMath::Max(0.0f, CVarTMViewmodelAimReticleMaxPullBackCm.GetValueOnGameThread());
			const float PullBackCm = FMath::Min(MinForwardCm - TargetForwardCm, MaxPullBackCm);
			CameraDepthOffset = -CameraForward * PullBackCm;
		}

		const FVector CameraWorldOffset = CameraPlaneOffset + CameraDepthOffset;
		if (CameraWorldOffset.IsNearlyZero(KINDA_SMALL_NUMBER))
		{
			return;
		}

		const FVector CorrectedWorldLocation = CameraWorldLocation + CameraWorldOffset;
		const USceneComponent* ParentComponent = CameraComponent->GetAttachParent();
		const FVector CorrectedRelativeLocation = ParentComponent
			? ParentComponent->GetComponentTransform().InverseTransformPosition(CorrectedWorldLocation)
			: CorrectedWorldLocation;
		if (!CameraComponent->GetRelativeLocation().Equals(CorrectedRelativeLocation, KINDA_SMALL_NUMBER))
		{
			CameraComponent->SetRelativeLocation(
				CorrectedRelativeLocation,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
	}

	void TMEnsureMPCameraBoomAttachment(ATMCharacter* Character)
	{
		if (!Character)
		{
			return;
		}

		USkeletalMeshComponent* Mesh = Character->GetMesh();
		USpringArmComponent* CameraBoom = Character->FindComponentByClass<USpringArmComponent>();
		if (!Mesh || !CameraBoom || !Mesh->DoesSocketExist(TMMPCameraFPSocketName))
		{
			return;
		}

		if (CameraBoom->GetAttachParent() == Mesh && CameraBoom->GetAttachSocketName() == TMMPCameraFPSocketName)
		{
			return;
		}

		CameraBoom->AttachToComponent(
			Mesh,
			FAttachmentTransformRules::KeepRelativeTransform,
			TMMPCameraFPSocketName);
		CameraBoom->SetRelativeLocation(FVector::ZeroVector);
		CameraBoom->SetRelativeRotation(FRotator(0.0f, 90.0f, -90.0f));
	}

	void TMApplyViewmodelCameraOffset(ATMCharacter* Character)
	{
		if (!Character || !Character->IsLocallyControlled())
		{
			return;
		}

		UCameraComponent* CameraComponent = Character->FindComponentByClass<UCameraComponent>();
		if (!CameraComponent)
		{
			return;
		}

		const UAnimInstance* AnimInstance = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
		const bool bAiming = TMIsAimingForWeaponBoneLock(Character, AnimInstance);
		const FVector DesiredRelativeLocation(
			bAiming ? CVarTMViewmodelAimForwardCm.GetValueOnGameThread() : CVarTMViewmodelForwardCm.GetValueOnGameThread(),
			bAiming ? CVarTMViewmodelAimRightCm.GetValueOnGameThread() : 0.0f,
			bAiming ? CVarTMViewmodelAimLowerCm.GetValueOnGameThread() : CVarTMViewmodelLowerCm.GetValueOnGameThread());
		if (!CameraComponent->GetRelativeLocation().Equals(DesiredRelativeLocation, KINDA_SMALL_NUMBER))
		{
			CameraComponent->SetRelativeLocation(
				DesiredRelativeLocation,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}

		if (bAiming)
		{
			TMCenterADSTargetOnCameraLine(Character, CameraComponent);
		}
	}

	bool TMIsDebugLocalHandsScaleEnabled()
	{
#if WITH_EDITOR
		static double LastFileCheckTime = -1.0;
		static bool bScaleFilePresent = false;
		static bool bLastReportedEnabled = false;
		static bool bReportedConsoleCommandRegistration = false;

		if (!bReportedConsoleCommandRegistration)
		{
			bReportedConsoleCommandRegistration = true;
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMDebugLocalHandsScale] ConsoleCommandRegistered=%d Command=tm.DebugLocalHandsScale"),
				IConsoleManager::Get().FindConsoleObject(TEXT("tm.DebugLocalHandsScale")) ? 1 : 0);
		}

		const double Now = FPlatformTime::Seconds();
		if (LastFileCheckTime < 0.0 || Now - LastFileCheckTime > 0.25)
		{
			LastFileCheckTime = Now;

			FString FileContents;
			const FString ToggleFilePath = FPaths::ProjectSavedDir() / TEXT("Codex/DebugLocalHandsScale.txt");
			bScaleFilePresent = FFileHelper::LoadFileToString(FileContents, *ToggleFilePath);
			if (bScaleFilePresent)
			{
				FileContents = FileContents.TrimStartAndEnd();
				if (!FileContents.IsEmpty())
				{
					const float ParsedScale = FCString::Atof(*FileContents);
					if (ParsedScale > UE_SMALL_NUMBER)
					{
						GTMDebugLocalHandsScale = ParsedScale;
					}
				}
			}
		}

		const bool bEnabled = GTMDebugLocalHandsScaleConsoleOverrideSet ? GTMDebugLocalHandsScaleEnabled : true;
		if (bEnabled != bLastReportedEnabled)
		{
			bLastReportedEnabled = bEnabled;
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMDebugLocalHandsScale] RuntimeDefault Enabled=%d BaseScale=%.3f ScaleFile=%d ConsoleOverride=%d"),
				bEnabled ? 1 : 0,
				GTMDebugLocalHandsScale,
				bScaleFilePresent ? 1 : 0,
				GTMDebugLocalHandsScaleConsoleOverrideSet ? 1 : 0);
		}
		return bEnabled;
#else
		return GTMDebugLocalHandsScaleEnabled;
#endif
	}

	bool TMIsAimBridgeFunction(const UFunction* Function)
	{
		if (!Function)
		{
			return false;
		}

		const FString UpperName = Function->GetName().ToUpper();
		if (TMIsAimBridgeNoiseName(UpperName))
		{
			return false;
		}

		return UpperName.Contains(TEXT("AIM"))
			|| UpperName.Contains(TEXT("ADS"))
			|| UpperName.Contains(TEXT("SIGHT"))
			|| UpperName.Contains(TEXT("SCOPE"));
	}

	bool TMIsMPSServerAimFunction(const UFunction* Function)
	{
		return Function && Function->GetName().Equals(TEXT("Svr_Aim"), ESearchCase::CaseSensitive);
	}

	bool TMIsAimStateBoolName(const FString& UpperName)
	{
		if (TMIsAimBridgeNoiseName(UpperName))
		{
			return false;
		}

		return UpperName.Contains(TEXT("AIM"))
			|| UpperName.Contains(TEXT("ADS"))
			|| UpperName.Contains(TEXT("SIGHT"))
			|| UpperName.Contains(TEXT("SCOPE"));
	}

	bool TMReadAimStateBoolProperty(const UObject* Object, bool& bOutAiming)
	{
		if (!Object)
		{
			return false;
		}

		static const FName PriorityPropertyNames[] =
		{
			TEXT("bIsAiming"),
			TEXT("IsAiming"),
			TEXT("bAiming"),
			TEXT("Aiming"),
			TEXT("bADS"),
			TEXT("IsADS"),
			TEXT("bIsADS"),
			TEXT("bAim"),
			TEXT("Aim")
		};

		for (const FName PropertyName : PriorityPropertyNames)
		{
			const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
			if (BoolProperty)
			{
				bOutAiming = BoolProperty->GetPropertyValue_InContainer(Object);
				return true;
			}
		}

		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FBoolProperty* BoolProperty = CastField<FBoolProperty>(*It);
			if (!BoolProperty || !TMIsAimStateBoolName(BoolProperty->GetName().ToUpper()))
			{
				continue;
			}

			bOutAiming = BoolProperty->GetPropertyValue_InContainer(Object);
			return true;
		}

		return false;
	}

	bool TMExtractAimStateFromParameters(const UFunction* Function, void* Parameters, bool& bOutAiming)
	{
		if (!Function || !Parameters)
		{
			return false;
		}

		const FBoolProperty* OnlyBoolParameter = nullptr;
		int32 BoolParameterCount = 0;
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			const FBoolProperty* BoolProperty = CastField<FBoolProperty>(*It);
			if (!BoolProperty
				|| !BoolProperty->HasAnyPropertyFlags(CPF_Parm)
				|| BoolProperty->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}

			++BoolParameterCount;
			OnlyBoolParameter = BoolProperty;

			const FString UpperName = BoolProperty->GetName().ToUpper();
			if (TMIsAimStateBoolName(UpperName)
				|| UpperName.Contains(TEXT("VALUE"))
				|| UpperName.Contains(TEXT("PRESSED"))
				|| UpperName.Contains(TEXT("ACTIVE"))
				|| UpperName.Contains(TEXT("ENABLE"))
				|| UpperName.Contains(TEXT("STATE")))
			{
				bOutAiming = BoolProperty->GetPropertyValue(BoolProperty->ContainerPtrToValuePtr<void>(Parameters));
				return true;
			}
		}

		if (BoolParameterCount == 1 && OnlyBoolParameter)
		{
			bOutAiming = OnlyBoolParameter->GetPropertyValue(OnlyBoolParameter->ContainerPtrToValuePtr<void>(Parameters));
			return true;
		}

		return false;
	}

	bool TMReadNumericProperty(const UObject* Object, const FName PropertyName, double& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!NumericProperty)
		{
			return false;
		}

		const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
		OutValue = NumericProperty->IsFloatingPoint()
			? NumericProperty->GetFloatingPointPropertyValue(ValuePtr)
			: static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
		return true;
	}

	bool TMReadEnumLikeProperty(const UObject* Object, const FName PropertyName, int64& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		const FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
		if (!Property)
		{
			return false;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			OutValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			return true;
		}

		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			OutValue = ByteProperty->GetPropertyValue(ValuePtr);
			return true;
		}

		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			OutValue = NumericProperty->IsFloatingPoint()
				? FMath::RoundToInt64(NumericProperty->GetFloatingPointPropertyValue(ValuePtr))
				: NumericProperty->GetSignedIntPropertyValue(ValuePtr);
			return true;
		}

		return false;
	}

	bool TMReadNameProperty(const UObject* Object, const FName PropertyName, FName& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		const FNameProperty* NameProperty = CastField<FNameProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!NameProperty)
		{
			return false;
		}

		OutValue = NameProperty->GetPropertyValue_InContainer(Object);
		return true;
	}

	template <typename TObjectType>
	TObjectType* TMReadObjectProperty(const UObject* Object, const FName PropertyName)
	{
		if (!Object)
		{
			return nullptr;
		}

		const FObjectPropertyBase* ObjectProperty =
			CastField<FObjectPropertyBase>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!ObjectProperty)
		{
			return nullptr;
		}

		return Cast<TObjectType>(ObjectProperty->GetObjectPropertyValue_InContainer(Object));
	}

	bool TMReadTransformProperty(const UObject* Object, const FName PropertyName, FTransform& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		const FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FTransform>::Get())
		{
			return false;
		}

		OutValue = *StructProperty->ContainerPtrToValuePtr<FTransform>(Object);
		return true;
	}

	bool TMWriteNumericProperty(UObject* Object, const FName PropertyName, const double Value)
	{
		if (!Object)
		{
			return false;
		}

		FNumericProperty* NumericProperty = CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!NumericProperty)
		{
			return false;
		}

		void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
		if (NumericProperty->IsFloatingPoint())
		{
			NumericProperty->SetFloatingPointPropertyValue(ValuePtr, Value);
		}
		else
		{
			NumericProperty->SetIntPropertyValue(ValuePtr, FMath::RoundToInt64(Value));
		}
		return true;
	}

	bool TMWriteRotatorProperty(UObject* Object, const FName PropertyName, const FRotator& Value)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FRotator>::Get())
		{
			return false;
		}

		FRotator* ValuePtr = StructProperty->ContainerPtrToValuePtr<FRotator>(Object);
		*ValuePtr = Value;
		return true;
	}

	bool TMWriteVectorProperty(UObject* Object, const FName PropertyName, const FVector& Value)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FVector>::Get())
		{
			return false;
		}

		FVector* ValuePtr = StructProperty->ContainerPtrToValuePtr<FVector>(Object);
		*ValuePtr = Value;
		return true;
	}

	bool TMWriteTransformProperty(UObject* Object, const FName PropertyName, const FTransform& Value)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FTransform>::Get())
		{
			return false;
		}

		FTransform* ValuePtr = StructProperty->ContainerPtrToValuePtr<FTransform>(Object);
		*ValuePtr = Value;
		return true;
	}

	bool TMWriteBoolProperty(UObject* Object, const FName PropertyName, const bool bValue)
	{
		if (!Object)
		{
			return false;
		}

		FBoolProperty* BoolProperty = CastField<FBoolProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!BoolProperty)
		{
			return false;
		}

		BoolProperty->SetPropertyValue_InContainer(Object, bValue);
		return true;
	}

	bool TMWriteAlphaLikeProperty(UObject* Object, const FName PropertyName, const double Value)
	{
		if (TMWriteNumericProperty(Object, PropertyName, Value))
		{
			return true;
		}

		return TMWriteBoolProperty(Object, PropertyName, Value > KINDA_SMALL_NUMBER);
	}

	bool TMIsAimingForWeaponBoneLock(const ATMCharacter* Character, const UAnimInstance* AnimInstance)
	{
		static const FName RotationModePropertyName(TEXT("RotationMode"));
		static constexpr int64 ALSRotationModeAimingValue = 2;

		int64 RotationModeValue = 0;
		if ((TMReadEnumLikeProperty(AnimInstance, RotationModePropertyName, RotationModeValue)
				|| TMReadEnumLikeProperty(Character, RotationModePropertyName, RotationModeValue))
			&& RotationModeValue == ALSRotationModeAimingValue)
		{
			return true;
		}

		bool bAiming = false;
		return TMReadAimStateBoolProperty(AnimInstance, bAiming) && bAiming;
	}

	bool TMIsCharacterAimingForCameraWeaponOffset(const ATMCharacter* Character, const UAnimInstance* AnimInstance)
	{
		if (TMIsAimingForWeaponBoneLock(Character, AnimInstance))
		{
			return true;
		}

		bool bAiming = false;
		if (TMReadAimStateBoolProperty(Character, bAiming))
		{
			return bAiming;
		}

		return false;
	}

	void TMApplyOpticCameraFOVGuard(ATMCharacter* Character)
	{
		if (!Character || !Character->IsLocallyControlled())
		{
			return;
		}

		AGun* ActiveGun = TMResolveActiveGun(Character);
		float OpticZoomMultiplier = 1.0f;
		if (!ActiveGun
			|| !ActiveGun->GetActiveOpticZoomMultiplier(OpticZoomMultiplier)
			|| OpticZoomMultiplier <= 1.0f + KINDA_SMALL_NUMBER)
		{
			return;
		}

		UCameraComponent* CameraComponent = Character->FindComponentByClass<UCameraComponent>();
		if (!CameraComponent)
		{
			return;
		}

		static constexpr float DefaultFOV = 90.0f;
		const float OpticAimFOV = DefaultFOV / FMath::Clamp(OpticZoomMultiplier, 1.0f, 16.0f);
		const bool bAiming = TMIsCharacterAimingForCameraWeaponOffset(
			Character,
			Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr);

		if (bAiming)
		{
			if (!FMath::IsNearlyEqual(CameraComponent->FieldOfView, OpticAimFOV, 0.01f))
			{
				CameraComponent->SetFieldOfView(OpticAimFOV);
			}
			return;
		}

		if (CameraComponent->FieldOfView > 170.0f || CameraComponent->FieldOfView < 5.0f)
		{
			CameraComponent->SetFieldOfView(DefaultFOV);
		}
	}

	FRotator TMGetCameraFloorLockedRotation(const ATMCharacter* Character)
	{
		FRotator CameraRotation = Character ? Character->GetViewRotation() : FRotator::ZeroRotator;
		CameraRotation.Roll = 0.f;
		return CameraRotation.GetNormalized();
	}

	FRotator TMGetWeaponBoneFloorLockedComponentRotation(
		const ATMCharacter* Character,
		const USkeletalMeshComponent* Mesh,
		const double YawOffsetDegrees)
	{
		if (!Mesh)
		{
			return FRotator::ZeroRotator;
		}

		const FRotator CameraRotation = TMGetCameraFloorLockedRotation(Character);
		const FRotator DesiredWorldRotation(CameraRotation.Pitch, CameraRotation.Yaw + YawOffsetDegrees, 0.f);
		const FQuat DesiredComponentRotation = Mesh->GetComponentQuat().Inverse() * DesiredWorldRotation.Quaternion();
		return DesiredComponentRotation.Rotator().GetNormalized();
	}

	void TMUpdateFabrikFixerAlpha(UAnimInstance* AnimInstance)
	{
		static const FName EnableHandIKLPropertyName(TEXT("Enable_HandIK_L"));
		static const FName FabrikFixerAlphaPropertyName(TEXT("FabrikFixerAlpha"));

		double EnableHandIKL = 0.0;
		if (!TMReadNumericProperty(AnimInstance, EnableHandIKLPropertyName, EnableHandIKL))
		{
			return;
		}

		const double FabrikFixerAlpha = FMath::Clamp(EnableHandIKL, 0.0, 1.0) * 0.5;
		TMWriteNumericProperty(AnimInstance, FabrikFixerAlphaPropertyName, FabrikFixerAlpha);
	}

	void TMUpdateFabrikFixerAlpha(const USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}

		TMUpdateFabrikFixerAlpha(Mesh->GetAnimInstance());
		for (UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			TMUpdateFabrikFixerAlpha(LinkedAnimInstance);
		}
	}

	void TMUpdateWeaponBoneFloorLock(
		UAnimInstance* AnimInstance,
		const ATMCharacter* Character,
		const USkeletalMeshComponent* Mesh)
	{
		if (!AnimInstance || !Character || !Mesh)
		{
			return;
		}

		static const FName CameraFloorLockedRotationPropertyName(TEXT("CameraFloorLockedRotation"));
		static const FName WeaponBoneFloorLockedRotationPropertyName(TEXT("WeaponBoneFloorLockedRotation"));
		static const FName WeaponBoneFloorLockAlphaPropertyName(TEXT("WeaponBoneFloorLockAlpha"));
		static const FName WeaponBoneFloorLockYawOffsetPropertyName(TEXT("WeaponBoneFloorLockYawOffset"));

		double YawOffsetDegrees = 0.0;
		TMReadNumericProperty(AnimInstance, WeaponBoneFloorLockYawOffsetPropertyName, YawOffsetDegrees);

		const FRotator CameraFloorLockedRotation = TMGetCameraFloorLockedRotation(Character);
		const FRotator WeaponBoneFloorLockedRotation =
			TMGetWeaponBoneFloorLockedComponentRotation(Character, Mesh, YawOffsetDegrees);
		const double Alpha = TMIsAimingForWeaponBoneLock(Character, AnimInstance) ? 1.0 : 0.0;

		TMWriteRotatorProperty(AnimInstance, CameraFloorLockedRotationPropertyName, CameraFloorLockedRotation);
		TMWriteRotatorProperty(AnimInstance, WeaponBoneFloorLockedRotationPropertyName, WeaponBoneFloorLockedRotation);
		TMWriteNumericProperty(AnimInstance, WeaponBoneFloorLockAlphaPropertyName, Alpha);
	}

	void TMUpdateWeaponBoneFloorLock(const ATMCharacter* Character, const USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}

		TMUpdateWeaponBoneFloorLock(Mesh->GetAnimInstance(), Character, Mesh);
		for (UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			TMUpdateWeaponBoneFloorLock(LinkedAnimInstance, Character, Mesh);
		}
	}

	const AGun* TMGetGunDefaults(const AGun* ActiveGun)
	{
		return ActiveGun ? Cast<AGun>(ActiveGun->GetClass()->GetDefaultObject()) : nullptr;
	}

	bool TMIsKrissGun(const AGun* ActiveGun)
	{
		return ActiveGun
			&& ActiveGun->GetClass()
			&& ActiveGun->GetClass()->GetPathName().Contains(TEXT("BP_Kriss"));
	}

	void TMApplyViewmodelAxisRotation(FQuat& Rotation, const FVector& Axis, const float Degrees)
	{
		if (FMath::IsNearlyZero(Degrees))
		{
			return;
		}

		Rotation = (Rotation * FQuat(Axis.GetSafeNormal(), FMath::DegreesToRadians(Degrees))).GetNormalized();
	}

	void TMApplyViewmodelRotationConsoleOffset(FTransform& CameraWeaponOffset, const bool bAiming)
	{
		const float PitchDeg = bAiming ? CVarTMViewmodelAimRollDeg.GetValueOnGameThread() : CVarTMViewmodelRollDeg.GetValueOnGameThread();
		const float YawDeg = bAiming ? CVarTMViewmodelAimYawDeg.GetValueOnGameThread() : CVarTMViewmodelYawDeg.GetValueOnGameThread();
		const float RollDeg = bAiming ? CVarTMViewmodelAimPitchDeg.GetValueOnGameThread() : CVarTMViewmodelPitchDeg.GetValueOnGameThread();
		if (FMath::IsNearlyZero(PitchDeg) && FMath::IsNearlyZero(YawDeg) && FMath::IsNearlyZero(RollDeg))
		{
			return;
		}

		FQuat Rotation = CameraWeaponOffset.GetRotation();
		TMApplyViewmodelAxisRotation(Rotation, FVector::RightVector, PitchDeg);
		TMApplyViewmodelAxisRotation(Rotation, FVector::UpVector, YawDeg);
		TMApplyViewmodelAxisRotation(Rotation, FVector::ForwardVector, RollDeg);
		CameraWeaponOffset.SetRotation(Rotation);
	}

	FTransform TMGetDefaultCameraWeaponOffset(const AGun* ActiveGun)
	{
		const AGun* DefaultGun = TMGetGunDefaults(ActiveGun);
		FTransform CameraWeaponOffset = DefaultGun ? DefaultGun->GetCameraWeaponOffset() : FTransform::Identity;
		const bool bIsKrissGun = TMIsKrissGun(ActiveGun);
		if (bIsKrissGun)
		{
			CameraWeaponOffset.SetLocation(FVector(6.0f, 11.0f, -6.0f));
		}

		static double LastPulseFileCheckTime = -1.0;
		const double Now = FPlatformTime::Seconds();
		if (LastPulseFileCheckTime < 0.0 || Now - LastPulseFileCheckTime > 0.1)
		{
			LastPulseFileCheckTime = Now;

			FString FileContents;
			const FString PulseFilePath = FPaths::ProjectSavedDir() / TEXT("Codex/KrissNoAimOffsetPulse.txt");
			if (FFileHelper::LoadFileToString(FileContents, *PulseFilePath))
			{
				TArray<FString> Args;
				FileContents.ParseIntoArrayWS(Args);
				float Duration = 3.0f;
				if (Args.Num() >= 4)
				{
					GTMDebugKrissNoAimOffsetPulseOffset = FVector(
						FCString::Atof(*Args[0]),
						FCString::Atof(*Args[1]),
						FCString::Atof(*Args[2]));
					Duration = FCString::Atof(*Args[3]);
				}
				else
				{
					const float PulseZ = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 15.0f;
					Duration = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 3.0f;
					GTMDebugKrissNoAimOffsetPulseOffset = FVector(0.0f, 0.0f, PulseZ);
				}
				GTMDebugKrissNoAimOffsetPulseEndTime = Now + FMath::Max(Duration, 0.0f);
				GTMDebugKrissNoAimOffsetPulseWasActive = false;
				IFileManager::Get().Delete(*PulseFilePath, false, true);
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[TMDebugKrissNoAimOffsetPulse] Started AddOffset=%s Duration=%.3f"),
					*GTMDebugKrissNoAimOffsetPulseOffset.ToString(),
					Duration);
			}
		}

		const bool bPulseActive = Now < GTMDebugKrissNoAimOffsetPulseEndTime;
		if (bPulseActive)
		{
			if (bIsKrissGun)
			{
				CameraWeaponOffset.AddToTranslation(GTMDebugKrissNoAimOffsetPulseOffset);
			}
		}

		if (bPulseActive != GTMDebugKrissNoAimOffsetPulseWasActive)
		{
			GTMDebugKrissNoAimOffsetPulseWasActive = bPulseActive;
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMDebugKrissNoAimOffsetPulse] Active=%d AddOffset=%s"),
				bPulseActive ? 1 : 0,
				*GTMDebugKrissNoAimOffsetPulseOffset.ToString());
		}

		TMApplyViewmodelRotationConsoleOffset(CameraWeaponOffset, false);
		return CameraWeaponOffset;
	}

	FTransform TMGetDefaultCameraWeaponOffsetAiming(const AGun* ActiveGun)
	{
		const AGun* DefaultGun = TMGetGunDefaults(ActiveGun);
		FTransform CameraWeaponOffset = DefaultGun ? DefaultGun->GetCameraWeaponOffsetAiming() : FTransform::Identity;
		TMApplyViewmodelRotationConsoleOffset(CameraWeaponOffset, true);
		return CameraWeaponOffset;
	}

	bool TMInferAimStateFromFunctionName(const UFunction* Function, bool& bOutAiming)
	{
		if (!Function)
		{
			return false;
		}

		const FString UpperName = Function->GetName().ToUpper();
		if (UpperName.Contains(TEXT("RELEASE"))
			|| UpperName.Contains(TEXT("STOP"))
			|| UpperName.Contains(TEXT("END"))
			|| UpperName.Contains(TEXT("CANCEL"))
			|| UpperName.Contains(TEXT("COMPLETE"))
			|| UpperName.Contains(TEXT("OUT"))
			|| UpperName.Contains(TEXT("DISABLE"))
			|| UpperName.Contains(TEXT("FALSE"))
			|| UpperName.Contains(TEXT("UNAIM"))
			|| UpperName.Contains(TEXT("UNSCOPE")))
		{
			bOutAiming = false;
			return true;
		}

		if (UpperName.Contains(TEXT("PRESS"))
			|| UpperName.Contains(TEXT("START"))
			|| UpperName.Contains(TEXT("BEGIN"))
			|| UpperName.Contains(TEXT("TRIGGER"))
			|| UpperName.Contains(TEXT("ENABLE"))
			|| UpperName.Contains(TEXT("ENTER"))
			|| UpperName.Contains(TEXT("TRUE"))
			|| UpperName.Contains(TEXT("ADS"))
			|| UpperName.Contains(TEXT("AIM"))
			|| UpperName.Contains(TEXT("SIGHT"))
			|| UpperName.Contains(TEXT("SCOPE")))
		{
			bOutAiming = true;
			return true;
		}

		return false;
	}

	bool TMIsActivationTraceFunctionName(const FString& Name)
	{
		return Name.Contains(TEXT("BeginPlay"))
			|| Name.Contains(TEXT("ReceiveBeginPlay"))
			|| Name.Contains(TEXT("UserConstructionScript"))
			|| Name.Contains(TEXT("ExecuteUbergraph"))
			|| Name.Contains(TEXT("SetActive"))
			|| Name.Contains(TEXT("Activate"))
			|| Name.Contains(TEXT("ReadiedWeapon"))
			|| Name.Contains(TEXT("Readied"))
			|| Name.Contains(TEXT("CheckForInventoryItems"))
			|| Name.Contains(TEXT("LoadLoadout"))
			|| Name.Contains(TEXT("CycleInventory"))
			|| Name.Contains(TEXT("Cycle Inventory"))
			|| Name.Contains(TEXT("InputAction"))
			|| Name.Contains(TEXT("InpActEvt"));
	}

	bool TMReadMaxAnimCurveValue(const USkeletalMeshComponent* Mesh, const FName CurveName, float& OutCurveValue)
	{
		OutCurveValue = 0.f;
		if (!Mesh || CurveName.IsNone())
		{
			return false;
		}

		float MaxValue = 0.f;
		bool bFoundCurve = false;
		auto ReadAnimCurve = [&MaxValue, &bFoundCurve, CurveName](const UAnimInstance* AnimInstance)
		{
			float CurveValue = 0.f;
			if (AnimInstance && AnimInstance->GetCurveValue(CurveName, CurveValue))
			{
				MaxValue = FMath::Max(MaxValue, CurveValue);
				bFoundCurve = true;
			}
		};

		ReadAnimCurve(Mesh->GetAnimInstance());
		for (UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			ReadAnimCurve(LinkedAnimInstance);
		}

		OutCurveValue = FMath::Clamp(MaxValue, 0.f, 1.f);
		return bFoundCurve;
	}

	float TMInterpolateAudioMuffleFrequency(const float ClearFrequency, const float MuffledFrequency, const float Alpha)
	{
		const float SafeClearFrequency = FMath::Clamp(ClearFrequency, 20.f, 20000.f);
		const float SafeMuffledFrequency = FMath::Clamp(MuffledFrequency, 20.f, SafeClearFrequency);
		const float SafeAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
		return FMath::Exp(FMath::Lerp(FMath::Loge(SafeClearFrequency), FMath::Loge(SafeMuffledFrequency), SafeAlpha));
	}

	USoundClass* TMLoadSoundClassFromPath(const TCHAR* SoundClassPath)
	{
		return Cast<USoundClass>(StaticLoadObject(USoundClass::StaticClass(), nullptr, SoundClassPath));
	}

	bool TMIsMenuGameModeActive(const UObject* WorldContextObject)
	{
		const AGameModeBase* GameMode = UGameplayStatics::GetGameMode(WorldContextObject);
		const UClass* GameModeClass = GameMode ? GameMode->GetClass() : nullptr;
		if (!GameModeClass)
		{
			return false;
		}

		const FString GameModePath = GameModeClass->GetPathName();
		return GameModePath.Contains(TEXT("/MainMenuPawn/GM_Menu."), ESearchCase::IgnoreCase)
			|| GameModePath.Contains(TEXT("GM_Menu_C"), ESearchCase::IgnoreCase);
	}

	bool TMShouldTraceFunction(const UFunction* Function)
	{
		if (!Function)
		{
			return false;
		}

		const FString Name = Function->GetName();
		if (TMIsTraceNoiseFunction(Name))
		{
			return false;
		}

		const FString Path = Function->GetPathName();
		return TMIsActivationTraceFunctionName(Name)
			|| Name.Contains(TEXT("Weapon"))
			|| Name.Contains(TEXT("Gun"))
			|| Name.Contains(TEXT("Item"))
			|| Name.Contains(TEXT("Equip"))
			|| Name.Contains(TEXT("Draw"))
			|| Name.Contains(TEXT("Holster"))
			|| Name.Contains(TEXT("Active"))
			|| Name.Contains(TEXT("Attach"))
			|| Name.Contains(TEXT("Inventory"))
			|| Name.Contains(TEXT("Loadout"))
			|| Name.Contains(TEXT("Slot"))
			|| Name.Contains(TEXT("Select"))
			|| Name.Contains(TEXT("Switch"))
			|| Name.Contains(TEXT("Primary"))
			|| Name.Contains(TEXT("Secondary"))
			|| Name.Contains(TEXT("Use"))
			|| Name.Contains(TEXT("MPS"))
			|| Name.Contains(TEXT("Anim"))
			|| Name.Contains(TEXT("Overlay"))
			|| Name.Contains(TEXT("Montage"))
			|| Path.Contains(TEXT("CBP_MPS"))
			|| Path.Contains(TEXT("BP_MPS_Master"))
			|| Path.Contains(TEXT("BP_Kriss"))
			|| Path.Contains(TEXT("BP_SMG"))
			|| Path.Contains(TEXT("AnimBP_MPS"))
			|| Path.Contains(TEXT("MP_System_V3"));
	}

	FString TMObjectName(const UObject* Object)
	{
		return Object
			? FString::Printf(TEXT("%s Class=%s"), *Object->GetPathName(), *Object->GetClass()->GetPathName())
			: TEXT("None");
	}

	FString TMComponentAttachInfo(const USceneComponent* Component, const bool bIncludeLocation = false)
	{
		if (!Component)
		{
			return TEXT("None");
		}

		const USceneComponent* Parent = Component->GetAttachParent();
		FString Info = FString::Printf(
			TEXT("%s Parent=%s Socket=%s Visible=%d"),
			*Component->GetName(),
			Parent ? *Parent->GetName() : TEXT("None"),
			*Component->GetAttachSocketName().ToString(),
			Component->IsVisible() ? 1 : 0);

		if (bIncludeLocation)
		{
			Info += FString::Printf(TEXT(" Loc=%s"), *Component->GetComponentLocation().ToCompactString());
		}

		return Info;
	}

	FString TMAnimClassName(const USkeletalMeshComponent* Mesh)
	{
		const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
		return AnimInstance ? AnimInstance->GetClass()->GetPathName() : TEXT("None");
	}

	FString TMMontageSlotNames(const UAnimMontage* Montage)
	{
		if (!Montage)
		{
			return TEXT("None");
		}

		FString Slots;
		for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
		{
			if (!Slots.IsEmpty())
			{
				Slots += TEXT(",");
			}
			Slots += SlotTrack.SlotName.ToString();
		}

		return Slots.IsEmpty() ? TEXT("None") : Slots;
	}

	FString TMMontageName(const UAnimMontage* Montage)
	{
		return Montage ? Montage->GetPathName() : TEXT("None");
	}

	void TMAppendFunctionParameters(const UFunction* Function, void* Parameters, FString& Out)
	{
		if (!Function || !Parameters)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Parameters);
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				const UObject* Value = ObjectProperty->GetObjectPropertyValue(ValueAddress);
				Out += FString::Printf(TEXT(" %s=[%s];"), *PropertyName, *TMObjectName(Value));
			}
			else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				Out += FString::Printf(TEXT(" %s=%d;"), *PropertyName, BoolProperty->GetPropertyValue(ValueAddress) ? 1 : 0);
			}
			else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
			{
				Out += FString::Printf(TEXT(" %s=%s;"), *PropertyName, *NameProperty->GetPropertyValue(ValueAddress).ToString());
			}
			else if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
			{
				Out += FString::Printf(TEXT(" %s=%s;"), *PropertyName, *StringProperty->GetPropertyValue(ValueAddress));
			}
			else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				if (NumericProperty->IsFloatingPoint())
				{
					Out += FString::Printf(TEXT(" %s=%.3f;"), *PropertyName, NumericProperty->GetFloatingPointPropertyValue(ValueAddress));
				}
				else if (NumericProperty->IsInteger())
				{
					Out += FString::Printf(TEXT(" %s=%lld;"), *PropertyName, NumericProperty->GetSignedIntPropertyValue(ValueAddress));
				}
			}
		}
	}

	bool TMIsLikelyActiveWeaponPropertyName(const FString& PropertyName)
	{
		const bool bWeaponLike = PropertyName.Contains(TEXT("Weapon"))
			|| PropertyName.Contains(TEXT("Gun"))
			|| PropertyName.Contains(TEXT("Item"));
		const bool bActiveLike = PropertyName.Contains(TEXT("Active"))
			|| PropertyName.Contains(TEXT("Current"))
			|| PropertyName.Contains(TEXT("Equipped"))
			|| PropertyName.Contains(TEXT("Selected"))
			|| PropertyName.Contains(TEXT("Held"));
		return bWeaponLike && bActiveLike;
	}

	bool TMIsRelevantActivationPropertyName(const FString& PropertyName)
	{
		return PropertyName.Contains(TEXT("Active"))
			|| PropertyName.Contains(TEXT("Current"))
			|| PropertyName.Contains(TEXT("Selected"))
			|| PropertyName.Contains(TEXT("Readied"))
			|| PropertyName.Contains(TEXT("Weapon"))
			|| PropertyName.Contains(TEXT("Gun"))
			|| PropertyName.Contains(TEXT("Item"))
			|| PropertyName.Contains(TEXT("Slot"))
			|| PropertyName.Contains(TEXT("Inventory"))
			|| PropertyName.Contains(TEXT("Loadout"))
			|| PropertyName.Contains(TEXT("Primary"))
			|| PropertyName.Contains(TEXT("Secondary"))
			|| PropertyName.Contains(TEXT("Holster"))
			|| PropertyName.Contains(TEXT("Draw"))
			|| PropertyName.Contains(TEXT("First"))
			|| PropertyName.Contains(TEXT("Return"));
	}

	bool TMObjectLooksLikeWeapon(const UObject* Object)
	{
		if (!Object)
		{
			return false;
		}

		const FString ObjectPath = Object->GetPathName();
		const FString ClassPath = Object->GetClass()->GetPathName();
		return ObjectPath.Contains(TEXT("/Weapons/"))
			|| ClassPath.Contains(TEXT("/Weapons/"))
			|| ObjectPath.Contains(TEXT("BP_Kriss"))
			|| ClassPath.Contains(TEXT("BP_Kriss"))
			|| ObjectPath.Contains(TEXT("BP_SMG"))
			|| ClassPath.Contains(TEXT("BP_SMG"));
	}

	FString TMEnumValueToString(const UEnum* Enum, const int64 Value)
	{
		if (!Enum)
		{
			return FString::Printf(TEXT("%lld"), static_cast<long long>(Value));
		}

		FString DisplayName = Enum->GetDisplayNameTextByValue(Value).ToString();
		if (DisplayName.IsEmpty())
		{
			DisplayName = Enum->GetNameStringByValue(Value);
		}

		return FString::Printf(TEXT("%lld/%s"), static_cast<long long>(Value), *DisplayName);
	}

	void TMAppendPropertyValue(const FProperty* Property, const void* ValuePtr, FString& Out, const int32 Depth);

	void TMAppendArrayPropertyValue(const FArrayProperty* ArrayProperty, const void* ValuePtr, FString& Out, const int32 Depth)
	{
		if (!ArrayProperty || !ValuePtr)
		{
			Out += TEXT("[]");
			return;
		}

		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		Out += FString::Printf(TEXT("[Num=%d"), Helper.Num());
		const int32 MaxElementsToLog = FMath::Min(Helper.Num(), 8);
		for (int32 Index = 0; Index < MaxElementsToLog; ++Index)
		{
			Out += FString::Printf(TEXT(" %d="), Index);
			TMAppendPropertyValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), Out, Depth + 1);
		}
		if (Helper.Num() > MaxElementsToLog)
		{
			Out += TEXT(" ...");
		}
		Out += TEXT("]");
	}

	void TMAppendStructPropertyValue(const FStructProperty* StructProperty, const void* ValuePtr, FString& Out, const int32 Depth)
	{
		if (!StructProperty || !ValuePtr || Depth > 1)
		{
			Out += StructProperty && StructProperty->Struct ? *StructProperty->Struct->GetName() : TEXT("Struct");
			return;
		}

		Out += FString::Printf(TEXT("%s{"), *StructProperty->Struct->GetName());
		bool bHasField = false;
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			const FProperty* InnerProperty = *It;
			if (!InnerProperty || !TMIsRelevantActivationPropertyName(InnerProperty->GetName()))
			{
				continue;
			}

			if (bHasField)
			{
				Out += TEXT(" ");
			}
			bHasField = true;

			const void* InnerValuePtr = InnerProperty->ContainerPtrToValuePtr<void>(ValuePtr);
			Out += FString::Printf(TEXT("%s="), *InnerProperty->GetName());
			TMAppendPropertyValue(InnerProperty, InnerValuePtr, Out, Depth + 1);
		}
		Out += bHasField ? TEXT("}") : TEXT("}");
	}

	void TMAppendPropertyValue(const FProperty* Property, const void* ValuePtr, FString& Out, const int32 Depth)
	{
		if (!Property || !ValuePtr)
		{
			Out += TEXT("None");
			return;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Value = ObjectProperty->GetObjectPropertyValue(ValuePtr);
			Out += FString::Printf(TEXT("[%s]"), *TMObjectName(Value));
		}
		else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			Out += BoolProperty->GetPropertyValue(ValuePtr) ? TEXT("1") : TEXT("0");
		}
		else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			Out += TMEnumValueToString(EnumProperty->GetEnum(), Value);
		}
		else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			const uint8 Value = ByteProperty->GetPropertyValue(ValuePtr);
			Out += TMEnumValueToString(ByteProperty->Enum, Value);
		}
		else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			Out += NameProperty->GetPropertyValue(ValuePtr).ToString();
		}
		else if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			Out += StringProperty->GetPropertyValue(ValuePtr);
		}
		else if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			Out += TextProperty->GetPropertyValue(ValuePtr).ToString();
		}
		else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				Out += FString::Printf(TEXT("%.3f"), NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
			}
			else if (NumericProperty->IsInteger())
			{
				Out += FString::Printf(TEXT("%lld"), NumericProperty->GetSignedIntPropertyValue(ValuePtr));
			}
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			TMAppendArrayPropertyValue(ArrayProperty, ValuePtr, Out, Depth);
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			TMAppendStructPropertyValue(StructProperty, ValuePtr, Out, Depth);
		}
		else
		{
			Out += Property->GetCPPType();
		}
	}

	void TMAppendRelevantObjectProperties(const UObject* Object, FString& Out, const int32 MaxChars = 12000)
	{
		if (!Object)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			if (!TMIsRelevantActivationPropertyName(PropertyName))
			{
				if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					const UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
					if (!TMObjectLooksLikeWeapon(Value))
					{
						continue;
					}
				}
				else
				{
					continue;
				}
			}

			Out += FString::Printf(TEXT(" %s="), *PropertyName);
			TMAppendPropertyValue(Property, Property->ContainerPtrToValuePtr<void>(Object), Out, 0);
			Out += TEXT(";");

			if (Out.Len() >= MaxChars)
			{
				Out += TEXT(" ...");
				return;
			}
		}
	}

	FString TMDescribeActiveWeaponState(const UObject* Object)
	{
		if (!Object)
		{
			return TEXT("None");
		}

		FString State;
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				const UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
				if (TMIsLikelyActiveWeaponPropertyName(PropertyName)
					|| PropertyName.Contains(TEXT("Active"))
					|| TMObjectLooksLikeWeapon(Value))
				{
					State += FString::Printf(TEXT(" %s=[%s];"), *PropertyName, *TMObjectName(Value));
				}
			}
			else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				if (TMIsLikelyActiveWeaponPropertyName(PropertyName)
					|| PropertyName.Contains(TEXT("Active"))
					|| PropertyName.Contains(TEXT("Holster")))
				{
					State += FString::Printf(
						TEXT(" %s=%d;"),
						*PropertyName,
						BoolProperty->GetPropertyValue_InContainer(Object) ? 1 : 0);
				}
			}
			else if (PropertyName.Contains(TEXT("Active")) || PropertyName.Contains(TEXT("Slot")))
			{
				State += FString::Printf(TEXT(" %s="), *PropertyName);
				TMAppendPropertyValue(Property, Property->ContainerPtrToValuePtr<void>(Object), State, 0);
				State += TEXT(";");
			}
		}

		return State.IsEmpty() ? TEXT("None") : State;
	}

	FString TMDescribeMontages(const USkeletalMeshComponent* Mesh)
	{
		const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
		if (!AnimInstance)
		{
			return TEXT("AnimInstance=None ActiveMontage=None");
		}

		const UAnimMontage* CurrentMontage = AnimInstance->GetCurrentActiveMontage();
		FString Result = FString::Printf(
			TEXT("AnimInstance=%s Class=%s ActiveMontage=%s"),
			*AnimInstance->GetPathName(),
			*AnimInstance->GetClass()->GetPathName(),
			*TMMontageName(CurrentMontage));

		for (const FAnimMontageInstance* MontageInstance : AnimInstance->MontageInstances)
		{
			if (!MontageInstance)
			{
				continue;
			}

			const UAnimMontage* Montage = MontageInstance->Montage;
			Result += FString::Printf(
				TEXT(" MontageInstance{Montage=%s Slots=%s Section=%s Pos=%.3f Rate=%.3f Weight=%.3f DesiredWeight=%.3f Playing=%d Active=%d Stopped=%d};"),
				*TMMontageName(Montage),
				*TMMontageSlotNames(Montage),
				Montage ? *AnimInstance->Montage_GetCurrentSection(Montage).ToString() : TEXT("None"),
				MontageInstance->GetPosition(),
				MontageInstance->GetPlayRate(),
				MontageInstance->GetWeight(),
				MontageInstance->GetDesiredWeight(),
				MontageInstance->IsPlaying() ? 1 : 0,
				MontageInstance->IsActive() ? 1 : 0,
				MontageInstance->IsStopped() ? 1 : 0);
		}

		return Result;
	}

	void TMAppendObjectProperties(const UObject* Object, FString& Out)
	{
		TMAppendRelevantObjectProperties(Object, Out);
	}

	FString TMGunState(const AGun* Gun)
	{
		if (!Gun)
		{
			return TEXT("None");
		}

		FString State = FString::Printf(
			TEXT("Gun=%s Owner=%s Root={%s} FakeMode=%d"),
			*TMObjectName(Gun),
			Gun->GetOwner() ? *Gun->GetOwner()->GetPathName() : TEXT("None"),
			*TMComponentAttachInfo(Gun->GetRootComponent()),
			Gun->IsFakeMode() ? 1 : 0);

		TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes(Gun);
		for (const USkeletalMeshComponent* SkeletalMesh : SkeletalMeshes)
		{
			State += FString::Printf(
				TEXT(" Mesh{%s Anim=%s Asset=%s};"),
				*TMComponentAttachInfo(SkeletalMesh),
				*TMAnimClassName(SkeletalMesh),
				SkeletalMesh->GetSkeletalMeshAsset()
					? *SkeletalMesh->GetSkeletalMeshAsset()->GetPathName()
					: TEXT("None"));
		}

		TMAppendRelevantObjectProperties(Gun, State, 8000);
		return State;
	}

	FString TMDescribeActivationCore(const ATMCharacter* Character)
	{
		if (!Character)
		{
			return TEXT("Character=None");
		}

		FString State = FString::Printf(
			TEXT("Character=%s ActiveWeaponState={%s}"),
			*TMObjectName(Character),
			*TMDescribeActiveWeaponState(Character));
		TMAppendRelevantObjectProperties(Character, State, 12000);
		return State;
	}

	AGun* TMResolveActiveGun(const ATMCharacter* Character)
	{
		if (!Character)
		{
			return nullptr;
		}

		AGun* WeaponLikeCandidate = nullptr;
		for (TFieldIterator<FProperty> It(Character->GetClass()); It; ++It)
		{
			const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(*It);
			if (!ObjectProperty)
			{
				continue;
			}

			UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Character);
			AGun* Gun = Cast<AGun>(Value);
			if (!Gun)
			{
				continue;
			}

			const FString PropertyName = ObjectProperty->GetName();
			if (TMIsLikelyActiveWeaponPropertyName(PropertyName))
			{
				return Gun;
			}

			if (!WeaponLikeCandidate && TMObjectLooksLikeWeapon(Gun))
			{
				WeaponLikeCandidate = Gun;
			}
		}

		if (WeaponLikeCandidate)
		{
			return WeaponLikeCandidate;
		}

		UWorld* World = Character->GetWorld();
		if (!World)
		{
			return nullptr;
		}

		AGun* ClosestCandidate = nullptr;
		double ClosestDistanceSquared = TNumericLimits<double>::Max();
		for (TActorIterator<AGun> It(World); It; ++It)
		{
			AGun* Gun = *It;
			if (!Gun)
			{
				continue;
			}

			if (Gun->GetOwner() == Character)
			{
				return Gun;
			}

			const double DistanceSquared = FVector::DistSquared(Gun->GetActorLocation(), Character->GetActorLocation());
			if (DistanceSquared < FMath::Square(400.f) && DistanceSquared < ClosestDistanceSquared)
			{
				ClosestDistanceSquared = DistanceSquared;
				ClosestCandidate = Gun;
			}
		}

		return ClosestCandidate;
	}

	FTransform TMMakeADSCameraTargetTransform(const FTransform& ADSSocketWorldTransform, const double EyeRelief)
	{
		FRotator CameraRotation = ADSSocketWorldTransform.GetRotation().Rotator().GetNormalized();
		CameraRotation.Roll = 0.f;

		const FVector CameraLocation =
			ADSSocketWorldTransform.GetLocation() - CameraRotation.Vector() * EyeRelief;
		return FTransform(CameraRotation, CameraLocation, FVector::OneVector);
	}

	FTransform TMGetCameraWorldTransform(const ATMCharacter* Character)
	{
		if (!Character)
		{
			return FTransform::Identity;
		}

		FVector CameraLocation = Character->GetPawnViewLocation();
		FRotator CameraRotation = Character->GetViewRotation();
		if (const UCameraComponent* CameraComponent = Character->FindComponentByClass<UCameraComponent>())
		{
			CameraLocation = CameraComponent->GetComponentLocation();
		}

		CameraRotation.Roll = 0.f;
		return FTransform(CameraRotation.GetNormalized(), CameraLocation, FVector::OneVector);
	}

	FProperty* TMFindFunctionParameter(UFunction* Function, const std::initializer_list<FName> ParameterNames)
	{
		if (!Function)
		{
			return nullptr;
		}

		for (const FName ParameterName : ParameterNames)
		{
			if (FProperty* Property = Function->FindPropertyByName(ParameterName))
			{
				return Property;
			}
		}

		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property
				|| !Property->HasAnyPropertyFlags(CPF_Parm)
				|| Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			const FString SpacedPropertyName = PropertyName.Replace(TEXT("_"), TEXT(" "));
			for (const FName ParameterName : ParameterNames)
			{
				const FString RequestedName = ParameterName.ToString();
				if (PropertyName.Equals(RequestedName, ESearchCase::IgnoreCase)
					|| SpacedPropertyName.Equals(RequestedName, ESearchCase::IgnoreCase))
				{
					return Property;
				}
			}
		}

		return nullptr;
	}

	bool TMSetVectorFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		const FVector& Value)
	{
		FStructProperty* StructProperty = CastField<FStructProperty>(TMFindFunctionParameter(Function, ParameterNames));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FVector>::Get())
		{
			return false;
		}

		*StructProperty->ContainerPtrToValuePtr<FVector>(Parameters) = Value;
		return true;
	}

	bool TMGetVectorFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		FVector& OutValue)
	{
		FStructProperty* StructProperty = CastField<FStructProperty>(TMFindFunctionParameter(Function, ParameterNames));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FVector>::Get())
		{
			return false;
		}

		OutValue = *StructProperty->ContainerPtrToValuePtr<FVector>(Parameters);
		return true;
	}

	bool TMSetNumericFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		const double Value)
	{
		FNumericProperty* NumericProperty = CastField<FNumericProperty>(TMFindFunctionParameter(Function, ParameterNames));
		if (!NumericProperty)
		{
			return false;
		}

		void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Parameters);
		if (NumericProperty->IsFloatingPoint())
		{
			NumericProperty->SetFloatingPointPropertyValue(ValuePtr, Value);
		}
		else
		{
			NumericProperty->SetIntPropertyValue(ValuePtr, FMath::RoundToInt64(Value));
		}
		return true;
	}

	bool TMSetBoolFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		const bool Value)
	{
		FBoolProperty* BoolProperty = CastField<FBoolProperty>(TMFindFunctionParameter(Function, ParameterNames));
		if (!BoolProperty)
		{
			return false;
		}

		BoolProperty->SetPropertyValue(BoolProperty->ContainerPtrToValuePtr<void>(Parameters), Value);
		return true;
	}

	bool TMGetBoolFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		bool& OutValue)
	{
		FBoolProperty* BoolProperty = CastField<FBoolProperty>(TMFindFunctionParameter(Function, ParameterNames));
		if (!BoolProperty)
		{
			return false;
		}

		OutValue = BoolProperty->GetPropertyValue(BoolProperty->ContainerPtrToValuePtr<void>(Parameters));
		return true;
	}

	bool TMSetNameFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		const FName Value)
	{
		FNameProperty* NameProperty = CastField<FNameProperty>(TMFindFunctionParameter(Function, ParameterNames));
		if (!NameProperty)
		{
			return false;
		}

		NameProperty->SetPropertyValue(NameProperty->ContainerPtrToValuePtr<void>(Parameters), Value);
		return true;
	}

	bool TMGetNameFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		FName& OutValue)
	{
		FNameProperty* NameProperty = CastField<FNameProperty>(TMFindFunctionParameter(Function, ParameterNames));
		if (!NameProperty)
		{
			return false;
		}

		OutValue = NameProperty->GetPropertyValue(NameProperty->ContainerPtrToValuePtr<void>(Parameters));
		return true;
	}

	bool TMSetObjectFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		UObject* Value)
	{
		FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(TMFindFunctionParameter(Function, ParameterNames));
		if (!ObjectProperty)
		{
			return false;
		}

		ObjectProperty->SetObjectPropertyValue(ObjectProperty->ContainerPtrToValuePtr<void>(Parameters), Value);
		return true;
	}

	template <typename TObjectType>
	TObjectType* TMGetObjectFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames)
	{
		FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(TMFindFunctionParameter(Function, ParameterNames));
		if (!ObjectProperty)
		{
			return nullptr;
		}

		return Cast<TObjectType>(ObjectProperty->GetObjectPropertyValue(ObjectProperty->ContainerPtrToValuePtr<void>(Parameters)));
	}

	bool TMSetHitResultFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		const FHitResult& Value)
	{
		FStructProperty* StructProperty = CastField<FStructProperty>(TMFindFunctionParameter(Function, ParameterNames));
		if (!StructProperty || StructProperty->Struct != FHitResult::StaticStruct())
		{
			return false;
		}

		*StructProperty->ContainerPtrToValuePtr<FHitResult>(Parameters) = Value;
		return true;
	}

	bool TMGetHitResultFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		FHitResult& OutValue)
	{
		FStructProperty* StructProperty = CastField<FStructProperty>(TMFindFunctionParameter(Function, ParameterNames));
		if (!StructProperty || StructProperty->Struct != FHitResult::StaticStruct())
		{
			return false;
		}

		OutValue = *StructProperty->ContainerPtrToValuePtr<FHitResult>(Parameters);
		return true;
	}

	bool TMSetActorArrayFunctionParameter(
		UFunction* Function,
		void* Parameters,
		const std::initializer_list<FName> ParameterNames,
		const TArray<AActor*>& Actors)
	{
		FProperty* Property = TMFindFunctionParameter(Function, ParameterNames);
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FObjectPropertyBase* InnerObjectProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner);
			if (!InnerObjectProperty)
			{
				return false;
			}

			FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Parameters));
			ArrayHelper.EmptyAndAddValues(Actors.Num());
			for (int32 Index = 0; Index < Actors.Num(); ++Index)
			{
				InnerObjectProperty->SetObjectPropertyValue(ArrayHelper.GetRawPtr(Index), Actors[Index]);
			}
			return true;
		}

		return Actors.Num() > 0 && TMSetObjectFunctionParameter(Function, Parameters, ParameterNames, Actors[0]);
	}

	double TMGetTimeSeconds(const AActor* Actor)
	{
		const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
		return World ? World->GetTimeSeconds() : 0.0;
	}

	USoundBase* TMLoadSoundBaseFromPath(const TCHAR* SoundPath)
	{
		return SoundPath && SoundPath[0] != TEXT('\0')
			? LoadObject<USoundBase>(nullptr, SoundPath)
			: nullptr;
	}

	FVector TMResolveHeadshotSoundLocationFromMesh(
		const USkeletalMeshComponent* Mesh,
		const FName HitBone,
		const FVector& FallbackLocation)
	{
		if (!Mesh)
		{
			return FallbackLocation;
		}

		const FName CandidateNames[] =
		{
			HitBone,
			TEXT("head"),
			TEXT("Head"),
			TEXT("HEAD")
		};

		for (const FName CandidateName : CandidateNames)
		{
			if (!CandidateName.IsNone()
				&& (Mesh->DoesSocketExist(CandidateName) || Mesh->GetBoneIndex(CandidateName) != INDEX_NONE))
			{
				return Mesh->GetSocketLocation(CandidateName);
			}
		}

		return FallbackLocation;
	}

	FVector TMResolveHeadshotSoundLocation(
		AActor* HitActor,
		const FHitResult& HitInfo,
		const FName HitBone,
		const FVector& FallbackLocation)
	{
		if (!UTMGameplayStatics::IsHeadHitBone(HitBone))
		{
			return FallbackLocation;
		}

		if (const USkeletalMeshComponent* HitMesh = Cast<USkeletalMeshComponent>(HitInfo.Component.Get()))
		{
			return TMResolveHeadshotSoundLocationFromMesh(HitMesh, HitBone, FallbackLocation);
		}

		if (!HitActor)
		{
			return FallbackLocation;
		}

		TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes(HitActor);
		for (const USkeletalMeshComponent* SkeletalMesh : SkeletalMeshes)
		{
			const FVector HeadLocation = TMResolveHeadshotSoundLocationFromMesh(SkeletalMesh, HitBone, FallbackLocation);
			if (!HeadLocation.Equals(FallbackLocation))
			{
				return HeadLocation;
			}
		}

		return FallbackLocation;
	}

	void TMPlayHeadshotHitmarkerSounds(
		ATMCharacter* Shooter,
		const FVector& HeadLocation,
		const FVector& Normal)
	{
		UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}

		const FRotator Rotation = Normal.IsNearlyZero()
			? FRotator::ZeroRotator
			: Normal.GetSafeNormal().Rotation();
		const bool bPlayShooterLocalCopy = Shooter->IsLocallyControlled();

		for (const TCHAR* SoundPath : TMHeadshotHitmarkerSoundPaths)
		{
			USoundBase* Sound = TMLoadSoundBaseFromPath(SoundPath);
			if (!Sound)
			{
				continue;
			}

			UGameplayStatics::PlaySoundAtLocation(Shooter, Sound, HeadLocation, Rotation);
			if (bPlayShooterLocalCopy)
			{
				UGameplayStatics::PlaySound2D(Shooter, Sound);
			}
		}
	}

	bool TMImpactPhysicalMaterialsMatch(
		const UPhysicalMaterial* Left,
		const UPhysicalMaterial* Right)
	{
		return Left == Right
			|| (Left
				&& Right
				&& UPhysicalMaterial::DetermineSurfaceType(Left) == UPhysicalMaterial::DetermineSurfaceType(Right));
	}

	bool TMImpactNormalsMatch(const FVector& Left, const FVector& Right)
	{
		if (Left.IsNearlyZero() || Right.IsNearlyZero())
		{
			return true;
		}

		return FVector::DotProduct(Left.GetSafeNormal(), Right.GetSafeNormal()) > 0.35f;
	}

	void TMPrunePendingImpactHeadshots(ATMCharacter* Character, TArray<FTMPendingImpactHeadshot>& PendingImpacts)
	{
		const double Now = TMGetTimeSeconds(Character);
		PendingImpacts.RemoveAll(
			[Now](const FTMPendingImpactHeadshot& PendingImpact)
			{
				return Now > 0.0
					&& PendingImpact.TimeSeconds > 0.0
					&& Now - PendingImpact.TimeSeconds > TMPendingImpactHeadshotMaxAgeSeconds;
			});
	}

	void TMStorePendingImpactHeadshot(
		ATMCharacter* Character,
		const FVector& Location,
		const FVector& Normal,
		const UPhysicalMaterial* PhysicalMaterial,
		const bool bHeadshot,
		const FVector& HeadshotSoundLocation)
	{
		if (!Character)
		{
			return;
		}

		TArray<FTMPendingImpactHeadshot>& PendingImpacts =
			GTMPendingImpactHeadshots.FindOrAdd(TWeakObjectPtr<ATMCharacter>(Character));
		TMPrunePendingImpactHeadshots(Character, PendingImpacts);

		FTMPendingImpactHeadshot PendingImpact;
		PendingImpact.Location = Location;
		PendingImpact.Normal = Normal;
		PendingImpact.PhysicalMaterial = PhysicalMaterial;
		PendingImpact.TimeSeconds = TMGetTimeSeconds(Character);
		PendingImpact.bHeadshot = bHeadshot;
		PendingImpact.bHasHeadshotSoundLocation = bHeadshot;
		PendingImpact.HeadshotSoundLocation = HeadshotSoundLocation;
		PendingImpacts.Add(PendingImpact);

		while (PendingImpacts.Num() > TMPendingImpactHeadshotMaxPerCharacter)
		{
			PendingImpacts.RemoveAt(0);
		}
	}

	bool TMConsumePendingImpactHeadshot(
		ATMCharacter* Character,
		const FVector& Location,
		const FVector& Normal,
		const UPhysicalMaterial* PhysicalMaterial,
		bool& bOutHeadshot,
		FVector& OutHeadshotSoundLocation)
	{
		const TWeakObjectPtr<ATMCharacter> CharacterKey(Character);
		TArray<FTMPendingImpactHeadshot>* PendingImpacts = GTMPendingImpactHeadshots.Find(CharacterKey);
		if (!Character || !PendingImpacts)
		{
			return false;
		}

		TMPrunePendingImpactHeadshots(Character, *PendingImpacts);

		int32 BestIndex = INDEX_NONE;
		double BestDistanceSq = TMPendingImpactLocationToleranceSq;
		for (int32 Index = 0; Index < PendingImpacts->Num(); ++Index)
		{
			const FTMPendingImpactHeadshot& PendingImpact = (*PendingImpacts)[Index];
			if (!TMImpactPhysicalMaterialsMatch(PendingImpact.PhysicalMaterial, PhysicalMaterial)
				|| !TMImpactNormalsMatch(PendingImpact.Normal, Normal))
			{
				continue;
			}

			const double DistanceSq = FVector::DistSquared(PendingImpact.Location, Location);
			if (DistanceSq <= BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				BestIndex = Index;
			}
		}

		if (BestIndex == INDEX_NONE)
		{
			if (PendingImpacts->IsEmpty())
			{
				GTMPendingImpactHeadshots.Remove(CharacterKey);
			}
			return false;
		}

		bOutHeadshot = (*PendingImpacts)[BestIndex].bHeadshot;
		OutHeadshotSoundLocation = (*PendingImpacts)[BestIndex].bHasHeadshotSoundLocation
			? (*PendingImpacts)[BestIndex].HeadshotSoundLocation
			: Location;
		PendingImpacts->RemoveAt(BestIndex);
		if (PendingImpacts->IsEmpty())
		{
			GTMPendingImpactHeadshots.Remove(CharacterKey);
		}
		return true;
	}

	bool TMExtractImpactHeadshotFromParameters(
		UFunction* Function,
		void* Parameters,
		bool& bOutHeadshot,
		FVector& OutHeadshotSoundLocation)
	{
		FHitResult HitInfo;
		if (TMGetHitResultFunctionParameter(
			Function,
			Parameters,
			{ TEXT("HitInfo"), TEXT("OutHit"), TEXT("Hit"), TEXT("HitResult"), TEXT("Hit Result") },
			HitInfo))
		{
			bOutHeadshot = UTMGameplayStatics::IsHeadHitBone(HitInfo.BoneName);
			const FVector FallbackLocation = !HitInfo.ImpactPoint.IsNearlyZero()
				? HitInfo.ImpactPoint
				: HitInfo.Location;
			OutHeadshotSoundLocation = bOutHeadshot
				? TMResolveHeadshotSoundLocation(HitInfo.GetActor(), HitInfo, HitInfo.BoneName, FallbackLocation)
				: FVector::ZeroVector;
			return true;
		}

		FName HitBone = NAME_None;
		if (TMGetNameFunctionParameter(
			Function,
			Parameters,
			{ TEXT("HitBone"), TEXT("HitBoneName"), TEXT("BoneName"), TEXT("Hit Bone"), TEXT("Hit Bone Name") },
			HitBone))
		{
			bOutHeadshot = UTMGameplayStatics::IsHeadHitBone(HitBone);
			OutHeadshotSoundLocation = FVector::ZeroVector;
			return true;
		}

		if (TMGetBoolFunctionParameter(
			Function,
			Parameters,
			{
				TEXT("bHeadshot"),
				TEXT("Headshot"),
				TEXT("bHeadShot"),
				TEXT("HeadShot"),
				TEXT("bIsHeadshot"),
				TEXT("IsHeadshot"),
				TEXT("bIsHeadShot"),
				TEXT("IsHeadShot")
			},
			bOutHeadshot))
		{
			OutHeadshotSoundLocation = FVector::ZeroVector;
			return true;
		}

		return false;
	}

	template <typename TFillParametersType>
	bool TMInvokeBlueprintFunction(UObject* Object, const FName FunctionName, TFillParametersType&& FillParameters)
	{
		if (!Object)
		{
			return false;
		}

		UFunction* Function = Object->FindFunction(FunctionName);
		if (!Function)
		{
			return false;
		}

		FStructOnScope FunctionParameters(Function);
		void* Parameters = FunctionParameters.GetStructMemory();
		FillParameters(Function, Parameters);
		Object->ProcessEvent(Function, Parameters);
		return true;
	}

	bool TMInvokeBlueprintFunction(UObject* Object, const FName FunctionName)
	{
		return TMInvokeBlueprintFunction(Object, FunctionName, [](UFunction*, void*) {});
	}

	USkeletalMeshComponent* TMResolveGunItemMesh(const AGun* Gun)
	{
		if (!Gun)
		{
			return nullptr;
		}

		if (USkeletalMeshComponent* ItemMesh = TMReadObjectProperty<USkeletalMeshComponent>(Gun, TEXT("Item")))
		{
			return ItemMesh;
		}

		TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes(Gun);
		for (USkeletalMeshComponent* SkeletalMesh : SkeletalMeshes)
		{
			if (SkeletalMesh && SkeletalMesh->GetFName() == TEXT("Item"))
			{
				return SkeletalMesh;
			}
		}

		for (USkeletalMeshComponent* SkeletalMesh : SkeletalMeshes)
		{
			if (SkeletalMesh && SkeletalMesh->DoesSocketExist(TEXT("Muzzle")))
			{
				return SkeletalMesh;
			}
		}

		return SkeletalMeshes.Num() > 0 ? SkeletalMeshes[0] : nullptr;
	}

	bool TMCanUseNativeBlueprintLineTrace(const ATMCharacter* Character)
	{
		return Character && TMResolveActiveGun(Character);
	}

	TSubclassOf<AActor> TMResolveShootFallbackProjectileClass(const ATMCharacter* Character)
	{
		if (Character && Character->ShootFallbackProjectileClass)
		{
			return Character->ShootFallbackProjectileClass;
		}

		static TSubclassOf<AActor> CachedPhysBulletClass;
		if (!CachedPhysBulletClass)
		{
			CachedPhysBulletClass = LoadClass<AActor>(
				nullptr,
				TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_PhysBullet.BP_PhysBullet_C"));
		}
		return CachedPhysBulletClass;
	}

	bool TMInvokeSvrTrail(ATMCharacter* Character, const FVector& Velocity)
	{
		return TMInvokeBlueprintFunction(
			Character,
			TEXT("Svr_Trail"),
			[&Velocity](UFunction* Function, void* Parameters)
			{
				TMSetVectorFunctionParameter(Function, Parameters, { TEXT("Velocity") }, Velocity);
			});
	}

	bool TMInvokeSvrDelay(ATMCharacter* Character, const double HitDelay)
	{
		return TMInvokeBlueprintFunction(
			Character,
			TEXT("Svr_Delay"),
			[HitDelay](UFunction* Function, void* Parameters)
			{
				TMSetNumericFunctionParameter(Function, Parameters, { TEXT("HitDelay") }, HitDelay);
			});
	}

	bool TMInvokeBulletPenetration(
		ATMCharacter* Character,
		const FVector& Speed,
		const FHitResult& InitialHit,
		const TArray<AActor*>& ActorsToIgnore)
	{
		return TMInvokeBlueprintFunction(
			Character,
			TEXT("BulletPenetration"),
			[&Speed, &InitialHit, &ActorsToIgnore](UFunction* Function, void* Parameters)
			{
				TMSetVectorFunctionParameter(Function, Parameters, { TEXT("Speed") }, Speed);
				TMSetHitResultFunctionParameter(Function, Parameters, { TEXT("InitialHit") }, InitialHit);
				TMSetActorArrayFunctionParameter(Function, Parameters, { TEXT("ActorsToIgnore") }, ActorsToIgnore);
			});
	}

	bool TMInvokeSvrLineTrace(
		ATMCharacter* Character,
		const double Distance,
		const FVector& Location,
		const FVector& Normal,
		const FVector& TraceStart,
		const FVector& HitDirection,
		UPhysicalMaterial* PhysicalMaterial,
		AActor* HitActor,
		const FName HitBone,
		const FHitResult& HitInfo)
	{
		const bool bHeadshot = UTMGameplayStatics::IsHeadHitBone(HitBone);
		const FVector HeadshotSoundLocation = bHeadshot
			? TMResolveHeadshotSoundLocation(HitActor, HitInfo, HitBone, Location)
			: FVector::ZeroVector;
		TMStorePendingImpactHeadshot(Character, Location, Normal, PhysicalMaterial, bHeadshot, HeadshotSoundLocation);

		return TMInvokeBlueprintFunction(
			Character,
			TEXT("Svr_LineTrace"),
			[Character, Distance, &Location, &Normal, &TraceStart, &HitDirection, PhysicalMaterial, HitActor, HitBone, &HitInfo, bHeadshot](
				UFunction* Function,
				void* Parameters)
			{
				const float DamageMultiplier = UTMGameplayStatics::GetBoneDamageMultiplier(
					HitBone,
					Character ? Character->HeadshotDamageMultiplier : 4.f,
					Character ? Character->DefaultHitDamageMultiplier : 1.f);

				TMSetNumericFunctionParameter(Function, Parameters, { TEXT("Distance") }, Distance);
				TMSetVectorFunctionParameter(Function, Parameters, { TEXT("Location") }, Location);
				TMSetVectorFunctionParameter(Function, Parameters, { TEXT("Normal"), TEXT("Rotation") }, Normal);
				TMSetVectorFunctionParameter(Function, Parameters, { TEXT("TraceStart") }, TraceStart);
				TMSetVectorFunctionParameter(Function, Parameters, { TEXT("HitDirection") }, HitDirection);
				TMSetObjectFunctionParameter(
					Function,
					Parameters,
					{ TEXT("Physical Material"), TEXT("Physical_Material"), TEXT("PhysicalMaterial"), TEXT("PhysMat") },
					PhysicalMaterial);
				TMSetObjectFunctionParameter(Function, Parameters, { TEXT("HitActor") }, HitActor);
				TMSetNameFunctionParameter(Function, Parameters, { TEXT("HitBone"), TEXT("HitBoneName") }, HitBone);
				TMSetHitResultFunctionParameter(Function, Parameters, { TEXT("HitInfo"), TEXT("OutHit") }, HitInfo);
				TMSetBoolFunctionParameter(
					Function,
					Parameters,
					{
						TEXT("bHeadshot"),
						TEXT("Headshot"),
						TEXT("bHeadShot"),
						TEXT("HeadShot"),
						TEXT("bIsHeadshot"),
						TEXT("IsHeadshot"),
						TEXT("bIsHeadShot"),
						TEXT("IsHeadShot")
					},
					bHeadshot);
				TMSetNumericFunctionParameter(
					Function,
					Parameters,
					{
						TEXT("DamageMultiplier"),
						TEXT("HitDamageMultiplier"),
						TEXT("BoneDamageMultiplier"),
						TEXT("HitBoneDamageMultiplier"),
						TEXT("HeadshotMultiplier"),
						TEXT("HeadShotMultiplier")
					},
					DamageMultiplier);
			});
	}

	void TMInvokeThrowableHitExplode(AActor* HitActor)
	{
		if (!HitActor)
		{
			return;
		}

		if (UFunction* HitExplodeFunction = HitActor->FindFunction(TEXT("HitExplode")))
		{
			HitActor->ProcessEvent(HitExplodeFunction, nullptr);
		}
	}

	AActor* TMSpawnFallbackPhysicalBullet(
		ATMCharacter* Character,
		const FVector& SpawnLocation,
		const FVector& Direction)
	{
		if (!Character)
		{
			return nullptr;
		}

		UWorld* World = Character->GetWorld();
		TSubclassOf<AActor> ProjectileClass = TMResolveShootFallbackProjectileClass(Character);
		const FVector ProjectileDirection = Direction.GetSafeNormal();
		if (!World || !ProjectileClass || ProjectileDirection.IsNearlyZero())
		{
			return nullptr;
		}

		AActor* ProjectileOwner = TMReadObjectProperty<AActor>(Character, TEXT("MPS Controller"));
		if (!ProjectileOwner)
		{
			ProjectileOwner = Character->GetController() ? Cast<AActor>(Character->GetController()) : Character;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = ProjectileOwner;
		SpawnParams.Instigator = Character;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* Projectile = World->SpawnActor<AActor>(
			ProjectileClass,
			SpawnLocation,
			ProjectileDirection.Rotation(),
			SpawnParams);
		if (!Projectile)
		{
			return nullptr;
		}

		const float ProjectileSpeed = Character->ShootFallbackProjectileSpeed;
		if (UProjectileMovementComponent* ProjectileMovement =
			Projectile->FindComponentByClass<UProjectileMovementComponent>())
		{
			const float ResolvedProjectileSpeed =
				ProjectileSpeed > 0.f ? ProjectileSpeed : ProjectileMovement->InitialSpeed;
			if (ResolvedProjectileSpeed > 0.f)
			{
				ProjectileMovement->InitialSpeed = ResolvedProjectileSpeed;
				ProjectileMovement->MaxSpeed = FMath::Max(ProjectileMovement->MaxSpeed, ResolvedProjectileSpeed);
				ProjectileMovement->Velocity = ProjectileDirection * ResolvedProjectileSpeed;
				ProjectileMovement->Activate(true);
			}
		}

		if (ProjectileSpeed > 0.f)
		{
			TMWriteVectorProperty(Projectile, TEXT("Velocity"), ProjectileDirection * ProjectileSpeed);
		}
		return Projectile;
	}

	void TMDestroyBlueprintTraceActor(ATMCharacter* Character)
	{
		AActor* TraceActor = TMReadObjectProperty<AActor>(Character, TEXT("Trace"));
		if (TraceActor && !TraceActor->IsActorBeingDestroyed())
		{
			TraceActor->Destroy();
		}
	}

	bool TMExecuteNativeBlueprintImpact(
		ATMCharacter* Character,
		const FVector& Location,
		const FVector& Normal,
		const UPhysicalMaterial* PhysicalMaterial,
		const bool bHasHeadshotInfo,
		const bool bHeadshot,
		const FVector& HeadshotSoundLocation)
	{
		if (!Character)
		{
			return false;
		}

		if (AGun* ActiveGun = TMResolveActiveGun(Character))
		{
			if (bHasHeadshotInfo)
			{
				ActiveGun->ImpactWithHitContext(Location, Normal, PhysicalMaterial, bHeadshot);
			}
			else
			{
				ActiveGun->Impact(Location, Normal, PhysicalMaterial);
			}
		}

		if (bHasHeadshotInfo && bHeadshot)
		{
			const FVector ResolvedHeadshotSoundLocation = HeadshotSoundLocation.IsNearlyZero()
				? Location
				: HeadshotSoundLocation;
			TMPlayHeadshotHitmarkerSounds(Character, ResolvedHeadshotSoundLocation, Normal);
		}

		TMDestroyBlueprintTraceActor(Character);
		return true;
	}

	bool TMTryHandleNativeBlueprintImpact(ATMCharacter* Character, UFunction* Function, void* Parameters)
	{
		if (!Character
			|| !Function
			|| !Parameters
			|| !Function->GetName().Equals(TEXT("MC_Impact"), ESearchCase::CaseSensitive)
			|| Character->HasAuthority())
		{
			return false;
		}

		FVector Location = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		if (!TMGetVectorFunctionParameter(Function, Parameters, { TEXT("Location") }, Location)
			|| !TMGetVectorFunctionParameter(Function, Parameters, { TEXT("Rotation"), TEXT("Normal") }, Normal))
		{
			return false;
		}

		const UPhysicalMaterial* PhysicalMaterial = TMGetObjectFunctionParameter<UPhysicalMaterial>(
			Function,
			Parameters,
			{ TEXT("PhysMat"), TEXT("PhysicalMaterial"), TEXT("Physical Material"), TEXT("Physical_Material") });
		bool bHeadshot = false;
		FVector HeadshotSoundLocation = FVector::ZeroVector;
		const bool bHasHeadshotInfo = TMConsumePendingImpactHeadshot(
				Character,
				Location,
				Normal,
				PhysicalMaterial,
				bHeadshot,
				HeadshotSoundLocation)
			|| TMExtractImpactHeadshotFromParameters(Function, Parameters, bHeadshot, HeadshotSoundLocation);
		TMExecuteNativeBlueprintImpact(
			Character,
			Location,
			Normal,
			PhysicalMaterial,
			bHasHeadshotInfo,
			bHeadshot,
			HeadshotSoundLocation);
		return true;
	}

	bool TMExecuteNativeBlueprintLineTrace(ATMCharacter* Character)
	{
		if (!Character)
		{
			return false;
		}

		AGun* ActiveGun = TMResolveActiveGun(Character);
		USkeletalMeshComponent* ItemMesh = TMResolveGunItemMesh(ActiveGun);
		if (!ActiveGun || !ItemMesh)
		{
			return false;
		}

		UCameraComponent* CameraComponent = Character->FindComponentByClass<UCameraComponent>();
		FVector CameraLocation = Character->GetPawnViewLocation();
		FVector CameraForward = Character->GetViewRotation().Vector();
		if (CameraComponent)
		{
			CameraLocation = CameraComponent->GetComponentLocation();
			CameraForward = CameraComponent->GetForwardVector();
		}
		CameraForward = CameraForward.GetSafeNormal();
		if (CameraForward.IsNearlyZero())
		{
			CameraForward = Character->GetActorForwardVector().GetSafeNormal();
		}

		double RecoilX = 0.0;
		double RecoilY = 0.0;
		double SpreadReduction = 1.0;
		TMReadNumericProperty(ActiveGun, TEXT("Recoil_X"), RecoilX);
		TMReadNumericProperty(ActiveGun, TEXT("Recoil_Y"), RecoilY);
		TMReadNumericProperty(ActiveGun, TEXT("DT_SpreadReduction"), SpreadReduction);
		if (FMath::IsNearlyZero(SpreadReduction))
		{
			SpreadReduction = 1.0;
		}

		const UAnimInstance* AnimInstance = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
		bool bAiming = TMIsCharacterAimingForCameraWeaponOffset(Character, AnimInstance);
		if (!bAiming)
		{
			TMReadAimStateBoolProperty(Character, bAiming);
		}

		FVector AimDirection = CameraForward;
		if (bAiming && CVarTMShootADSUseReticleRay.GetValueOnGameThread() != 0)
		{
			FVector ReticleWorldLocation = FVector::ZeroVector;
			if (TMGetADSCenterTargetWorldLocation(Character, ReticleWorldLocation))
			{
				const FVector CameraToReticle = ReticleWorldLocation - CameraLocation;
				const FVector ReticleDirection = CameraToReticle.GetSafeNormal();
				if (!ReticleDirection.IsNearlyZero()
					&& FVector::DotProduct(ReticleDirection, CameraForward) > 0.25f)
				{
					AimDirection = ReticleDirection;
				}
			}
		}

		const float MaxYawInDegrees = static_cast<float>(bAiming ? RecoilX / SpreadReduction : RecoilX);
		const float MaxPitchInDegrees = static_cast<float>(bAiming ? RecoilY / SpreadReduction : RecoilY);
		const bool bUseRandomSpread = !bAiming || CVarTMShootADSNoRandomSpread.GetValueOnGameThread() == 0;
		const FVector TraceDirection = bUseRandomSpread
			? UKismetMathLibrary::RandomUnitVectorInEllipticalConeInDegrees(
				AimDirection,
				MaxYawInDegrees,
				MaxPitchInDegrees)
			: AimDirection;

		TArray<AActor*> ActorsToIgnore;
		ActorsToIgnore.Add(ActiveGun);
		ActorsToIgnore.Add(Character);

		const double TraceDistance = FMath::Max(0.0f, Character->ShootTraceDistanceMeters) * 100.0;
		if (TraceDistance <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const FVector CameraTraceEnd = CameraLocation + TraceDirection * TraceDistance;
		FHitResult CameraHit;
		const bool bCameraHit = UKismetSystemLibrary::LineTraceSingle(
			Character,
			CameraLocation,
			CameraTraceEnd,
			ETraceTypeQuery::TraceTypeQuery2,
			true,
			ActorsToIgnore,
			EDrawDebugTrace::None,
			CameraHit,
			true,
			FLinearColor(1.0f, 0.0f, 0.835798f, 1.0f),
			FLinearColor::Green,
			15.0f);

		const FVector MuzzleLocation = ItemMesh->GetSocketLocation(TEXT("Muzzle"));
		const FVector WeaponTraceEnd = bCameraHit
			? CameraHit.Location + TraceDirection
			: CameraTraceEnd;
		const FVector BulletDirection = UKismetMathLibrary::GetDirectionUnitVector(MuzzleLocation, WeaponTraceEnd);
		const FVector TrailVelocity = BulletDirection * 20000.0;
		TMInvokeSvrTrail(Character, TrailVelocity);

		FHitResult WeaponHit;
		const bool bWeaponHit = UKismetSystemLibrary::LineTraceSingle(
			Character,
			MuzzleLocation,
			WeaponTraceEnd,
			ETraceTypeQuery::TraceTypeQuery2,
			true,
			ActorsToIgnore,
			EDrawDebugTrace::None,
			WeaponHit,
			true,
			FLinearColor(1.0f, 0.021123f, 0.0f, 1.0f),
			FLinearColor(0.036429f, 0.0f, 1.0f, 1.0f),
			5.0f);

		if (!bWeaponHit)
		{
			TMSpawnFallbackPhysicalBullet(Character, MuzzleLocation, BulletDirection);
			return true;
		}

		TMInvokeBulletPenetration(Character, TrailVelocity, WeaponHit, ActorsToIgnore);

		const double HitDelay = static_cast<double>(WeaponHit.Distance) / 20000.0;
		TMWriteNumericProperty(Character, TEXT("HitDelay"), HitDelay);
		TMInvokeSvrDelay(Character, HitDelay);

		TMInvokeSvrLineTrace(
			Character,
			WeaponHit.Distance,
			WeaponHit.ImpactPoint,
			WeaponHit.ImpactNormal,
			WeaponHit.TraceStart,
			BulletDirection,
			WeaponHit.PhysMaterial.Get(),
			WeaponHit.GetActor(),
			WeaponHit.BoneName,
			WeaponHit);
		TMInvokeThrowableHitExplode(WeaponHit.GetActor());
		return true;
	}

	bool TMTryHandleNativeBlueprintLineTrace(ATMCharacter* Character, UFunction* Function)
	{
		if (!Character
			|| !Function
			|| !Function->GetName().Equals(TEXT("LineTrace"), ESearchCase::CaseSensitive)
			|| !TMCanUseNativeBlueprintLineTrace(Character))
		{
			return false;
		}

		Character->Shoot();
		return true;
	}

	bool TMHasBoneOrSocket(const USkeletalMeshComponent* Mesh, const FName BoneOrSocketName)
	{
		return Mesh
			&& !BoneOrSocketName.IsNone()
			&& (Mesh->GetBoneIndex(BoneOrSocketName) != INDEX_NONE || Mesh->DoesSocketExist(BoneOrSocketName));
	}

	bool TMComputeADSWeaponBoneComponentTransform(
		const UAnimInstance* AnimInstance,
		const ATMCharacter* Character,
		const USkeletalMeshComponent* Mesh,
		const FTransform& ADSSocketWorldTransform,
		FTransform& OutWeaponBoneComponentTransform,
		FTransform& OutDesiredSocketWorldTransform)
	{
		if (!AnimInstance || !Character || !Mesh)
		{
			OutWeaponBoneComponentTransform = FTransform::Identity;
			OutDesiredSocketWorldTransform = FTransform::Identity;
			return false;
		}

		static const FName ADSWeaponBoneNamePropertyName(TEXT("ADSWeaponBoneName"));
		static const FName CameraWeaponOffsetAimingPropertyName(TEXT("CameraWeaponOffsetAiming"));
		static const FName ADSWeaponBoneRotationStrengthPropertyName(TEXT("ADSWeaponBoneRotationStrength"));

		FName WeaponBoneName(TEXT("Weapon"));
		TMReadNameProperty(AnimInstance, ADSWeaponBoneNamePropertyName, WeaponBoneName);
		if (!TMHasBoneOrSocket(Mesh, WeaponBoneName))
		{
			OutWeaponBoneComponentTransform = FTransform::Identity;
			OutDesiredSocketWorldTransform = FTransform::Identity;
			return false;
		}

		const FTransform CurrentWeaponBoneWorldTransform = Mesh->GetSocketTransform(WeaponBoneName, RTS_World);
		const FTransform ADSSocketRelativeToWeaponBone =
			ADSSocketWorldTransform.GetRelativeTransform(CurrentWeaponBoneWorldTransform);

		FTransform SocketCameraOffset = FTransform::Identity;
		TMReadTransformProperty(AnimInstance, CameraWeaponOffsetAimingPropertyName, SocketCameraOffset);
		SocketCameraOffset.SetRotation(FQuat::Identity);
		SocketCameraOffset.SetScale3D(FVector::OneVector);
		OutDesiredSocketWorldTransform = SocketCameraOffset * TMGetCameraWorldTransform(Character);
		const FTransform FullySolvedWeaponBoneWorldTransform =
			ADSSocketRelativeToWeaponBone.Inverse() * OutDesiredSocketWorldTransform;

		double RotationStrength = 0.0;
		TMReadNumericProperty(AnimInstance, ADSWeaponBoneRotationStrengthPropertyName, RotationStrength);
		RotationStrength = FMath::Clamp(RotationStrength, 0.0, 1.0);

		const FQuat DesiredWeaponBoneWorldRotation = FQuat::Slerp(
			CurrentWeaponBoneWorldTransform.GetRotation(),
			FullySolvedWeaponBoneWorldTransform.GetRotation(),
			RotationStrength).GetNormalized();
		const FVector DesiredWeaponBoneWorldLocation =
			OutDesiredSocketWorldTransform.GetLocation()
			- DesiredWeaponBoneWorldRotation.RotateVector(ADSSocketRelativeToWeaponBone.GetLocation());
		const FTransform DesiredWeaponBoneWorldTransform(
			DesiredWeaponBoneWorldRotation,
			DesiredWeaponBoneWorldLocation,
			CurrentWeaponBoneWorldTransform.GetScale3D());

		OutWeaponBoneComponentTransform =
			DesiredWeaponBoneWorldTransform.GetRelativeTransform(Mesh->GetComponentTransform());
		OutWeaponBoneComponentTransform.NormalizeRotation();
		return true;
	}

	void TMApplyActiveGunCameraOffsetToFPCameraSocket(ATMCharacter* Character, USkeletalMeshComponent* Mesh, const AGun* ActiveGun)
	{
		if (!Mesh || !ActiveGun)
		{
			return;
		}

		USkeletalMesh* SkeletalMesh = Mesh->GetSkeletalMeshAsset();
		if (!SkeletalMesh)
		{
			return;
		}

		USkeletalMeshSocket* FPCameraSocket = nullptr;
		if (USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
		{
			FPCameraSocket = Skeleton->FindSocket(TMMPCameraFPSocketName);
		}

		if (!FPCameraSocket)
		{
			FPCameraSocket = SkeletalMesh->FindSocket(TMMPCameraFPSocketName);
		}

		if (!FPCameraSocket)
		{
			return;
		}

		const bool bUseAimingOffset = TMIsCharacterAimingForCameraWeaponOffset(Character, Mesh->GetAnimInstance());
		const FTransform CameraWeaponOffset = bUseAimingOffset
			? TMGetDefaultCameraWeaponOffsetAiming(ActiveGun)
			: TMGetDefaultCameraWeaponOffset(ActiveGun);
		const FVector TargetLocation = CameraWeaponOffset.GetLocation();
		UCameraComponent* CameraComponent = Character && Character->IsLocallyControlled()
			? Character->FindComponentByClass<UCameraComponent>()
			: nullptr;
		if (CameraComponent && CameraComponent->IsUsingAbsoluteRotation())
		{
			CameraComponent->SetAbsolute(
				CameraComponent->IsUsingAbsoluteLocation(),
				false,
				CameraComponent->IsUsingAbsoluteScale());
		}

		const bool bNeedsSocketLocationUpdate = !FPCameraSocket->RelativeLocation.Equals(TargetLocation, KINDA_SMALL_NUMBER);
		const bool bNeedsCameraRollUpdate = CameraComponent
			&& !FMath::IsNearlyZero(CameraComponent->GetComponentRotation().GetNormalized().Roll, 0.01f);
		if (!bNeedsSocketLocationUpdate && !bNeedsCameraRollUpdate)
		{
			return;
		}

		bool bHasViewTarget = false;
		FVector ViewTarget = FVector::ZeroVector;
		if (Character && CameraComponent)
		{
			const FVector ViewLocation = CameraComponent->GetComponentLocation();
			const FRotator ViewRotation = Character->GetViewRotation();
			const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * 100000.0f;

			FHitResult HitResult;
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TMApplyActiveGunCameraOffsetToFPCameraSocket), false);
			QueryParams.AddIgnoredActor(Character);
			QueryParams.AddIgnoredActor(ActiveGun);
			bHasViewTarget = Character->GetWorld()
				&& Character->GetWorld()->LineTraceSingleByChannel(
					HitResult,
					ViewLocation,
					TraceEnd,
					ECC_Visibility,
					QueryParams);
			ViewTarget = bHasViewTarget ? HitResult.ImpactPoint : TraceEnd;
			bHasViewTarget = true;
		}

		if (bNeedsSocketLocationUpdate)
		{
#if WITH_EDITOR
			FPCameraSocket->Modify();
#endif
			FPCameraSocket->RelativeLocation = TargetLocation;
			Mesh->UpdateChildTransforms();
		}

		if (bHasViewTarget && CameraComponent)
		{
			const FVector UpdatedViewLocation = CameraComponent->GetComponentLocation();
			FRotator TargetViewRotation = (ViewTarget - UpdatedViewLocation).Rotation().GetNormalized();
			TargetViewRotation.Roll = 0.0f;

			if (AController* Controller = Character->GetController())
			{
				Controller->SetControlRotation(TargetViewRotation);
			}

			if (!Character->GetController() || !CameraComponent->bUsePawnControlRotation)
			{
				CameraComponent->SetWorldRotation(TargetViewRotation, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}

	void TMUpdateCameraWeaponOffsetAnimBridge(UAnimInstance* AnimInstance, const AGun* ActiveGun, const ATMCharacter* Character)
	{
		if (!AnimInstance || !ActiveGun)
		{
			return;
		}

		static const FName CameraWeaponOffsetPropertyName(TEXT("CameraWeaponOffset"));
		static const FName CameraWeaponOffsetNoAimingPropertyName(TEXT("CameraWeaponOffsetNoAiming"));
		static const FName CameraWeaponOffsetAimingPropertyName(TEXT("CameraWeaponOffsetAiming"));
		const FTransform CameraWeaponOffsetNoAiming = TMGetDefaultCameraWeaponOffset(ActiveGun);
		const FTransform CameraWeaponOffsetAiming = TMGetDefaultCameraWeaponOffsetAiming(ActiveGun);
		const bool bUseAimingOffset = TMIsCharacterAimingForCameraWeaponOffset(Character, AnimInstance);

		TMWriteTransformProperty(
			AnimInstance,
			CameraWeaponOffsetPropertyName,
			bUseAimingOffset ? CameraWeaponOffsetAiming : CameraWeaponOffsetNoAiming);
		TMWriteTransformProperty(
			AnimInstance,
			CameraWeaponOffsetNoAimingPropertyName,
			CameraWeaponOffsetNoAiming);
		TMWriteTransformProperty(
			AnimInstance,
			CameraWeaponOffsetAimingPropertyName,
			CameraWeaponOffsetAiming);
	}

	void TMUpdateADSSocketAnimBridge(
		UAnimInstance* AnimInstance,
		const ATMCharacter* Character,
		const USkeletalMeshComponent* Mesh,
		const FTransform& ADSSocketWorldTransform,
		const bool bHasADSSocket)
	{
		if (!AnimInstance || !Character || !Mesh)
		{
			return;
		}

		static const FName ADSSocketWorldTransformPropertyName(TEXT("ADSSocketWorldTransform"));
		static const FName ADSSocketComponentTransformPropertyName(TEXT("ADSSocketComponentTransform"));
		static const FName ADSCameraTargetWorldTransformPropertyName(TEXT("ADSCameraTargetWorldTransform"));
		static const FName ADSSocketValidPropertyName(TEXT("ADSSocketValid"));
		static const FName ADSSocketAlphaPropertyName(TEXT("ADSSocketAlpha"));
		static const FName ADSEyeReliefPropertyName(TEXT("ADSEyeRelief"));
		static const FName ADSWeaponBoneComponentTransformPropertyName(TEXT("ADSWeaponBoneComponentTransform"));
		static const FName ADSWeaponBoneComponentLocationPropertyName(TEXT("ADSWeaponBoneComponentLocation"));
		static const FName ADSWeaponBoneComponentRotationPropertyName(TEXT("ADSWeaponBoneComponentRotation"));
		static const FName ADSDesiredSocketWorldTransformPropertyName(TEXT("ADSDesiredSocketWorldTransform"));
		static const FName ADSWeaponBonePullAlphaPropertyName(TEXT("ADSWeaponBonePullAlpha"));
		static const FName ADSWeaponBonePullStrengthPropertyName(TEXT("ADSWeaponBonePullStrength"));

		double EyeRelief = 0.0;
		TMReadNumericProperty(AnimInstance, ADSEyeReliefPropertyName, EyeRelief);

		const FTransform ADSSocketComponentTransform =
			bHasADSSocket
				? ADSSocketWorldTransform.GetRelativeTransform(Mesh->GetComponentTransform())
				: FTransform::Identity;
		const FTransform ADSCameraTargetWorldTransform =
			bHasADSSocket
				? TMMakeADSCameraTargetTransform(ADSSocketWorldTransform, EyeRelief)
				: FTransform::Identity;
		const bool bAiming = TMIsAimingForWeaponBoneLock(Character, AnimInstance);
		const double Alpha = bHasADSSocket && bAiming ? 1.0 : 0.0;

		FTransform ADSWeaponBoneComponentTransform = FTransform::Identity;
		FTransform ADSDesiredSocketWorldTransform = FTransform::Identity;
		const bool bHasADSWeaponBoneTransform =
			bHasADSSocket
			&& TMComputeADSWeaponBoneComponentTransform(
				AnimInstance,
				Character,
				Mesh,
				ADSSocketWorldTransform,
				ADSWeaponBoneComponentTransform,
				ADSDesiredSocketWorldTransform);

		double PullStrength = 1.0;
		TMReadNumericProperty(AnimInstance, ADSWeaponBonePullStrengthPropertyName, PullStrength);
		const double PullAlpha = bHasADSWeaponBoneTransform && bAiming ? FMath::Clamp(PullStrength, 0.0, 1.0) : 0.0;

		TMWriteTransformProperty(AnimInstance, ADSSocketWorldTransformPropertyName, bHasADSSocket ? ADSSocketWorldTransform : FTransform::Identity);
		TMWriteTransformProperty(AnimInstance, ADSSocketComponentTransformPropertyName, ADSSocketComponentTransform);
		TMWriteTransformProperty(AnimInstance, ADSCameraTargetWorldTransformPropertyName, ADSCameraTargetWorldTransform);
		TMWriteBoolProperty(AnimInstance, ADSSocketValidPropertyName, bHasADSSocket);
		TMWriteNumericProperty(AnimInstance, ADSSocketAlphaPropertyName, Alpha);
		TMWriteTransformProperty(AnimInstance, ADSWeaponBoneComponentTransformPropertyName, ADSWeaponBoneComponentTransform);
		TMWriteVectorProperty(AnimInstance, ADSWeaponBoneComponentLocationPropertyName, ADSWeaponBoneComponentTransform.GetLocation());
		TMWriteRotatorProperty(AnimInstance, ADSWeaponBoneComponentRotationPropertyName, ADSWeaponBoneComponentTransform.GetRotation().Rotator().GetNormalized());
		TMWriteTransformProperty(AnimInstance, ADSDesiredSocketWorldTransformPropertyName, ADSDesiredSocketWorldTransform);
		TMWriteNumericProperty(AnimInstance, ADSWeaponBonePullAlphaPropertyName, PullAlpha);
	}

	void TMUpdateADSSocketAnimBridge(const ATMCharacter* Character, const USkeletalMeshComponent* Mesh)
	{
		if (!Character || !Mesh)
		{
			return;
		}

		FTransform ADSSocketWorldTransform = FTransform::Identity;
		bool bHasADSSocket = false;
		AGun* ActiveGun = TMResolveActiveGun(Character);
		if (ActiveGun)
		{
			ActiveGun->RefreshADSSocket();
			bHasADSSocket = ActiveGun->GetADSSocketWorldTransform(ADSSocketWorldTransform);
		}

		TMApplyActiveGunCameraOffsetToFPCameraSocket(
			const_cast<ATMCharacter*>(Character),
			const_cast<USkeletalMeshComponent*>(Mesh),
			ActiveGun);
		TMUpdateCameraWeaponOffsetAnimBridge(Mesh->GetAnimInstance(), ActiveGun, Character);
		TMUpdateADSSocketAnimBridge(
			Mesh->GetAnimInstance(),
			Character,
			Mesh,
			ADSSocketWorldTransform,
			bHasADSSocket);

		for (UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			TMUpdateCameraWeaponOffsetAnimBridge(LinkedAnimInstance, ActiveGun, Character);
			TMUpdateADSSocketAnimBridge(
				LinkedAnimInstance,
				Character,
				Mesh,
				ADSSocketWorldTransform,
				bHasADSSocket);
		}
	}

	bool TMIsRightHandIKTargetTooFar(const USkeletalMeshComponent* Mesh)
	{
		static const FName RightHandBoneName(TEXT("hand_r"));
		static const FName RightHandIKTargetBoneName(TEXT("VB LHS_ik_hand_r"));
		static constexpr double MaxRightHandIKTargetDistance = 80.0;

		if (!TMHasBoneOrSocket(Mesh, RightHandBoneName) || !TMHasBoneOrSocket(Mesh, RightHandIKTargetBoneName))
		{
			return false;
		}

		const FVector RightHandLocation = Mesh->GetSocketLocation(RightHandBoneName);
		const FVector RightHandTargetLocation = Mesh->GetSocketLocation(RightHandIKTargetBoneName);
		return FVector::DistSquared(RightHandLocation, RightHandTargetLocation) > FMath::Square(MaxRightHandIKTargetDistance);
	}

	void TMUpdateRightHandIKTargetGuard(UAnimInstance* AnimInstance, const USkeletalMeshComponent* Mesh)
	{
		if (!AnimInstance || !Mesh || !TMIsRightHandIKTargetTooFar(Mesh))
		{
			return;
		}

		static const FName EnableHandIKRPropertyName(TEXT("Enable_HandIK_R"));
		static const FName HandIKRightPropertyName(TEXT("HandIK_Right"));

		TMWriteAlphaLikeProperty(AnimInstance, EnableHandIKRPropertyName, 0.0);
		TMWriteAlphaLikeProperty(AnimInstance, HandIKRightPropertyName, 0.0);
	}

	void TMUpdateRightHandIKTargetGuard(const USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}

		TMUpdateRightHandIKTargetGuard(Mesh->GetAnimInstance(), Mesh);
		for (UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			TMUpdateRightHandIKTargetGuard(LinkedAnimInstance, Mesh);
		}
	}

	bool TMIsForearmRootBoneName(const FString& BoneName)
	{
		const FString UpperName = BoneName.ToUpper();
		return UpperName.Contains(TEXT("LOWERARM"))
			|| UpperName.Contains(TEXT("LOWER_ARM"))
			|| UpperName.Contains(TEXT("FOREARM"))
			|| UpperName.Contains(TEXT("FORE_ARM"));
	}

	bool TMIsWeaponBoneName(const FString& BoneName)
	{
		return BoneName.Equals(TEXT("Weapon"), ESearchCase::IgnoreCase);
	}

	bool TMIsFingerOrWristBoneName(const FString& BoneName)
	{
		const FString UpperName = BoneName.ToUpper();
		return UpperName.Contains(TEXT("WRIST"))
			|| UpperName.Contains(TEXT("FINGER"))
			|| UpperName.Contains(TEXT("THUMB"))
			|| UpperName.Contains(TEXT("INDEX"))
			|| UpperName.Contains(TEXT("MIDDLE"))
			|| UpperName.Contains(TEXT("RING"))
			|| UpperName.Contains(TEXT("PINKY"))
			|| UpperName.Contains(TEXT("LITTLE"))
			|| UpperName.Contains(TEXT("METACARPAL"));
	}

	bool TMIsDescendantOfAnyBone(
		const FReferenceSkeleton& RefSkeleton,
		const int32 BoneIndex,
		const TSet<int32>& RootBoneIndices)
	{
		for (int32 CurrentBoneIndex = BoneIndex; CurrentBoneIndex != INDEX_NONE; CurrentBoneIndex = RefSkeleton.GetParentIndex(CurrentBoneIndex))
		{
			if (RootBoneIndices.Contains(CurrentBoneIndex))
			{
				return true;
			}
		}

		return false;
	}

	bool TMCollectDebugBoneScaleIndices(
		const USkeletalMeshComponent* Mesh,
		const bool bIncludeForearmDescendants,
		const bool bIncludeWeaponBone,
		TArray<int32>& OutBoneIndices,
		FString& OutBoneNames)
	{
		OutBoneIndices.Reset();
		OutBoneNames.Reset();

		const USkeletalMesh* SkeletalMesh = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		if (!SkeletalMesh)
		{
			return false;
		}

		const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
		const int32 BoneCount = RefSkeleton.GetNum();
		TSet<int32> ForearmRootBoneIndices;
		TSet<int32> TargetBoneIndices;

		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const FString BoneName = RefSkeleton.GetBoneName(BoneIndex).ToString();
			if (TMIsForearmRootBoneName(BoneName))
			{
				ForearmRootBoneIndices.Add(BoneIndex);
			}
		}

		if (bIncludeForearmDescendants)
		{
			for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
			{
				if (TMIsDescendantOfAnyBone(RefSkeleton, BoneIndex, ForearmRootBoneIndices))
				{
					TargetBoneIndices.Add(BoneIndex);
				}
			}
		}

		if (bIncludeWeaponBone)
		{
			for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
			{
				const FString BoneName = RefSkeleton.GetBoneName(BoneIndex).ToString();
				if (TMIsWeaponBoneName(BoneName))
				{
					TargetBoneIndices.Add(BoneIndex);
				}
			}
		}

		for (const int32 BoneIndex : TargetBoneIndices)
		{
			OutBoneIndices.Add(BoneIndex);
		}
		OutBoneIndices.Sort();

		static constexpr int32 MaxLoggedBoneNames = 28;
		for (int32 Index = 0; Index < OutBoneIndices.Num() && Index < MaxLoggedBoneNames; ++Index)
		{
			if (!OutBoneNames.IsEmpty())
			{
				OutBoneNames += TEXT(", ");
			}
			OutBoneNames += RefSkeleton.GetBoneName(OutBoneIndices[Index]).ToString();
		}

		if (OutBoneIndices.Num() > MaxLoggedBoneNames)
		{
			OutBoneNames += FString::Printf(TEXT(", ... +%d"), OutBoneIndices.Num() - MaxLoggedBoneNames);
		}

		return OutBoneIndices.Num() > 0;
	}

	bool TMApplyDebugBoneScaleToMesh(
		USkeletalMeshComponent* Mesh,
		const bool bIncludeForearmDescendants,
		const bool bIncludeWeaponBone,
		const float Scale,
		const bool bWaitForParallelEvaluation)
	{
		if (!Mesh)
		{
			return false;
		}

		if (!bIncludeForearmDescendants && bIncludeWeaponBone)
		{
			return false;
		}

		const float SafeScale = FMath::Max(Scale, UE_SMALL_NUMBER);
		const USkeletalMesh* SkeletalMesh = Mesh->GetSkeletalMeshAsset();
		TArray<int32> TargetBoneIndices;
		FString TargetBoneNames;
		if (!TMCollectDebugBoneScaleIndices(
			Mesh,
			bIncludeForearmDescendants,
			bIncludeWeaponBone,
			TargetBoneIndices,
			TargetBoneNames))
		{
			return false;
		}

		if (bWaitForParallelEvaluation)
		{
			Mesh->HandleExistingParallelEvaluationTask(true, true);
		}

		const FVector Scale3D(SafeScale);
		const FVector WeaponScale3D(1.0f);
		bool bApplied = false;

		TArray<FTransform>& ComponentSpaceTransforms =
			const_cast<TArray<FTransform>&>(Mesh->GetComponentSpaceTransforms());
		for (const int32 BoneIndex : TargetBoneIndices)
		{
			if (ComponentSpaceTransforms.IsValidIndex(BoneIndex))
			{
				const FString BoneName = SkeletalMesh ? SkeletalMesh->GetRefSkeleton().GetBoneName(BoneIndex).ToString() : FString();
				ComponentSpaceTransforms[BoneIndex].SetScale3D(TMIsWeaponBoneName(BoneName) ? WeaponScale3D : Scale3D);
				bApplied = true;
			}
		}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		for (const int32 BoneIndex : TargetBoneIndices)
		{
			if (Mesh->BoneSpaceTransforms.IsValidIndex(BoneIndex))
			{
				const FString BoneName = SkeletalMesh ? SkeletalMesh->GetRefSkeleton().GetBoneName(BoneIndex).ToString() : FString();
				Mesh->BoneSpaceTransforms[BoneIndex].SetScale3D(TMIsWeaponBoneName(BoneName) ? WeaponScale3D : Scale3D);
				bApplied = true;
			}
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		if (bApplied)
		{
			const uint32 MeshId = Mesh->GetUniqueID();
			if (!GTMDebugBoneScaleReportedMeshIds.Contains(MeshId))
			{
				GTMDebugBoneScaleReportedMeshIds.Add(MeshId);
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[TMDebugLocalHandsScale] Applied Mesh=%s Asset=%s Bones=%d ForearmDesc=%d WeaponBone=%d BaseScale=%.3f WristFingerScale=Base WeaponBoneScale=1.000 BonesSample=[%s]"),
					*Mesh->GetPathName(),
					Mesh->GetSkeletalMeshAsset() ? *Mesh->GetSkeletalMeshAsset()->GetPathName() : TEXT("None"),
					TargetBoneIndices.Num(),
					bIncludeForearmDescendants ? 1 : 0,
					bIncludeWeaponBone ? 1 : 0,
					SafeScale,
					*TargetBoneNames);
			}
			Mesh->InvalidateCachedBounds();
			Mesh->UpdateBounds();
			Mesh->UpdateChildTransforms();
			Mesh->MarkRenderDynamicDataDirty();
		}

		return bApplied;
	}

	void TMRegisterDebugBoneScaleDelegate(
		USkeletalMeshComponent* Mesh,
		const bool bIncludeForearmDescendants,
		const bool bIncludeWeaponBone)
	{
		if (!Mesh)
		{
			return;
		}

		for (const FTMDebugBoneScaleDelegate& Entry : GTMDebugBoneScaleDelegates)
		{
			if (Entry.Mesh.Get() == Mesh
				&& Entry.bIncludeForearmDescendants == bIncludeForearmDescendants
				&& Entry.bIncludeWeaponBone == bIncludeWeaponBone)
			{
				return;
			}
		}

		const TWeakObjectPtr<USkeletalMeshComponent> WeakMesh(Mesh);
		const FDelegateHandle Handle = Mesh->RegisterOnBoneTransformsFinalizedDelegate(
			FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateLambda(
				[WeakMesh, bIncludeForearmDescendants, bIncludeWeaponBone]()
				{
					if (!TMIsDebugLocalHandsScaleEnabled())
					{
						return;
					}

					if (USkeletalMeshComponent* LiveMesh = WeakMesh.Get())
					{
						TMApplyDebugBoneScaleToMesh(
							LiveMesh,
							bIncludeForearmDescendants,
							bIncludeWeaponBone,
							GTMDebugLocalHandsScale,
							false);
					}
				}));

		FTMDebugBoneScaleDelegate Entry;
		Entry.Mesh = WeakMesh;
		Entry.Handle = Handle;
		Entry.bIncludeForearmDescendants = bIncludeForearmDescendants;
		Entry.bIncludeWeaponBone = bIncludeWeaponBone;
		GTMDebugBoneScaleDelegates.Add(Entry);
	}

	void TMApplyDebugLocalHandsScaleForCharacter(ATMCharacter* Character)
	{
		if (!TMIsDebugLocalHandsScaleEnabled() || !Character || !Character->IsLocallyControlled())
		{
			return;
		}

		if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
		{
			TMRegisterDebugBoneScaleDelegate(CharacterMesh, true, true);
			TMApplyDebugBoneScaleToMesh(CharacterMesh, true, true, GTMDebugLocalHandsScale, true);
		}
	}

	void TMConsoleDebugLocalHandsScale(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() > 0)
		{
			GTMDebugLocalHandsScaleConsoleOverrideSet = true;
			GTMDebugLocalHandsScaleEnabled = FCString::Atoi(*Args[0]) != 0;
		}
		else
		{
			const bool bCurrentlyEnabled = TMIsDebugLocalHandsScaleEnabled();
			GTMDebugLocalHandsScaleConsoleOverrideSet = true;
			GTMDebugLocalHandsScaleEnabled = !bCurrentlyEnabled;
		}

		if (Args.Num() > 1)
		{
			GTMDebugLocalHandsScale = FCString::Atof(*Args[1]);
		}

		int32 AppliedCharacterCount = 0;
		if (World)
		{
			for (TActorIterator<ATMCharacter> It(World); It; ++It)
			{
				ATMCharacter* Character = *It;
				if (Character && Character->IsLocallyControlled())
				{
					TMApplyDebugLocalHandsScaleForCharacter(Character);
					++AppliedCharacterCount;
				}
			}
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TMDebugLocalHandsScale] Console Enabled=%d BaseScale=%.3f LocalCharacters=%d"),
			GTMDebugLocalHandsScaleEnabled ? 1 : 0,
			GTMDebugLocalHandsScale,
			AppliedCharacterCount);
	}

	FAutoConsoleCommandWithWorldAndArgs GTMDebugLocalHandsScaleConsoleCommand(
		TEXT("tm.DebugLocalHandsScale"),
		TEXT("tm.DebugLocalHandsScale [0|1] [Scale]. Runtime debug: scale local character forearm descendants and Weapon bone."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TMConsoleDebugLocalHandsScale));
}

ATMCharacter::ATMCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;

	AudioMuffleSoundClasses.Add(TSoftObjectPtr<USoundClass>(FSoftObjectPath(TMProjectMasterSoundClassPath)));
	AudioMuffleSoundClasses.Add(TSoftObjectPtr<USoundClass>(FSoftObjectPath(TMEngineMasterSoundClassPath)));
}

AGun* ATMCharacter::GetActiveGun() const
{
	return TMResolveActiveGun(this);
}

void ATMCharacter::Shoot()
{
	if (!TMExecuteNativeBlueprintLineTrace(this) && IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[TMNativeBlueprintShoot] Failed Character=%s ActiveWeaponState={%s}"),
			*TMObjectName(this),
			*TMDescribeActiveWeaponState(this));
	}
}

void ATMCharacter::ServerSetCharacterSkinId(FName SkinId)
{
	if (HasAuthority())
	{
		if (ATMPlayerState* TMPlayerState = GetPlayerState<ATMPlayerState>())
		{
			TMPlayerState->SetSelectedCharacterSkinIdFromServer(SkinId, true);
		}
		else
		{
			SetAppliedCharacterSkinId(SkinId);
		}
		return;
	}

	ServerSetCharacterSkinIdInternal(SkinId);
}

void ATMCharacter::ServerSetCharacterSkinIdInternal_Implementation(FName SkinId)
{
	ServerSetCharacterSkinId(SkinId);
}

bool ATMCharacter::ApplyCharacterSkinById(FName SkinId)
{
	const FName ResolvedSkinId = ResolveCharacterSkinId(SkinId);
	const FTMCharacterSkinPreset* Preset = FindCharacterSkinPreset(ResolvedSkinId);
	if (!Preset)
	{
		return false;
	}

	ApplyCharacterSkinPreset(*Preset);
	return true;
}

void ATMCharacter::ApplyCharacterSkinFromPlayerState()
{
	FName RequestedSkinId = NAME_None;
	ATMPlayerState* TMPlayerState = GetPlayerState<ATMPlayerState>();
	if (TMPlayerState)
	{
		RequestedSkinId = TMPlayerState->GetSelectedCharacterSkinId();
	}

	const FName ResolvedSkinId = ResolveCharacterSkinId(RequestedSkinId);
	if (HasAuthority())
	{
		if (TMPlayerState && TMPlayerState->GetSelectedCharacterSkinId() != ResolvedSkinId)
		{
			TMPlayerState->SetSelectedCharacterSkinIdFromServer(ResolvedSkinId, false);
		}

		SetAppliedCharacterSkinId(ResolvedSkinId);
		return;
	}

	ApplyCharacterSkinById(AppliedCharacterSkinId.IsNone() ? ResolvedSkinId : AppliedCharacterSkinId);
}

FName ATMCharacter::ResolveCharacterSkinId(FName RequestedSkinId) const
{
	if (AvailableCharacterSkinPresets.IsEmpty())
	{
		return NAME_None;
	}

	if (!RequestedSkinId.IsNone() && FindCharacterSkinPreset(RequestedSkinId))
	{
		return RequestedSkinId;
	}

	if (!DefaultCharacterSkinId.IsNone() && FindCharacterSkinPreset(DefaultCharacterSkinId))
	{
		return DefaultCharacterSkinId;
	}

	return AvailableCharacterSkinPresets[0].SkinId;
}

const FTMCharacterSkinPreset* ATMCharacter::FindCharacterSkinPreset(FName SkinId) const
{
	if (SkinId.IsNone())
	{
		return nullptr;
	}

	return AvailableCharacterSkinPresets.FindByPredicate([SkinId](const FTMCharacterSkinPreset& Preset)
	{
		return Preset.SkinId == SkinId;
	});
}

bool ATMCharacter::SetAppliedCharacterSkinId(FName SkinId)
{
	const FName ResolvedSkinId = ResolveCharacterSkinId(SkinId);
	if (ResolvedSkinId.IsNone())
	{
		return false;
	}

	const bool bChanged = AppliedCharacterSkinId != ResolvedSkinId;
	AppliedCharacterSkinId = ResolvedSkinId;
	ApplyCharacterSkinById(AppliedCharacterSkinId);

	if (bChanged)
	{
		ForceNetUpdate();
	}

	return true;
}

void ATMCharacter::OnRep_AppliedCharacterSkinId(FName PreviousSkinId)
{
	if (AppliedCharacterSkinId != PreviousSkinId)
	{
		ApplyCharacterSkinById(AppliedCharacterSkinId);
	}
}

void ATMCharacter::ApplyCharacterSkinPreset(const FTMCharacterSkinPreset& Preset)
{
	USkeletalMeshComponent* MainMeshComponent = GetMesh();
	if (!MainMeshComponent)
	{
		return;
	}

	if (USkeletalMesh* MainMesh = Preset.MainMesh.LoadSynchronous())
	{
		if (MainMeshComponent->GetSkeletalMeshAsset() != MainMesh)
		{
			MainMeshComponent->SetSkeletalMeshAsset(MainMesh);
		}
	}

	if (UMaterialInterface* MainMaterial = Preset.MainMaterial.LoadSynchronous())
	{
		MainMeshComponent->SetMaterial(0, MainMaterial);
	}

	USkeletalMeshComponent* SecondaryMeshComponent = ResolveSecondarySkinMeshComponent();
	USkeletalMesh* SecondaryMesh = Preset.SecondaryMesh.LoadSynchronous();
	if (SecondaryMeshComponent && SecondaryMesh)
	{
		if (SecondaryMeshComponent->GetSkeletalMeshAsset() != SecondaryMesh)
		{
			SecondaryMeshComponent->SetSkeletalMeshAsset(SecondaryMesh);
		}

		SecondaryMeshComponent->SetLeaderPoseComponent(MainMeshComponent, true);
		SecondaryMeshComponent->SetHiddenInGame(false, true);
		SecondaryMeshComponent->SetVisibility(true, true);
		SecondaryMeshComponent->SetComponentTickEnabled(true);
		SecondaryMeshComponent->MarkRenderStateDirty();
	}

	MainMeshComponent->SetHiddenInGame(false, true);
	MainMeshComponent->SetVisibility(true, true);
	MainMeshComponent->MarkRenderStateDirty();
}

USkeletalMeshComponent* ATMCharacter::ResolveSecondarySkinMeshComponent() const
{
	USkeletalMeshComponent* MainMeshComponent = GetMesh();
	TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(this);

	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent || MeshComponent == MainMeshComponent)
		{
			continue;
		}

		if (MeshComponent->GetFName() == TEXT("SecondaryMesh")
			|| MeshComponent->GetName().Contains(TEXT("Secondary"), ESearchCase::IgnoreCase))
		{
			return MeshComponent;
		}
	}

	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (MeshComponent && MeshComponent != MainMeshComponent && MeshComponent->LeaderPoseComponent.Get() == MainMeshComponent)
		{
			return MeshComponent;
		}
	}

	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (MeshComponent && MeshComponent != MainMeshComponent)
		{
			return MeshComponent;
		}
	}

	return nullptr;
}

void ATMCharacter::HideRuntimeHelperComponents()
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent)
		{
			continue;
		}

		const FName ComponentName = PrimitiveComponent->GetFName();
		const FString ComponentNameString = ComponentName.ToString();
		const FString ComponentClassName = PrimitiveComponent->GetClass()->GetName();
		const bool bIsRuntimeHelper =
			ComponentName == TMItemReleaseComponentName
			|| ComponentNameString.StartsWith(TMCameraProxyMeshComponentPrefix)
			|| ComponentNameString.StartsWith(TMDrawFrustumComponentPrefix)
			|| ComponentClassName == TMCameraProxyMeshComponentPrefix
			|| ComponentClassName == TMDrawFrustumComponentPrefix;
		if (!bIsRuntimeHelper)
		{
			continue;
		}

		PrimitiveComponent->SetHiddenInGame(true, true);
		PrimitiveComponent->SetVisibility(false, true);
		PrimitiveComponent->MarkRenderStateDirty();
	}
}

void ATMCharacter::UpdateLocalViewMeshVisibility()
{
	USkeletalMeshComponent* MainMeshComponent = GetMesh();
	if (!MainMeshComponent)
	{
		return;
	}

	MainMeshComponent->SetOwnerNoSee(bIsLocalPlayerControlled);
	MainMeshComponent->MarkRenderStateDirty();
}

bool ATMCharacter::GetActiveWeaponADSSocketWorldTransform(FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;

	AGun* ActiveGun = GetActiveGun();
	if (!ActiveGun)
	{
		return false;
	}

	ActiveGun->RefreshADSSocket();
	return ActiveGun->GetADSSocketWorldTransform(OutTransform);
}

bool ATMCharacter::GetActiveWeaponADSCameraTargetTransform(const float EyeRelief, FTransform& OutTransform) const
{
	FTransform ADSSocketWorldTransform = FTransform::Identity;
	if (!GetActiveWeaponADSSocketWorldTransform(ADSSocketWorldTransform))
	{
		OutTransform = FTransform::Identity;
		return false;
	}

	OutTransform = TMMakeADSCameraTargetTransform(ADSSocketWorldTransform, EyeRelief);
	return true;
}

void ATMCharacter::BeginPlay()
{
	Super::BeginPlay();

	TMEnsureMPCameraBoomAttachment(this);
	UpdateLocalPlayerControlledFlag();
	ApplyCharacterSkinFromPlayerState();
	UpdateLocalViewMeshVisibility();
	HideRuntimeHelperComponents();
	if (IsTouchMeRuntimeTraceEnabled())
	{
		BindRuntimeTraceAnimDelegates();
		LogRuntimeTraceSnapshot(TEXT("BeginPlay"));
	}
}

void ATMCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TMFlushViewmodelConsoleWeaponProfile(this);
	PopAudioMuffleSoundMix();

	Super::EndPlay(EndPlayReason);
}

void ATMCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateLocalPlayerControlledFlag();
	TMUpdateViewmodelConsoleWeaponProfile(this);
	UTMGameplayStatics::ApplyALSTurnInPlaceState(this, DeltaSeconds);
	TMUpdateFabrikFixerAlpha(GetMesh());
	TMUpdateWeaponBoneFloorLock(this, GetMesh());
	TMUpdateADSSocketAnimBridge(this, GetMesh());
	TMApplyViewmodelCameraOffset(this);
	TMApplyOpticCameraFOVGuard(this);
	TMUpdateRightHandIKTargetGuard(GetMesh());
	UpdateAnimCurveAudioMuffle(DeltaSeconds);
	// TMApplyDebugLocalHandsScaleForCharacter(this);
	if (!IsTouchMeRuntimeTraceEnabled())
	{
		return;
	}

	BindRuntimeTraceAnimDelegates();

	if (!bIsLocalPlayerControlled)
	{
		return;
	}

	RuntimeTraceAccumulator += DeltaSeconds;
	if (RuntimeTraceAccumulator < 0.1f)
	{
		return;
	}
	RuntimeTraceAccumulator = 0.f;

	LogRuntimeTraceSnapshot(TEXT("Tick"));
}

void ATMCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	UpdateLocalPlayerControlledFlag();
	ApplyCharacterSkinFromPlayerState();
	UpdateLocalViewMeshVisibility();
	HideRuntimeHelperComponents();
}

void ATMCharacter::UnPossessed()
{
	Super::UnPossessed();

	UpdateLocalPlayerControlledFlag();
	UpdateLocalViewMeshVisibility();
}

void ATMCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	UpdateLocalPlayerControlledFlag();
	UpdateLocalViewMeshVisibility();
	if (IsTouchMeRuntimeTraceEnabled())
	{
		BindRuntimeTraceAnimDelegates();
	}
}

void ATMCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	ApplyCharacterSkinFromPlayerState();
	UpdateLocalViewMeshVisibility();
	HideRuntimeHelperComponents();
}

void ATMCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATMCharacter, AppliedCharacterSkinId);
}

void ATMCharacter::ProcessEvent(UFunction* Function, void* Parameters)
{
	if (TMTryHandleNativeBlueprintLineTrace(this, Function)
		|| TMTryHandleNativeBlueprintImpact(this, Function, Parameters))
	{
		return;
	}

	const bool bTrace = IsTouchMeRuntimeTraceEnabled() && TMShouldTraceFunction(Function);
	const bool bMPSServerAimFunction = TMIsMPSServerAimFunction(Function);
	const bool bALSAimBridgeCandidate = bMPSServerAimFunction || TMIsAimBridgeFunction(Function);
	bool bFallbackALSAimingState = false;
	const bool bHasFallbackALSAimingState = bALSAimBridgeCandidate
		&& (TMExtractAimStateFromParameters(Function, Parameters, bFallbackALSAimingState)
			|| TMInferAimStateFromFunctionName(Function, bFallbackALSAimingState));
	const FString BeforeActivationState = bTrace ? TMDescribeActivationCore(this) : FString();
	if (bTrace)
	{
		FString Params;
		TMAppendFunctionParameters(Function, Parameters, Params);
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[CharacterActivationTrace:Before] Function=%s Params={%s} State={%s} %s"),
			Function ? *Function->GetPathName() : TEXT("None"),
			*Params,
			*BeforeActivationState,
			*TMDescribeMontages(GetMesh()));
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[CharacterProcessEvent:Before] Character=%s Function=%s Params={%s} ActiveWeaponState={%s} %s"),
			*TMObjectName(this),
			Function ? *Function->GetPathName() : TEXT("None"),
			*Params,
			*TMDescribeActiveWeaponState(this),
			*TMDescribeMontages(GetMesh()));
	}

	Super::ProcessEvent(Function, Parameters);

	if (bALSAimBridgeCandidate)
	{
		bool bAiming = false;
		bool bHasAimingState = false;
		if (bMPSServerAimFunction && bHasFallbackALSAimingState)
		{
			bAiming = bFallbackALSAimingState;
			bHasAimingState = true;
		}
		else
		{
			bHasAimingState = TMReadAimStateBoolProperty(this, bAiming)
				|| (bHasFallbackALSAimingState ? (bAiming = bFallbackALSAimingState, true) : false);
		}

		if (bHasAimingState && (bMPSServerAimFunction || !bHasLastALSAimBridgeState || bLastALSAimBridgeState != bAiming))
		{
			bHasLastALSAimBridgeState = true;
			bLastALSAimBridgeState = bAiming;
			UTMGameplayStatics::ApplyALSAimState(this, bAiming);

			if (IsTouchMeRuntimeTraceEnabled())
			{
				UE_LOG(
					LogTouchMeRuntimeTrace,
					Warning,
					TEXT("[ALSAimBridge] Character=%s Function=%s bAiming=%d"),
					*TMObjectName(this),
					Function ? *Function->GetPathName() : TEXT("None"),
					bAiming ? 1 : 0);
			}
		}
	}

	if (bTrace)
	{
		const FString AfterActivationState = TMDescribeActivationCore(this);
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[CharacterActivationTrace:After] Function=%s Changed=%d Before={%s} After={%s}"),
			Function ? *Function->GetPathName() : TEXT("None"),
			BeforeActivationState != AfterActivationState ? 1 : 0,
			*BeforeActivationState,
			*AfterActivationState);

		const FString Reason = FString::Printf(
			TEXT("ProcessEventAfter:%s"),
			Function ? *Function->GetName() : TEXT("None"));
		LogRuntimeTraceSnapshot(*Reason);
	}
}

void ATMCharacter::OnRuntimeTraceMontageStarted(UAnimMontage* Montage)
{
	if (!IsTouchMeRuntimeTraceEnabled())
	{
		return;
	}

	UE_LOG(
		LogTouchMeRuntimeTrace,
		Warning,
		TEXT("[MontageStarted] Character=%s Montage=%s Slots=%s %s"),
		*TMObjectName(this),
		*TMMontageName(Montage),
		*TMMontageSlotNames(Montage),
		*TMDescribeMontages(GetMesh()));
}

void ATMCharacter::OnRuntimeTraceMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (!IsTouchMeRuntimeTraceEnabled())
	{
		return;
	}

	UE_LOG(
		LogTouchMeRuntimeTrace,
		Warning,
		TEXT("[MontageBlendingOut] Character=%s Montage=%s Interrupted=%d %s"),
		*TMObjectName(this),
		*TMMontageName(Montage),
		bInterrupted ? 1 : 0,
		*TMDescribeMontages(GetMesh()));
}

void ATMCharacter::OnRuntimeTraceMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!IsTouchMeRuntimeTraceEnabled())
	{
		return;
	}

	UE_LOG(
		LogTouchMeRuntimeTrace,
		Warning,
		TEXT("[MontageEnded] Character=%s Montage=%s Interrupted=%d %s"),
		*TMObjectName(this),
		*TMMontageName(Montage),
		bInterrupted ? 1 : 0,
		*TMDescribeMontages(GetMesh()));
}

void ATMCharacter::OnRuntimeTraceMontageSectionChanged(UAnimMontage* Montage, FName SectionName, bool bLooped)
{
	if (!IsTouchMeRuntimeTraceEnabled())
	{
		return;
	}

	UE_LOG(
		LogTouchMeRuntimeTrace,
		Warning,
		TEXT("[MontageSectionChanged] Character=%s Montage=%s Section=%s Looped=%d %s"),
		*TMObjectName(this),
		*TMMontageName(Montage),
		*SectionName.ToString(),
		bLooped ? 1 : 0,
		*TMDescribeMontages(GetMesh()));
}

void ATMCharacter::BindRuntimeTraceAnimDelegates()
{
	if (!IsTouchMeRuntimeTraceEnabled() || !bIsLocalPlayerControlled || !GetMesh())
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (!AnimInstance || RuntimeTraceAnimInstance.Get() == AnimInstance)
	{
		return;
	}

	if (UAnimInstance* PreviousAnimInstance = RuntimeTraceAnimInstance.Get())
	{
		PreviousAnimInstance->OnMontageStarted.RemoveDynamic(this, &ATMCharacter::OnRuntimeTraceMontageStarted);
		PreviousAnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &ATMCharacter::OnRuntimeTraceMontageBlendingOut);
		PreviousAnimInstance->OnMontageEnded.RemoveDynamic(this, &ATMCharacter::OnRuntimeTraceMontageEnded);
		PreviousAnimInstance->OnMontageSectionChanged.RemoveDynamic(this, &ATMCharacter::OnRuntimeTraceMontageSectionChanged);
	}

	RuntimeTraceAnimInstance = AnimInstance;
	AnimInstance->OnMontageStarted.AddUniqueDynamic(this, &ATMCharacter::OnRuntimeTraceMontageStarted);
	AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &ATMCharacter::OnRuntimeTraceMontageBlendingOut);
	AnimInstance->OnMontageEnded.AddUniqueDynamic(this, &ATMCharacter::OnRuntimeTraceMontageEnded);
	AnimInstance->OnMontageSectionChanged.AddUniqueDynamic(this, &ATMCharacter::OnRuntimeTraceMontageSectionChanged);

	UE_LOG(
		LogTouchMeRuntimeTrace,
		Warning,
		TEXT("[MontageTraceBound] Character=%s %s"),
		*TMObjectName(this),
		*TMDescribeMontages(GetMesh()));
}

void ATMCharacter::LogRuntimeTraceSnapshot(const TCHAR* Reason)
{
	if (!IsTouchMeRuntimeTraceEnabled() || !bIsLocalPlayerControlled)
	{
		return;
	}

	FString Signature = FString::Printf(
		TEXT("Reason=%s Character=%s MeshAnim=%s Mesh={%s} %s"),
		Reason,
		*TMObjectName(this),
		*TMAnimClassName(GetMesh()),
		*TMComponentAttachInfo(GetMesh()),
		*TMDescribeMontages(GetMesh()));

	TMAppendObjectProperties(this, Signature);

	const FString ActiveWeaponSignature = TMDescribeActiveWeaponState(this);
	if (ActiveWeaponSignature != LastRuntimeTraceActiveWeaponSignature)
	{
		LastRuntimeTraceActiveWeaponSignature = ActiveWeaponSignature;
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[ActiveWeaponStateChanged] Reason=%s Character=%s ActiveWeaponState={%s}"),
			Reason,
			*TMObjectName(this),
			*ActiveWeaponSignature);
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AGun> It(World); It; ++It)
		{
			const AGun* Gun = *It;
			if (!Gun)
			{
				continue;
			}

			if (Gun->GetOwner() == this || FVector::DistSquared(Gun->GetActorLocation(), GetActorLocation()) < FMath::Square(400.f))
			{
				Signature += TEXT(" | ");
				Signature += TMGunState(Gun);
			}
		}
	}

	if (Signature != LastRuntimeTraceSignature)
	{
		LastRuntimeTraceSignature = Signature;
		UE_LOG(LogTouchMeRuntimeTrace, Warning, TEXT("[CharacterSnapshot] %s"), *Signature);
	}
}

void ATMCharacter::UpdateLocalPlayerControlledFlag()
{
	bIsLocalPlayerControlled = IsPlayerControlled() && IsLocallyControlled();
}

void ATMCharacter::UpdateAnimCurveAudioMuffle(float DeltaSeconds)
{
	if (TMIsMenuGameModeActive(this))
	{
		CurrentAudioMuffleAlpha = 0.f;
		PopAudioMuffleSoundMix();
		return;
	}

	float TargetAlpha = 0.f;
	if (bEnableAnimCurveAudioMuffle && bIsLocalPlayerControlled)
	{
		float AimedSoundValue = 0.f;
		TMReadMaxAnimCurveValue(GetMesh(), AudioMuffleCurveName, AimedSoundValue);
		TargetAlpha = 1.f - AimedSoundValue;
	}

	if (AudioMuffleInterpSpeed <= 0.f || DeltaSeconds <= 0.f)
	{
		CurrentAudioMuffleAlpha = TargetAlpha;
	}
	else
	{
		CurrentAudioMuffleAlpha = FMath::FInterpTo(CurrentAudioMuffleAlpha, TargetAlpha, DeltaSeconds, AudioMuffleInterpSpeed);
	}

	const bool bNeedsActiveMix = CurrentAudioMuffleAlpha > UE_KINDA_SMALL_NUMBER
		|| TargetAlpha > UE_KINDA_SMALL_NUMBER
		|| bAudioMuffleSoundMixPushed;
	if (!bNeedsActiveMix)
	{
		return;
	}

	if (!EnsureAudioMuffleSoundMix())
	{
		return;
	}

	const float LowPassFrequency = TMInterpolateAudioMuffleFrequency(
		AudioMuffleClearLowPassFrequency,
		AudioMuffleFullyMuffledLowPassFrequency,
		CurrentAudioMuffleAlpha);
	ApplyAudioMuffleLowPassFrequency(LowPassFrequency);
}

bool ATMCharacter::EnsureAudioMuffleSoundMix()
{
	if (AudioMuffleRuntimeSoundMix && bAudioMuffleSoundMixPushed)
	{
		return AudioMuffleRuntimeSoundMix->SoundClassEffects.Num() > 0;
	}

	if (!AudioMuffleRuntimeSoundMix)
	{
		AudioMuffleRuntimeSoundMix = NewObject<USoundMix>(this, TEXT("TMRuntimeAudioMuffleSoundMix"));
		if (!AudioMuffleRuntimeSoundMix)
		{
			return false;
		}

		AudioMuffleRuntimeSoundMix->InitialDelay = 0.f;
		AudioMuffleRuntimeSoundMix->FadeInTime = 0.f;
		AudioMuffleRuntimeSoundMix->Duration = -1.f;
		AudioMuffleRuntimeSoundMix->FadeOutTime = 0.f;
	}

	AudioMuffleRuntimeSoundMix->SoundClassEffects.Reset();

	TSet<USoundClass*> AddedSoundClasses;
	auto AddSoundClass = [this, &AddedSoundClasses](USoundClass* SoundClass)
	{
		if (!SoundClass || AddedSoundClasses.Contains(SoundClass))
		{
			return;
		}

		AddedSoundClasses.Add(SoundClass);

		FSoundClassAdjuster Adjuster;
		Adjuster.SoundClassObject = SoundClass;
		Adjuster.VolumeAdjuster = 1.f;
		Adjuster.PitchAdjuster = 1.f;
		Adjuster.VoiceCenterChannelVolumeAdjuster = 1.f;
		Adjuster.LowPassFilterFrequency = FMath::Clamp(AudioMuffleClearLowPassFrequency, 20.f, 20000.f);
		Adjuster.bApplyToChildren = true;
		AudioMuffleRuntimeSoundMix->SoundClassEffects.Add(Adjuster);
	};

	for (const TSoftObjectPtr<USoundClass>& SoundClassPtr : AudioMuffleSoundClasses)
	{
		AddSoundClass(SoundClassPtr.LoadSynchronous());
	}

	if (const UAudioSettings* AudioSettings = GetDefault<UAudioSettings>())
	{
		AddSoundClass(AudioSettings->GetDefaultSoundClass());
		AddSoundClass(AudioSettings->GetDefaultMediaSoundClass());
	}

	AddSoundClass(TMLoadSoundClassFromPath(TMProjectMasterSoundClassPath));
	AddSoundClass(TMLoadSoundClassFromPath(TMEngineMasterSoundClassPath));

	if (AudioMuffleRuntimeSoundMix->SoundClassEffects.Num() <= 0)
	{
		return false;
	}

	UGameplayStatics::PushSoundMixModifier(this, AudioMuffleRuntimeSoundMix);
	bAudioMuffleSoundMixPushed = true;
	LastAppliedAudioMuffleLowPassFrequency = -1.f;
	return true;
}

void ATMCharacter::ApplyAudioMuffleLowPassFrequency(float LowPassFrequency)
{
	if (!AudioMuffleRuntimeSoundMix || AudioMuffleRuntimeSoundMix->SoundClassEffects.Num() <= 0)
	{
		return;
	}

	LowPassFrequency = FMath::Clamp(LowPassFrequency, 20.f, 20000.f);
	if (LastAppliedAudioMuffleLowPassFrequency >= 0.f
		&& FMath::Abs(LastAppliedAudioMuffleLowPassFrequency - LowPassFrequency) < AudioMuffleUpdateThresholdHz)
	{
		return;
	}

	LastAppliedAudioMuffleLowPassFrequency = LowPassFrequency;

	USoundMix* SoundMix = AudioMuffleRuntimeSoundMix;
	FAudioThread::RunCommandOnAudioThread([SoundMix, LowPassFrequency]()
	{
		if (!SoundMix)
		{
			return;
		}

		for (FSoundClassAdjuster& Adjuster : SoundMix->SoundClassEffects)
		{
			Adjuster.LowPassFilterFrequency = LowPassFrequency;
		}
	});
}

void ATMCharacter::PopAudioMuffleSoundMix()
{
	if (!AudioMuffleRuntimeSoundMix || !bAudioMuffleSoundMixPushed)
	{
		return;
	}

	ApplyAudioMuffleLowPassFrequency(AudioMuffleClearLowPassFrequency);
	UGameplayStatics::PopSoundMixModifier(this, AudioMuffleRuntimeSoundMix);
	bAudioMuffleSoundMixPushed = false;
	LastAppliedAudioMuffleLowPassFrequency = -1.f;
}
