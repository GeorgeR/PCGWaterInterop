// Fill out your copyright notice in the Description page of Project Settings.

#include "Elements/PCGWaterGetter.h"

#include "Algo/AnyOf.h"
#include "Data/PCGPointData.h"
#include "Data/PCGWaterData.h"
#include "Elements/PCGMergeElement.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "PCG/Private/Grid/PCGPartitionActor.h"
#include "PCGComponent.h"
#include "PCGSubsystem.h"
#include "WaterBodyActor.h"

#define LOCTEXT_NAMESPACE "PCGWaterGetterElements"

// @note: these are mostly copied from Engine\UE5\Plugins\Experimental\PCG\Source\PCG\Private\Elements\PCGActorSelector.cpp:14 - which is currently unexported (as of UE 5.3)
namespace UE::PCGWaterInterop::Private
{
	// Filter is required if it is not disabled and if we are gathering all world actors or gathering all children.
	bool FilterRequired(const FPCGActorSelectorSettings& InSettings)
	{
		return (InSettings.ActorFilter == EPCGActorFilter::AllWorldActors || InSettings.bIncludeChildren) && !InSettings.bDisableFilter;
	}
	
	// Need to pass a pointer of pointer to the found actor. The lambda will capture this pointer and modify its value when an actor is found.
	TFunction<bool(AActor*)> GetFilteringFunction(const FPCGActorSelectorSettings& InSettings, const TFunction<bool(const AActor*)>& BoundsCheck, const TFunction<bool(const AActor*)>& SelfIgnoreCheck, TArray<AActor*>& InFoundActors)
	{
		if (!FilterRequired(InSettings))
		{
			return [&InFoundActors, &BoundsCheck, &SelfIgnoreCheck](AActor* Actor) -> bool
			{
				if (BoundsCheck(Actor) && SelfIgnoreCheck(Actor))
				{
					InFoundActors.Add(Actor);
				}
				return true;
			};
		}

		const bool bMultiSelect = InSettings.bSelectMultiple;

		switch (InSettings.ActorSelection)
		{
		case EPCGActorSelection::ByTag:
			return[ActorSelectionTag = InSettings.ActorSelectionTag, &InFoundActors, bMultiSelect, &BoundsCheck, &SelfIgnoreCheck](AActor* Actor) -> bool
			{
				if (Actor && Actor->ActorHasTag(ActorSelectionTag) && BoundsCheck(Actor) && SelfIgnoreCheck(Actor))
				{
					InFoundActors.Add(Actor);
					return bMultiSelect;
				}

				return true;
			};

		case EPCGActorSelection::ByClass:
			return[ActorSelectionClass = InSettings.ActorSelectionClass, &InFoundActors, bMultiSelect, &BoundsCheck, &SelfIgnoreCheck](AActor* Actor) -> bool
			{
				if (Actor && Actor->IsA(ActorSelectionClass) && BoundsCheck(Actor) && SelfIgnoreCheck(Actor))
				{
					InFoundActors.Add(Actor);
					return bMultiSelect;
				}

				return true;
			};

		case EPCGActorSelection::ByName:
			UE_LOG(LogPCG, Error, TEXT("PCGActorSelector::GetFilteringFunction: Unsupported value for EPCGActorSelection - selection by name is no longer supported."));
			break;

		default:
			break;
		}

		return [](AActor* Actor) -> bool { return false; };
	}
	
