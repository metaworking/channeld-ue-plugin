#pragma once
#include "PropertyDecorator.h"

const static TCHAR* UObjPropDeco_SetDeltaStateArrayInnerTemp =
	LR"EOF(
UObject * & PropItem = (*{Declare_PropertyPtr})[i];
unrealpb::UnrealObjectRef* NewOne = {Declare_DeltaStateName}->add_{Definition_ProtoName}();
*NewOne = *ChanneldUtils::GetRefOfObject(PropItem);
if (!bPropChanged)
{
  bPropChanged = !(PropItem == ChanneldUtils::GetObjectByRef(&{Declare_FullStateName}->{Definition_ProtoName}()[i], {Code_GetWorldRef}));
}
)EOF";

const static TCHAR* UObjPropDeco_OnChangeStateArrayInnerTemp =
	LR"EOF(
UObject* NewObjRef = ChanneldUtils::GetObjectByRef(&MessageArr[i], {Code_GetWorldRef});
if ((*{Declare_PropertyPtr})[i] != NewObjRef)
{
  (*{Declare_PropertyPtr})[i] = NewObjRef;
  if (!bPropChanged)
  {
    bPropChanged = true;
  }
}
)EOF";

class FUnrealObjectPropertyDecorator : public FPropertyDecorator
{
public:
	FUnrealObjectPropertyDecorator(FProperty* InProperty, IPropertyDecoratorOwner* InOwner)
		: FPropertyDecorator(InProperty, InOwner)
	{
		bForceNotDirectlyAccessible = true;
	}
	virtual FString GetCPPType() override;

	virtual FString GetPropertyType() override;
	virtual FString GetProtoFieldType() override;
	virtual FString GetProtoPackageName() override;
	virtual FString GetProtoNamespace() override;
	virtual FString GetProtoStateMessageType() override;

	virtual FString GetCode_ActorPropEqualToProtoState(const FString& FromActor, const FString& FromState) override;
	virtual FString GetCode_ActorPropEqualToProtoState(const FString& FromActor, const FString& FromState, bool ForceFromPointer) override;

	virtual FString GetCode_GetProtoFieldValueFrom(const FString& StateName) override;

	virtual FString GetCode_SetProtoFieldValueTo(const FString& StateName, const FString& GetValueCode) override;

	virtual FString GetCode_SetPropertyValueTo(const FString& TargetInstance, const FString& NewStateName, const FString& AfterSetValueCode) override;
	virtual FString GetCode_SetDeltaStateArrayInner(const FString& PropertyPointer, const FString& FullStateName, const FString& DeltaStateName, bool ConditionFullStateIsNull) override;

	virtual FString GetCode_SetPropertyValueArrayInner(const FString& PropertyPointer, const FString& NewStateName) override;

	virtual TArray<FString> GetAdditionalIncludes() override;

	virtual FString GetCode_GetWorldRef() override;
};

