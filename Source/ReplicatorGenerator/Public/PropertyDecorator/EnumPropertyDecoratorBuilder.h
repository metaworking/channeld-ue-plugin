#pragma once
#include "PropertyDecoratorBuilder.h"

class FEnumPropertyDecoratorBuilder : public FPropertyDecoratorBuilder
{
public:
	virtual bool IsSpecialProperty(FProperty*) override;

protected:
	virtual FPropertyDecorator* ConstructPropertyDecorator(FProperty*, IPropertyDecoratorOwner*) override;
	FString GetCode_GetProtoFieldValueFrom(const FString& StateName);
};
