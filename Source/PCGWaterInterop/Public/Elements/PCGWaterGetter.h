// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Elements/PCGDataFromActor.h"
#include "UObject/Object.h"

#include "PCGWaterGetter.generated.h"

/** Builds a collection of water data from the selected actors. */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGWATERINTEROP_API UPCGGetWaterSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetWaterSettings();

	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GetWaterData")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGGetWaterSettings", "NodeTitle", "Get Water Data"); }
	virtual FText GetNodeTooltipText() const override;
#endif

	virtual FName AdditionalTaskName() const override;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	//~Begin UPCGDataFromActorSettings interface
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::Surface; }
	virtual TSubclassOf<AActor> GetDefaultActorSelectorClass() const override;
	//~End UPCGDataFromActorSettings
};

// @note: this is largely copied from FPCGDataFromActorElement, which isn't exported (as of UE 5.3)
class FPCGGetWaterDataElement : public IPCGElement
{
public:
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node) override;
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
	
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	void GatherWaitTasks(AActor* FoundActor, FPCGContext* Context, TArray<FPCGTaskId>& OutWaitTasks) const;
	virtual void ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, const TArray<AActor*>& FoundActors) const;
	virtual void ProcessActor(FPCGContext* InContext, const UPCGDataFromActorSettings* Settings, AActor* FoundActor) const;

	void MergeActorsIntoPointData(FPCGContext* InContext, const UPCGDataFromActorSettings* InSettings, const TArray<AActor*>& FoundActors) const;
};
