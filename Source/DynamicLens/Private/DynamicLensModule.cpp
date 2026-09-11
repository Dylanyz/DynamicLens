// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

// DynamicLens - focal/focus/f-stop driven lens character for CineCameras.
#include "Modules/ModuleManager.h"
#include "CameraCalibrationSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UnrealType.h"

static TAutoConsoleVariable<int32> CVarDynamicLensDisplacementMapResolution(
	TEXT("DynamicLens.DisplacementMapResolution"), 2048,
	TEXT("Resolution the Camera Calibration plugin renders its distortion displacement maps at. Epic's default (256) is too coarse for ST maps and shows as soft, stepped edges. DynamicLens raises it to this value at startup if the project still uses the default. 0 = leave alone."));

class FDynamicLensModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FCoreDelegates::OnPostEngineInit.AddLambda([]()
		{
			const int32 Wanted = CVarDynamicLensDisplacementMapResolution.GetValueOnGameThread();
			UCameraCalibrationSettings* Settings = GetMutableDefault<UCameraCalibrationSettings>();
			if (!Settings || Wanted <= 0) return;
			const FIntPoint Current = Settings->GetDisplacementMapResolution();
			if (Current.X > 256 || Current.Y > 256) return;   // the project chose its own value
			if (FStructProperty* Prop = FindFProperty<FStructProperty>(UCameraCalibrationSettings::StaticClass(), TEXT("DisplacementMapResolution")))
			{
				const FIntPoint NewRes(Wanted, Wanted);
				Prop->SetValue_InContainer(Settings, &NewRes);
#if WITH_EDITOR
				Settings->OnDisplacementMapResolutionChanged().Broadcast(NewRes);   // lens files recreate their intermediate maps
#endif
			}
		});
	}
};

IMPLEMENT_MODULE(FDynamicLensModule, DynamicLens)
