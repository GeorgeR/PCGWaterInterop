// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Data/PCGSurfaceData.h"

#include "PCGWaterData.generated.h"

class UPCGWaterCache;
class AWaterBody;
/**
* Water data access abstraction for PCG. Supports multi-waterbody access, but it assumes that they are not overlapping.
*/
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGWATERINTEROP_API UPCGWaterData : public UPCGSurfaceData
{
	GENERATED_BODY()

public:
	void Initialize(const TArray<TWeakObjectPtr<AWaterBody>>& InWaterBodies, const FBox& InBounds, bool bInUseMetadata);

	// ~Begin UPCGData interface
	virtual EPCGDataType GetDataType() const override { return EPCGDataType::Surface; }
	// ~End UPCGData interface
	
	// ~Begin UPGCSpatialData interface
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual bool SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	virtual bool ProjectPoint(const FTransform& InTransform, const FBox& InBounds, const FPCGProjectionParams& InParams, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	virtual bool HasNonTrivialTransform() const override { return true; }
	
protected:
	virtual UPCGSpatialData* CopyInternal() const override;
	//~End UPCGSpatialData interface

public:
	// ~Begin UPCGSpatialDataWithPointCache interface
	virtual bool SupportsBoundedPointData() const { return true; }
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context) const override { return CreatePointData(Context, FBox(EForceInit::ForceInit)); }
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context, const FBox& InBounds) const override;
	// ~End UPCGSpatialDataWithPointCache interface

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = SourceData)
	TArray<TSoftObjectPtr<AWaterBody>> WaterBodies;

	bool IsUsingMetadata() const { return bUseMetadata; }

protected:
	UPROPERTY()
	FBox Bounds = FBox(EForceInit::ForceInit);

	UPROPERTY()
	bool bHeightOnly = false;

	UPROPERTY()
	bool bUseMetadata = true;
};