	TArray<AActor*> FindActors(const FPCGActorSelectorSettings& Settings, const UPCGComponent* InComponent, const TFunction<bool(const AActor*)>& BoundsCheck, const TFunction<bool(const AActor*)>& SelfIgnoreCheck)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGActorSelector::FindActor);

		UWorld* World = InComponent ? InComponent->GetWorld() : nullptr;
		AActor* Self = InComponent ? InComponent->GetOwner() : nullptr;

		TArray<AActor*> FoundActors;

		if (!World)
		{
			return FoundActors;
		}

		// Early out if we have not the information necessary.
		const bool bNoTagInfo = Settings.ActorSelection == EPCGActorSelection::ByTag && Settings.ActorSelectionTag == NAME_None;
		const bool bNoClassInfo = Settings.ActorSelection == EPCGActorSelection::ByClass && !Settings.ActorSelectionClass;

		if (FilterRequired(Settings) && (bNoTagInfo || bNoClassInfo))
		{
			return FoundActors;
		}

		// We pass FoundActor ref, that will be captured by the FilteringFunction
		// It will modify the FoundActor pointer to the found actor, if found.
		TFunction<bool(AActor*)> FilteringFunction = GetFilteringFunction(Settings, BoundsCheck, SelfIgnoreCheck, FoundActors);

		// In case of iterating over all actors in the world, call our filtering function and get out.
		if (Settings.ActorFilter == EPCGActorFilter::AllWorldActors)
		{
			// A potential optimization if we know the sought actors are collide-able could be to obtain overlaps via a collision query.
			UPCGActorHelpers::ForEachActorInWorld<AActor>(World, FilteringFunction);

			// FoundActor is set by the FilteringFunction (captured)
			return FoundActors;
		}

		// Otherwise, gather all the actors we need to check
		TArray<AActor*> ActorsToCheck;
		switch (Settings.ActorFilter)
		{
		case EPCGActorFilter::Self:
			if (Self)
			{
				ActorsToCheck.Add(Self);
			}
			break;

		case EPCGActorFilter::Parent:
			if (Self)
			{
				if (AActor* Parent = Self->GetParentActor())
				{
					ActorsToCheck.Add(Parent);
				}
				else
				{
					// If there is no parent, set the owner as the parent.
					ActorsToCheck.Add(Self);
				}
			}
			break;

		case EPCGActorFilter::Root:
		{
			AActor* Current = Self;
			while (Current != nullptr)
			{
				AActor* Parent = Current->GetParentActor();
				if (Parent == nullptr)
				{
					ActorsToCheck.Add(Current);
					break;
				}
				Current = Parent;
			}

			break;
		}

		case EPCGActorFilter::Original:
		{
			APCGPartitionActor* PartitionActor = Cast<APCGPartitionActor>(Self);
			UPCGComponent* OriginalComponent = (PartitionActor && InComponent) ? PartitionActor->GetOriginalComponent(InComponent) : nullptr;
			AActor* OriginalActor = OriginalComponent ? OriginalComponent->GetOwner() : nullptr;
			if (OriginalActor)
			{
				ActorsToCheck.Add(OriginalActor);
			}
			else if (Self)
			{
				ActorsToCheck.Add(Self);
			}
		}

		default:
			break;
		}

		if (Settings.bIncludeChildren)
		{
			const int32 InitialCount = ActorsToCheck.Num();
			for (int32 i = 0; i < InitialCount; ++i)
			{
				ActorsToCheck[i]->GetAttachedActors(ActorsToCheck, /*bResetArray=*/ false, /*bRecursivelyIncludeAttachedActors=*/ true);
			}
		}

		for (AActor* Actor : ActorsToCheck)
		{
			// FoundActor is set by the FilteringFunction (captured)
			if (!FilteringFunction(Actor))
			{
				break;
			}
		}

		return FoundActors;
	}
}

UPCGGetWaterSettings::UPCGGetWaterSettings()
{
	bDisplayModeSettings = false;
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	ActorSelector.bShowActorFilter = false;
	ActorSelector.bIncludeChildren = false;
	ActorSelector.bShowActorSelectionClass = false;
	ActorSelector.bSelectMultiple = true;
	ActorSelector.bShowSelectMultiple = false;

	// We want to apply different defaults to newly placed nodes. We detect new object if they are not a default object/archetype
	// and/or they do not need load.
	if (PCGHelpers::IsNewObjectAndNotDefault(this))
	{
		ActorSelector.ActorFilter = EPCGActorFilter::AllWorldActors;
		ActorSelector.bMustOverlapSelf = true;
		ActorSelector.ActorSelection = EPCGActorSelection::ByClass;
		ActorSelector.ActorSelectionClass = AWaterBody::StaticClass();
	}
}

#if WITH_EDITOR
FText UPCGGetWaterSettings::GetNodeTooltipText() const
{
	return LOCTEXT("GetWaterTooltip", "Builds a collection of surfaces from the selected water body actors.");
}
#endif

FName UPCGGetWaterSettings::AdditionalTaskName() const
{
	// Do not use the version from data from actor otherwise we'll show the selected actor class, which serves no purpose
	return UPCGSettings::AdditionalTaskName();
}

TArray<FPCGPinProperties> UPCGGetWaterSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Surface, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/false);

	return PinProperties;
}

FPCGElementPtr UPCGGetWaterSettings::CreateElement() const
{
	return MakeShared<FPCGGetWaterDataElement>();
}

TSubclassOf<AActor> UPCGGetWaterSettings::GetDefaultActorSelectorClass() const
{
	return AWaterBody::StaticClass();
}

