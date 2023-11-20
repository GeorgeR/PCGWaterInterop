// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"

class FPCGWaterInteropModule final
	: public IModuleInterface
{
public:
	//~ IModuleInterface implementation
	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}
	//~ End IModuleInterface implementation
};

IMPLEMENT_MODULE(FPCGWaterInteropModule, PCGWaterInterop)
