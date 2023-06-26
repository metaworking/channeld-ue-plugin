#pragma once
#include "PropertyDecorator/VectorPropertyDecorator.h"

class FBoolPropertyDecorator : public FPropertyDecorator
{
public:
	FBoolPropertyDecorator(FProperty* InProperty, IPropertyDecoratorOwner* InOwner)
		: FPropertyDecorator(InProperty, InOwner)
	{
		ProtoFieldType = L"bool";
	}
	virtual FString GetCPPType() override { return L"bool"; }
	virtual FString GetPropertyType() override { return L"FBoolProperty"; }
	virtual FString GetCode_SetPropertyValueTo(const FString& TargetInstance, const FString& NewStateName, const FString& AfterSetValueCode) override;
	virtual FString GetCode_GetPropertyValueFrom(const FString& TargetInstance, bool ForceFromPointer = false) override;
};