FPCGContext* FPCGGetWaterDataElement::Initialize(
	const FPCGDataCollection& InputData,
	TWeakObjectPtr<UPCGComponent> SourceComponent,
	const UPCGNode* Node)
{
	FPCGDataFromActorContext* Context = new FPCGDataFromActorContext();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;
	Context->Node = Node;

	return Context;
}

bool FPCGGetWaterDataElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDataFromActorElement::Execute);

	check(InContext);
	FPCGDataFromActorContext* Context = static_cast<FPCGDataFromActorContext*>(InContext);

	const UPCGDataFromActorSettings* Settings = Context->GetInputSettings<UPCGDataFromActorSettings>();
	check(Settings);

	if (!Context->bPerformedQuery)
	{
		TFunction<bool(const AActor*)> BoundsCheck = [](const AActor*) -> bool { return true; };
		const UPCGComponent* PCGComponent = Context->SourceComponent.IsValid() ? Context->SourceComponent.Get() : nullptr;
		const AActor* Self = PCGComponent ? PCGComponent->GetOwner() : nullptr;
		if (Self && Settings->ActorSelector.bMustOverlapSelf)
		{
			// Capture ActorBounds by value because it goes out of scope
			const FBox ActorBounds = PCGHelpers::GetActorBounds(Self);
			BoundsCheck = [Settings, ActorBounds, PCGComponent](const AActor* OtherActor) -> bool
			{
				const FBox OtherActorBounds = OtherActor ? PCGHelpers::GetGridBounds(OtherActor, PCGComponent) : FBox(EForceInit::ForceInit);
				return ActorBounds.Intersect(OtherActorBounds);
			};
		}

		TFunction<bool(const AActor*)> SelfIgnoreCheck = [](const AActor*) -> bool { return true; };
		if (Self && Settings->ActorSelector.bIgnoreSelfAndChildren)
		{
			SelfIgnoreCheck = [Self](const AActor* OtherActor) -> bool
			{
				// Check if OtherActor is a child of self
				const AActor* CurrentOtherActor = OtherActor;
				while (CurrentOtherActor)
				{
					if (CurrentOtherActor == Self)
					{
						return false;
					}

					CurrentOtherActor = CurrentOtherActor->GetParentActor();
				}

				// Check if Self is a child of OtherActor
				const AActor* CurrentSelfActor = Self;
				while (CurrentSelfActor)
				{
					if (CurrentSelfActor == OtherActor)
					{
						return false;
					}

					CurrentSelfActor = CurrentSelfActor->GetParentActor();
				}

				return true;
			};
		}

		Context->FoundActors = UE::PCGWaterInterop::Private::FindActors(Settings->ActorSelector, Context->SourceComponent.Get(), BoundsCheck, SelfIgnoreCheck);
		Context->bPerformedQuery = true;

		if (Context->FoundActors.IsEmpty())
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("NoActorFound", "No matching actor was found"));
			return true;
		}

		// If we're looking for PCG component data, we might have to wait for it.
		if (Settings->Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponent || Settings->Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponentOrParseComponents)
		{
			TArray<FPCGTaskId> WaitOnTaskIds;
			for (AActor* Actor : Context->FoundActors)
			{
				GatherWaitTasks(Actor, Context, WaitOnTaskIds);
			}

			if (!WaitOnTaskIds.IsEmpty())
			{
				UPCGSubsystem* Subsystem = Context->SourceComponent.IsValid() ? Context->SourceComponent->GetSubsystem() : nullptr;
				if (Subsystem)
				{
					// Add a trivial task after these generations that wakes up this task
					Context->bIsPaused = true;

					Subsystem->ScheduleGeneric([Context]()
					{
						// Wake up the current task
						Context->bIsPaused = false;
						return true;
					}, Context->SourceComponent.Get(), WaitOnTaskIds);

					return false;
				}
				else
				{
					PCGE_LOG(Error, GraphAndLog, LOCTEXT("UnableToWaitForGenerationTasks", "Unable to wait for end of generation tasks"));
				}
			}
		}
	}

	if (Context->bPerformedQuery)
	{
		ProcessActors(Context, Settings, Context->FoundActors);
	}

	return true;
}

void FPCGGetWaterDataElement::GatherWaitTasks(AActor* FoundActor, FPCGContext* Context, TArray<FPCGTaskId>& OutWaitTasks) const
{
	if (!FoundActor)
	{
		return;
	}

	// We will prevent gathering the current execution - this task cannot wait on itself
	AActor* ThisOwner = ((Context && Context->SourceComponent.IsValid()) ? Context->SourceComponent->GetOwner() : nullptr);

	TInlineComponentArray<UPCGComponent*, 1> PCGComponents;
	FoundActor->GetComponents(PCGComponents);

	for (UPCGComponent* Component : PCGComponents)
	{
		if (Component->IsGenerating() && Component->GetOwner() != ThisOwner)
		{
			OutWaitTasks.Add(Component->GetGenerationTaskId());
		}
	}
}

