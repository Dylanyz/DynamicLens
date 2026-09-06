#include "DynamicLensLibrary.h"

#include "CineCameraActor.h"
#include "DynamicLensComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

UDynamicLensComponent* UDynamicLensLibrary::AddToActor(AActor* Actor, UDynamicLensPreset* Preset)
{
	if (!Actor) return nullptr;
	UDynamicLensComponent* Comp = Actor->FindComponentByClass<UDynamicLensComponent>();
	if (!Comp)
	{
		Actor->Modify();
		Comp = NewObject<UDynamicLensComponent>(Actor, UDynamicLensComponent::StaticClass(), TEXT("DynamicLens"), RF_Transactional);
		Actor->AddInstanceComponent(Comp);
		Comp->RegisterComponent();
	}
	if (Preset)
	{
		Comp->Modify();
		Comp->Preset = Preset;
	}
	return Comp;
}

int32 UDynamicLensLibrary::AddToAllCineCameras(UObject* WorldContextObject, UDynamicLensPreset* Preset, bool bOverwriteExisting)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World) return 0;
	int32 Count = 0;
	for (TActorIterator<ACineCameraActor> It(World); It; ++It)
	{
		ACineCameraActor* Cam = *It;
		if (!Cam || Cam->IsTemplate()) continue;
		UDynamicLensComponent* Existing = Cam->FindComponentByClass<UDynamicLensComponent>();
		if (Existing && !bOverwriteExisting) continue;
		AddToActor(Cam, Preset);
		++Count;
	}
	return Count;
}

int32 UDynamicLensLibrary::RemoveFromAllCineCameras(UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World) return 0;
	int32 Count = 0;
	for (TActorIterator<ACineCameraActor> It(World); It; ++It)
	{
		TArray<UDynamicLensComponent*> Comps;
		It->GetComponents<UDynamicLensComponent>(Comps);
		for (UDynamicLensComponent* C : Comps)
		{
			It->Modify();
			C->ClearEffect();
			C->DestroyComponent();
			++Count;
		}
	}
	return Count;
}

bool UDynamicLensLibrary::FillProfile(UDynamicLensProfile* Profile, const FString& Label, const FString& Source, const TArray<float>& FocusCm, const TArray<float>& FocalsMm, const TArray<float>& FlatParams)
{
	if (!Profile || FocusCm.Num() == 0 || FocalsMm.Num() == 0) return false;
	if (FlatParams.Num() != FocalsMm.Num() * FocusCm.Num() * 5) return false;
	Profile->Modify();
	Profile->Label = Label;
	Profile->Source = Source;
	Profile->FocusCm = FocusCm;
	Profile->Rows.Reset(FocalsMm.Num());
	int32 I = 0;
	for (float Focal : FocalsMm)
	{
		FDynamicLensProfileRow Row;
		Row.FocalMm = Focal;
		Row.ByFocus.Reserve(FocusCm.Num());
		for (int32 D = 0; D < FocusCm.Num(); ++D)
		{
			FDynamicLensParams P;
			P.K1 = FlatParams[I++]; P.K2 = FlatParams[I++]; P.K3 = FlatParams[I++]; P.P1 = FlatParams[I++]; P.P2 = FlatParams[I++];
			Row.ByFocus.Add(P);
		}
		Profile->Rows.Add(MoveTemp(Row));
	}
	return Profile->IsValidProfile();
}
