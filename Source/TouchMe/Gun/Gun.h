// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "Gun.generated.h"

class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UPhysicalMaterial;
class UProjectileImpactData;
class UFakeGunAnimInstance;
class UFXSystemAsset;
class USoundBase;

UCLASS(Blueprintable)
class TOUCHME_API AGun : public AActor
{
	GENERATED_BODY()

public:
	AGun();

	UFUNCTION(BlueprintPure, Category = "Gun", meta = (DisplayName = "Get Weapon Display Name"))
	FText GetWeaponDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "Gun", meta = (DisplayName = "Get Weapon Display Name From Class"))
	static FText GetWeaponDisplayNameFromClass(TSubclassOf<AGun> WeaponClass);

	UFUNCTION(BlueprintCallable, Category = "Gun|Impact")
	void Impact(FVector Location, FVector Normal, const UPhysicalMaterial* PhysicalMaterial);

	void ImpactWithHitContext(FVector Location, FVector Normal, const UPhysicalMaterial* PhysicalMaterial, bool bHeadshot);

	UFUNCTION(BlueprintCallable, Category = "Gun|Fake Mode")
	void SetFakeMode(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Gun|Fake Mode")
	bool IsFakeMode() const { return bFakeMode; }

	UFUNCTION(BlueprintPure, Category = "Gun|Fake Mode")
	UFakeGunAnimInstance* GetFakeAnimInstance() const;

	UFUNCTION(BlueprintCallable, Category = "Gun|Attachments")
	int32 SanitizeInvalidAttachmentComponents();

	UFUNCTION(BlueprintCallable, Category = "Gun|Attachments", meta = (DisplayName = "Spawn Attachment Feedback At Location"))
	void SpawnAttachmentFeedbackAtLocation(FVector Location, FRotator Rotation = FRotator::ZeroRotator);

	UFUNCTION(BlueprintCallable, Category = "Gun|Loadout|Feedback", meta = (DisplayName = "Play Weapon Spawn Feedback"))
	void PlayWeaponSpawnFeedback();

	UFUNCTION(BlueprintCallable, Category = "Gun|Loadout|Feedback", meta = (DisplayName = "Spawn Weapon Spawn Feedback At Location"))
	void SpawnWeaponSpawnFeedbackAtLocation(FVector Location, FRotator Rotation = FRotator::ZeroRotator);

	UFUNCTION(BlueprintCallable, Category = "Gun|Customization", meta = (DisplayName = "Start Customization Skin Preview Cycle"))
	void StartCustomizationSkinPreviewCycle();

	UFUNCTION(BlueprintCallable, Category = "Gun|Customization", meta = (DisplayName = "Stop Customization Skin Preview Cycle"))
	void StopCustomizationSkinPreviewCycle();

	UFUNCTION(BlueprintCallable, Category = "Gun|ADS")
	void RefreshADSSocket();

	UFUNCTION(BlueprintCallable, Category = "Gun|ADS")
	void SetUseSecondaryADSSocket(bool bUseSecondary);

	UFUNCTION(BlueprintPure, Category = "Gun|ADS")
	bool IsUsingSecondaryADSSocket() const { return bUseSecondaryADSSocket; }

	UFUNCTION(BlueprintPure, Category = "Gun|ADS")
	USceneComponent* GetADSSocketComponent() const { return ADSSocketComponent; }

	UFUNCTION(BlueprintPure, Category = "Gun|ADS")
	bool GetADSSocketWorldTransform(FTransform& OutTransform) const;

	UFUNCTION(BlueprintPure, Category = "Gun|ADS")
	FVector GetADSSocketForwardVector() const;

	UFUNCTION(BlueprintPure, Category = "Gun|ADS")
	bool GetActiveOpticZoomMultiplier(float& OutZoomMultiplier) const;

	UFUNCTION(BlueprintPure, Category = "Gun|Camera")
	FTransform GetCameraWeaponOffset() const { return CameraWeaponOffset; }

	UFUNCTION(BlueprintPure, Category = "Gun|Camera")
	FTransform GetCameraWeaponOffsetAiming() const { return CameraWeaponOffsetAiming; }

	virtual void ProcessEvent(UFunction* Function, void* Parameters) override;

protected:
	virtual void PostLoad() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gun")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun")
	TObjectPtr<UProjectileImpactData> Caliber;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Attachments|Feedback")
	TObjectPtr<UFXSystemAsset> AttachmentFeedbackFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Attachments|Feedback")
	TObjectPtr<USoundBase> AttachmentFeedbackSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Attachments|Feedback", meta = (ClampMin = "0.0"))
	FVector AttachmentFeedbackScale = FVector(0.25f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Attachments|Feedback", meta = (ClampMin = "0.0"))
	float AttachmentFeedbackVolume = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Attachments|Feedback", meta = (ClampMin = "0.0"))
	float AttachmentFeedbackPitch = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Attachments|Feedback")
	bool bAttachmentFeedbackPlaySound2D = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Loadout|Feedback")
	TObjectPtr<UFXSystemAsset> WeaponSpawnFeedbackFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Loadout|Feedback")
	TObjectPtr<USoundBase> WeaponSpawnFeedbackSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Loadout|Feedback", meta = (ClampMin = "0.0"))
	FVector WeaponSpawnFeedbackScale = FVector(1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Loadout|Feedback", meta = (ClampMin = "0.0"))
	float WeaponSpawnFeedbackVolume = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Loadout|Feedback", meta = (ClampMin = "0.0"))
	float WeaponSpawnFeedbackPitch = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Loadout|Feedback")
	bool bWeaponSpawnFeedbackPlaySound2D = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Loadout|Feedback")
	bool bAutoPlayWeaponSpawnFeedbackInLoadout = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Loadout|Feedback")
	FName WeaponSpawnFeedbackSocketName = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Gun", meta = (DisplayName = "Display Name"))
	FText WeaponDisplayName;

	UPROPERTY(EditAnywhere, BlueprintGetter = IsFakeMode, BlueprintSetter = SetFakeMode, Category = "Gun|Fake Mode")
	bool bFakeMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Fake Mode", meta = (EditCondition = "bFakeMode"))
	TObjectPtr<USkeletalMesh> FakeSkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Fake Mode", meta = (EditCondition = "bFakeMode"))
	TSubclassOf<UFakeGunAnimInstance> FakeAnimInstanceClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Fake Mode", meta = (EditCondition = "bFakeMode"))
	FTransform FakeSkeletalMeshOffset = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Fake Mode", meta = (EditCondition = "bFakeMode"))
	TObjectPtr<USkeletalMesh> FakeAttachedSkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Fake Mode", meta = (EditCondition = "bFakeMode"))
	FName FakeAttachedSkeletalMeshSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Fake Mode", meta = (EditCondition = "bFakeMode"))
	FTransform FakeAttachedSkeletalMeshOffset = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Camera", meta = (DisplayName = "Camera Weapon Offset"))
	FTransform CameraWeaponOffset = FTransform(
		FRotator::ZeroRotator,
		FVector(0.0f, -8.0f, -9.0f),
		FVector::OneVector);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Camera", meta = (DisplayName = "Camera Weapon Offset Aiming"))
	FTransform CameraWeaponOffsetAiming = FTransform(
		FRotator::ZeroRotator,
		FVector(0.0f, -8.0f, -9.0f),
		FVector::OneVector);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gun|Fake Mode")
	TObjectPtr<USkeletalMeshComponent> FakeSkeletalMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gun|Fake Mode")
	TObjectPtr<USkeletalMeshComponent> FakeAttachedSkeletalMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gun|ADS", meta = (DisplayName = "ADS Eye"))
	TObjectPtr<USceneComponent> ADSSocketComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|ADS", meta = (DisplayName = "ADS Socket Offset"))
	FTransform ADSSocketOffset = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|ADS", meta = (DisplayName = "Secondary ADS Socket Offset"))
	FTransform SecondaryADSSocketOffset = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|ADS", meta = (DisplayName = "Use Secondary ADS Socket"))
	bool bUseSecondaryADSSocket = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|ADS", meta = (DisplayName = "Optic ADS Socket Name"))
	FName OpticADSSocketName = TEXT("ADS_Eye");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|ADS", meta = (DisplayName = "Secondary Optic ADS Socket Name"))
	FName SecondaryOpticADSSocketName = TEXT("ADS_Eye");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|ADS", meta = (DisplayName = "Weapon Optics Socket Name"))
	FName WeaponOpticsSocketName = TEXT("Optics");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|ADS", meta = (DisplayName = "Weapon Secondary Optics Socket Name"))
	FName WeaponSecondaryOpticsSocketName = TEXT("AT_Backup");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|ADS", meta = (DisplayName = "Weapon Iron Sight Socket Name"))
	FName WeaponIronSightSocketName = TEXT("RearSight");

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gun|ADS")
	bool bADSSocketResolved = false;

private:
	static FText MakeDefaultWeaponDisplayName(const UClass* WeaponClass);

	bool HasCustomWeaponDisplayName() const;
	void ImpactInternal(
		FVector Location,
		FVector Normal,
		const UPhysicalMaterial* PhysicalMaterial,
		bool bHasHeadshotInfo,
		bool bHeadshot);
	void ApplyFakeMode();
	void RestoreFromFakeMode();
	void RequestDeferredAttachmentSanitize();
	void RequestDeferredAttachmentFeedback(const UFunction* Function);
	void RunDeferredAttachmentSanitize();
	void PlayAttachmentFeedback();
	FTransform ResolveAttachmentFeedbackTransform() const;
	FTransform ResolveWeaponSpawnFeedbackTransform() const;
	FName ResolveAttachmentFeedbackPreferredSocket(const UFunction* Function) const;
	FName ResolveAttachmentFeedbackSocketFromContext(const FString& Context, FName SocketName) const;
	FName ResolveChangedAttachmentFeedbackSocket(
		const TMap<FName, FString>& CurrentStateSignatures,
		const TMap<FName, FName>& CurrentStateSockets) const;
	void MonitorAttachmentFeedbackState();
	void UpdateAttachmentFeedbackStateSnapshot();
	uint32 BuildAttachmentFeedbackStateHash(
		TMap<FName, FString>* OutStateSignatures = nullptr,
		TMap<FName, FName>* OutStateSockets = nullptr) const;
	bool IsInvalidWeaponAttachmentComponent(const UStaticMeshComponent* Component) const;
	int32 SynchronizeUnderbarrelAttachmentComponent();
	int32 SynchronizeAcogRenderComponents();
	void DestroyAcogRenderComponents();
	void UpdateAcogMaterialParameterCollection() const;
	void RefreshActorTickEnabled();
	USkeletalMeshComponent* ResolveMainSkeletalMesh() const;
	bool ResolveADSSocketAttachTarget(USceneComponent*& OutParent, FName& OutSocketName) const;
	UStaticMeshComponent* ResolvePrimaryOpticComponent() const;
	UStaticMeshComponent* ResolveSecondaryOpticComponent() const;
	UStaticMeshComponent* ResolveAcogOpticComponent() const;
	FName ResolveOpticADSSocket(const USceneComponent* Component, FName PreferredSocketName) const;
	FName ResolveWeaponADSSocket(const USkeletalMeshComponent* Mesh) const;
	FName ResolveSecondaryWeaponADSSocket(const USkeletalMeshComponent* Mesh) const;
	static bool IsWeaponAttachmentMesh(const UStaticMeshComponent* Component);
	static bool IsAcogOpticMesh(const UStaticMeshComponent* Component);
	static bool IsLikelyOpticComponent(const UStaticMeshComponent* Component);
	static bool IsLikelySecondaryOpticComponent(const UStaticMeshComponent* Component);
	static bool ShouldRequestAttachmentSanitizeForFunction(const UFunction* Function);

	static const FName MainSkeletalMeshComponentName;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> AcogRenderDiscComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> AcogGlassComponent;

	bool bAttachmentSanitizeRequested = false;
	bool bAttachmentFeedbackRequested = false;
	bool bAttachmentFeedbackStateChangeDetected = false;
	bool bAttachmentFeedbackStateInitialized = false;
	bool bSanitizingAttachmentComponents = false;
	bool bFakeModeApplied = false;
	bool bMainMeshWasVisible = true;
	bool bAcogRenderTickActive = false;
	FName AttachmentFeedbackPreferredSocketName = NAME_None;
	uint32 LastAttachmentFeedbackStateHash = 0;
	TMap<FName, FString> LastAttachmentFeedbackStateSignatures;
	TMap<FName, FName> LastAttachmentFeedbackStateSockets;
	double AttachmentFeedbackSuppressUntilTime = 0.0;
	FTimerHandle AttachmentFeedbackMonitorTimerHandle;
	uint8 MainMeshPreviousAnimTickOption = 0;
};