void FPCGGetWaterDataElement::ProcessActors(
	FPCGContext* InContext,
	const UPCGDataFromActorSettings* InSettings,
	const TArray<AActor*>& FoundActors) const
{
	check(InContext);
	check(InSettings);

	const UPCGGetWaterSettings* Settings = CastChecked<UPCGGetWaterSettings>(InSettings);

	TArray<TWeakObjectPtr<AWaterBody>> WaterBodies;
	FBox WaterBounds(EForceInit::ForceInit);
	TSet<FString> WaterTags;

	for (AActor* FoundActor : FoundActors)
	{
		if (!FoundActor || !IsValid(FoundActor))
		{
			continue;
		}

		AWaterBody* WaterBody = Cast<AWaterBody>(FoundActor);
		if (ensure(WaterBody))
		{
			WaterBodies.Add(WaterBody);
			WaterBounds += PCGHelpers::GetGridBounds(WaterBody, nullptr);

			for (FName Tag : WaterBody->Tags)
			{
				WaterTags.Add(Tag.ToString());
			}
		}
	}

	if (!WaterBodies.IsEmpty())
	{
		UPCGWaterData* WaterData = NewObject<UPCGWaterData>();
		WaterData->Initialize(WaterBodies, WaterBounds, true);
		
		FPCGTaggedData& TaggedData = InContext->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = WaterData;
		TaggedData.Tags = WaterTags;
	}
}

void FPCGGetWaterDataElement::ProcessActor(
	FPCGContext* InContext,
	const UPCGDataFromActorSettings* InSettings,
	AActor* FoundActor) const
{
	checkf(false, TEXT("This should never be called directly"));
}

void FPCGGetWaterDataElement::MergeActorsIntoPointData(
	FPCGContext* InContext,
	const UPCGDataFromActorSettings* InSettings,
	const TArray<AActor*>& FoundActors) const
{
	check(InContext);

	// At this point in time, the partition actors behave slightly differently, so if we are in the case where
	// we have one or more partition actors, we'll go through the normal process and do post-processing to merge the point data instead.
	const bool bContainsPartitionActors = Algo::AnyOf(FoundActors, [](const AActor* Actor) { return Cast<APCGPartitionActor>(Actor) != nullptr; });

	if (!bContainsPartitionActors)
	{
		UPCGPointData* Data = NewObject<UPCGPointData>();
		bool bHasData = false;

		for (AActor* Actor : FoundActors)
		{
			if (Actor)
			{
				Data->AddSinglePointFromActor(Actor);
				bHasData = true;
			}
		}

		if (bHasData)
		{
			FPCGTaggedData& TaggedData = InContext->OutputData.TaggedData.Emplace_GetRef();
			TaggedData.Data = Data;
		}
	}
	else // Stripped-down version of the normal code path with bParseActor = false
	{
		FPCGDataCollection DataToMerge;
		const bool bParseActor = false;

		for (AActor* Actor : FoundActors)
		{
			if (Actor)
			{
				FPCGDataCollection Collection = UPCGComponent::CreateActorPCGDataCollection(Actor, InContext->SourceComponent.Get(), EPCGDataType::Any, bParseActor);
				DataToMerge.TaggedData += Collection.TaggedData;
			}
		}

		// Perform point data-to-point data merge
		if (DataToMerge.TaggedData.Num() > 1)
		{
			UPCGMergeSettings* MergeSettings = NewObject<UPCGMergeSettings>();
			TSharedPtr<IPCGElement> MergeElement = MergeSettings->GetElement();
			FPCGContext* MergeContext = MergeElement->Initialize(DataToMerge, InContext->SourceComponent, nullptr);
			MergeContext->AsyncState.NumAvailableTasks = InContext->AsyncState.NumAvailableTasks;
			MergeContext->InputData.TaggedData.Emplace_GetRef().Data = MergeSettings;

			while (!MergeElement->Execute(MergeContext))
			{
			}

			InContext->OutputData = MergeContext->OutputData;
			delete MergeContext;
		}
		else if (DataToMerge.TaggedData.Num() == 1)
		{
			InContext->OutputData.TaggedData = DataToMerge.TaggedData;
		}
	}
}
