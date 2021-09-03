// Fill out your copyright notice in the Description page of Project Settings.


#include "AFPS_Weapon.h"
#include "Engine/CollisionProfile.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"

#include "AFPS_Character.h"

// todo: fix energy level target 

AAFPS_Weapon::AAFPS_Weapon()
{
	// tick enable
	PrimaryActorTick.bCanEverTick = true;

	MeshComp = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	RootComponent = MeshComp;

	// find default mesh
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(TEXT("/Game/FirstPerson/FPWeapon/Mesh/SK_FPGun.SK_FPGun"));
	if (MeshFinder.Succeeded())
	{
		MeshComp->SetSkeletalMesh(MeshFinder.Object);
	}

	// weapon defaults
	MuzzleSocketName = "Muzzle";
	AttachSocketName = "GripPoint";

	Range = 1'000'00.f;  // 1000 m
	Damage = 10.f;
	StartFireDelay = 0.5f;
	FireRate = 0.1f;
	EnergyLevel = 100.f;
	EnergyRecoveryRate = 50.f;
	EnergyDrainPerShot = 2.5f;

	ShotLineTraceChanel = ECollisionChannel::ECC_Camera;

	DamageType = UDamageType::StaticClass();

	CurrentEnergyLevel = EnergyLevel;
	EnergyLevelTarget = EnergyLevel;
	
	TraceQueryParams.AddIgnoredActor(this);
	TraceQueryParams.bTraceComplex = true;
}

void AAFPS_Weapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	CalculateEnergyLevel(DeltaTime);

	#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		DrawDebug();
	#endif

}

void AAFPS_Weapon::StartFire()
{
	bWantsToFire = true;

	if (CanStartShooting() && HasEnergyForSingleShot())
	{
		StartFire_Internal();
	}
}

void AAFPS_Weapon::StopFire()
{
	bWantsToFire = false;

	StopFire_Internal();
}

void AAFPS_Weapon::OnAttach(AAFPS_Character* InCharacterOwner)
{
	if (InCharacterOwner)
	{
		CharacterOwner = InCharacterOwner;
		SetOwner(InCharacterOwner);

		TraceQueryParams.AddIgnoredActor(InCharacterOwner);
	}
}

bool AAFPS_Weapon::CanStartShooting()
{
	float DeltaTime = GetWorld()->TimeSince(LastTimeWhenFiringStarts);
	return DeltaTime >= StartFireDelay;
}

bool AAFPS_Weapon::HasEnergyForSingleShot()
{
	return CurrentEnergyLevel >= EnergyDrainPerShot;
}

void AAFPS_Weapon::StartFire_Internal()
{
	if (CharacterOwner == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("AAFPS_Weapon::StartFire_Internal() CharacterOwner == nullptr"));
		return;
	}

	LastTimeWhenFiringStarts = GetWorld()->GetTimeSeconds();

	CharacterOwner->PlayFireAnimMontage();  // character fire anim
	PlayStartFireEffects(); // effects

	bIsFiring = true;
	FireLoop();
}

void AAFPS_Weapon::ResetShotTimer(bool bShouldReset)
{
	if (bShouldReset)
	{
		GetWorldTimerManager().SetTimer(TimerHandle_FireLoop, this, &AAFPS_Weapon::FireLoop, FireRate);
	}
	else
	{
		GetWorldTimerManager().ClearTimer(TimerHandle_FireLoop);
	}
}

void AAFPS_Weapon::ShotLineTrace()
{
	if (CharacterOwner == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("AAFPS_Weapon::ShotLineTrace() CharacterOwner is nullptr"));
		PLATFORM_BREAK();
		return;
	}

	FVector EyeLocation;
	FRotator EyeRotation;
	CharacterOwner->GetActorEyesViewPoint(EyeLocation, EyeRotation);

	FVector ShotDirection = EyeRotation.Vector();
	FVector ShotEnd = EyeLocation + ShotDirection * Range;

	LastHit = FHitResult();  // flush old result

	if (GetWorld()->LineTraceSingleByChannel(LastHit, EyeLocation, ShotEnd, ShotLineTraceChanel, TraceQueryParams))
	{
		AActor* HitActor = LastHit.GetActor();

		UGameplayStatics::ApplyPointDamage(HitActor, Damage, ShotDirection, LastHit, CharacterOwner->GetInstigatorController(), this, DamageType);

		PlayHitEffects();  // effects
	}
}

void AAFPS_Weapon::FireLoop()
{
	EnergyLevelTarget = CurrentEnergyLevel - EnergyDrainPerShot;

	ShotLineTrace();

	PlayFireEffects(); // effects

	if (HasEnergyForSingleShot())
	{
		ResetShotTimer(true);  // prepare next shot
	}
	else
	{
		bEnergyWasDrained = true;
		PlayEnergyDrainedEffects(); // effects

		StopFire_Internal();
	}
}

void AAFPS_Weapon::StopFire_Internal()
{
	EnergyLevelTarget = EnergyLevel;

	ResetShotTimer(false); // stop shooting loop
	bIsFiring = false;
}

void AAFPS_Weapon::CalculateEnergyLevel(float DeltaSeconds)
{
	float InterpSpeed = bIsFiring ? (1.f / FireRate) : EnergyRecoveryRate;
	CurrentEnergyLevel = FMath::FInterpConstantTo(CurrentEnergyLevel, EnergyLevelTarget, DeltaSeconds, InterpSpeed);

	if (bEnergyWasDrained && CurrentEnergyLevel == EnergyLevel)
	{
		bEnergyWasDrained = false;
		PlayEnergyRestoredEffects();  // effects
	}
}

void AAFPS_Weapon::PlayStartFireEffects()
{
	OnPlayStartFireEffects(); // Calling blueprint version
}

void AAFPS_Weapon::PlayFireEffects()
{
	OnPlayFireEffects(); // Calling blueprint version
}

void AAFPS_Weapon::PlayHitEffects()
{
	OnPlayHitEffects(); // Calling blueprint verison
}

void AAFPS_Weapon::PlayEnergyDrainedEffects()
{
	OnPlayEnergyDrainedEffects(); // Calling blueprint version
}

void AAFPS_Weapon::PlayEnergyRestoredEffects()
{
	OnPlayEnergyRestoredEffects(); // Calling blueprint version
}

FORCEINLINE void AAFPS_Weapon::DrawDebug()
{
	// draw debug weapon parameters
	FVector DrawLocation = MeshComp->GetSocketLocation(MuzzleSocketName);
	FString Msg = "bWantsToFire: " + FString(bWantsToFire ? "true" : "false") + 
		"\nbEnergyWasDrained: " + FString(bEnergyWasDrained ? "true" : "false") +
		"\nbIsFiring: " + FString((bIsFiring ? "true" : "false")) +
		"\nEnergyCurrent: " + FString::SanitizeFloat(CurrentEnergyLevel) + 
		"\nEnergyTarget:" + FString::SanitizeFloat(EnergyLevelTarget);

	DrawDebugString(GetWorld(), DrawLocation, Msg, 0, FColor::Cyan, 0.f, true);

	// draw debug last hit
	if (LastHit.bBlockingHit)
	{
		DrawDebugSphere(GetWorld(), LastHit.Location, 10.f, 4, FColor::Yellow);
	}
}
