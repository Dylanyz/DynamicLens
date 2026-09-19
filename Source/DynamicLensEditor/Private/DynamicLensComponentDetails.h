// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

// Puts "Browse" in the Dynamic Lens component's Preset row, next to the asset picker.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class UDynamicLensComponent;

/**
 * Details panel for UDynamicLensComponent.
 *
 * Only one thing is customized: the Preset row grows a Browse button beside the picker, which
 * opens the Preset Browser already pointed at this camera. The button sits inside the row rather
 * than under it because a custom row added to a category always lands after every property in
 * that category - which would put it below Amount Multiplier, nowhere near the preset.
 */
class FDynamicLensComponentDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<UDynamicLensComponent> Target;
};
