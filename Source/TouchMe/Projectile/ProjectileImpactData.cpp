// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectileImpactData.h"

#include "PhysicalMaterials/PhysicalMaterial.h"

FProjectileImpactEffects UProjectileImpactData::GetEffectsForSurface(
	const TEnumAsByte<EPhysicalSurface> SurfaceType) const
{
	if (const FProjectileImpactEffects* Effects = EffectsBySurface.Find(SurfaceType))
	{
		return *Effects;
	}

	return DefaultEffects;
}

FProjectileImpactEffects UProjectileImpactData::GetEffectsForPhysicalMaterial(
	const UPhysicalMaterial* PhysicalMaterial) const
{
	return GetEffectsForSurface(UPhysicalMaterial::DetermineSurfaceType(PhysicalMaterial));
}
