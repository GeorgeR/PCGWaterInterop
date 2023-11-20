// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/PCGWaterData.h"

#include "Data/PCGPointData.h"
#include "WaterBodyActor.h"

void UPCGWaterData::Initialize(const TArray<TWeakObjectPtr<AWaterBody>>& InWaterBodies, const FBox& InBounds, bool bInUseMetadata)
{
	TArray<TWeakObjectPtr<AWaterBody>> FilteredWaterBodies;
	for (TWeakObjectPtr<AWaterBody> WaterBody : InWaterBodies)
	{
		if (WaterBody.IsValid())
		{
			FilteredWaterBodies.Add(WaterBody);
		}
	}

	for (TWeakObjectPtr<AWaterBody> WaterBody : FilteredWaterBodies)
	{
		check(WaterBody.IsValid());
		WaterBodies.Emplace(WaterBody.Get());
	}

	check(!WaterBodies.IsEmpty());

	AWaterBody* FirstWaterBody = WaterBodies[0].Get();
	check(FirstWaterBody);

	Bounds = InBounds;
	bUseMetadata = bInUseMetadata;

	Transform = FirstWaterBody->GetActorTransform();

	if (bUseMetadata)
	{
		static const FName WaterBodyTypeMetadataKey = TEXT("WaterBodyType");
		for (TSoftObjectPtr<AWaterBody> WaterBody : WaterBodies)
		{
			// @todo: enum to name
			const FName WaterBodyTypeName = NAME_None;
			if (!Metadata->HasAttribute(WaterBodyTypeMetadataKey))
			{
				Metadata->CreateNameAttribute(WaterBodyTypeMetadataKey, NAME_None, false);
			}

			// @todo: set from WaterBody->GetWaterBodyType()
		}		
	}
}

FBox UPCGWaterData::GetBounds() const
{
	return Bounds;
}

FBox UPCGWaterData::GetStrictBounds() const
{
	return Bounds;
}

bool UPCGWaterData::SamplePoint(
	const FTransform& InTransform,
	const FBox& InBounds,
	FPCGPoint& OutPoint,
	UPCGMetadata* OutMetadata) const
{
	if (ProjectPoint(InTransform, InBounds, {}, OutPoint, OutMetadata))
	{
		if (InBounds.IsValid)
		{
			return FMath::PointBoxIntersection(OutPoint.Transform.GetLocation(), InBounds.TransformBy(InTransform));
		}
		else
		{
			return (InTransform.GetLocation() - OutPoint.Transform.GetLocation()).SquaredLength() < UE_SMALL_NUMBER;
		}
	}
	
	return false;
}

bool UPCGWaterData::ProjectPoint(
	const FTransform& InTransform,
	const FBox& InBounds,
	const FPCGProjectionParams& InParams,
	FPCGPoint& OutPoint,
	UPCGMetadata* OutMetadata) const
{
	EWaterBodyQueryFlags QueryFlags = EWaterBodyQueryFlags::ComputeImmersionDepth | EWaterBodyQueryFlags::IncludeWaves;
	for (TSoftObjectPtr<AWaterBody> WaterBodyPtr : WaterBodies)
	{
		if (AWaterBody* WaterBody = WaterBodyPtr.Get())
		{
			UWaterBodyComponent* WaterBodyComponent = WaterBody->GetWaterBodyComponent();
			const FWaterBodyQueryResult QueryResult = WaterBodyComponent->QueryWaterInfoClosestToWorldLocation(InTransform.GetLocation(), QueryFlags);
			if (QueryResult.IsInWater())
			{
				OutPoint.Transform.SetIdentity();
				OutPoint.Transform.SetLocation(QueryResult.GetWaterSurfaceLocation());
				OutPoint.Transform.SetRotation(QueryResult.GetWaterSurfaceNormal().ToOrientationQuat());
				OutPoint.Density = QueryResult.GetImmersionDepth();
				break;
			}
		}
	}

	if (!InParams.bProjectRotations)
	{
		OutPoint.Transform.SetRotation(InTransform.GetRotation());
	}
	else
	{
		// Take landscape transform, but respect initial point yaw (don't spin points around Z axis).
		FVector RotVector = InTransform.GetRotation().ToRotationVector();
		RotVector.X = RotVector.Y = 0;
		OutPoint.Transform.SetRotation(OutPoint.Transform.GetRotation() * FQuat::MakeFromRotationVector(RotVector));
	}
	
	if (!InParams.bProjectScales)
	{
		OutPoint.Transform.SetScale3D(InTransform.GetScale3D());
	}

	return true;
}

UPCGSpatialData* UPCGWaterData::CopyInternal() const
{
	UPCGWaterData* NewWaterData = NewObject<UPCGWaterData>();

	CopyBaseSurfaceData(NewWaterData);

	NewWaterData->WaterBodies = WaterBodies;
	NewWaterData->Bounds = Bounds;
	NewWaterData->bHeightOnly = bHeightOnly;
	NewWaterData->bUseMetadata = bUseMetadata;

	return NewWaterData;
}

const UPCGPointData* UPCGWaterData::CreatePointData(FPCGContext* Context, const FBox& InBounds) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGWaterData::CreatePointData);

	UPCGPointData* Data = NewObject<UPCGPointData>();
	Data->InitializeFromData(this);
	TArray<FPCGPoint>& Points = Data->GetMutablePoints();

	FBox EffectiveBounds = Bounds;
	if (InBounds.IsValid)
	{
		EffectiveBounds = Bounds.Overlap(InBounds);
	}

	// Early out
	if (!EffectiveBounds.IsValid)
	{
		return Data;
	}

	UPCGMetadata* OutMetadata = bUseMetadata ? Data->Metadata : nullptr;


	return Data;
}
