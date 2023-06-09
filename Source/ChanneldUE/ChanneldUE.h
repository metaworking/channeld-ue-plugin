#pragma once

#include "CoreMinimal.h"
#include "DefaultSpatialChannelDataProcessor.h"
#include "Modules/ModuleManager.h"

class FChanneldUEModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDefaultSpatialChannelDataProcessor* SpatialChannelDataProcessor;
};
