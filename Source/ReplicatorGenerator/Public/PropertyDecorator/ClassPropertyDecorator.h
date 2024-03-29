﻿#pragma once
#include "PropertyDecorator.h"

const static TCHAR* ClassPropDeco_SetDeltaStateArrayInnerTemp =
	LR"EOF(
std::string PropItem = std::string(TCHAR_TO_UTF8(*(*{Declare_PropertyPtr})[i]->GetName()));
std::string* NewOne = {Declare_DeltaStateName}->add_{Definition_ProtoName}();
*NewOne = PropItem;
if (!bPropChanged)
{
  bPropChanged = !(PropItem == {Declare_FullStateName}->{Definition_ProtoName}()[i]);
}
)EOF";

const static TCHAR* ClassPropDeco_OnChangeStateArrayInnerTemp =
	LR"EOF(
FName NewName = FName(UTF8_TO_TCHAR(MessageArr[i].c_str()));
if ((*{Declare_PropertyPtr})[i]->GetName() != NewName)
{
  (*{Declare_PropertyPtr})[i] = LoadClass<{Declare_ClassName}>(NULL, *NewName);
  if (!b{Declare_PropertyName}Changed)
  {
    b{Declare_PropertyName}Changed = true;
  }
}
)EOF";

// For TSubclassOf<T>
class FClassPropertyDecorator : public FPropertyDecorator
{
public:
	FClassPropertyDecorator(FProperty* InProperty, IPropertyDecoratorOwner* InOwner)
	: FPropertyDecorator(InProperty, InOwner)
	{
		ProtoFieldType = TEXT("string");
		const FString CPPType = InProperty->GetCPPType();
		InnerClassName = CPPType.Mid(12, CPPType.Len() - 14);
	}
	
	virtual FString GetPropertyType() override;

	virtual FString GetCode_GetProtoFieldValueFrom(const FString& StateName) override;
	
	virtual FString GetCode_SetProtoFieldValueTo(const FString& StateName, const FString& GetValueCode) override;

	virtual FString GetCode_SetDeltaStateArrayInner(const FString& PropertyPointer, const FString& FullStateName, const FString& DeltaStateName, bool ConditionFullStateIsNull) override;

	virtual FString GetCode_SetPropertyValueArrayInner(const FString& ArrayPropertyName, const FString& PropertyPointer, const FString& NewStateName) override;

	virtual TArray<FString> GetAdditionalIncludes() override;
	
protected:

	// The name of T in TSubclassOf<T>
	FString InnerClassName;
};
