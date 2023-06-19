#pragma once
#include "PropertyDecorator.h"
const static TCHAR* ArrPropDeco_SetDeltaStateTemplate =
	LR"EOF(
{
  bool bPropChanged = false;
  const int32 ActorPropLength = {Declare_PropPtrName}->Num();
  const int32 FullStateValueLength = {Code_ConditionFullStateIsNull}{Code_GetProtoFieldValueFrom}.size();
  if (ActorPropLength != FullStateValueLength)
  {
    bPropChanged = true;
  }
  for (int32 i = 0; i < ActorPropLength; ++i)
  {
  {Code_SetDeltaStateArrayInner}
  }
  if (bPropChanged)
  {
    bStateChanged = true;
    {Declare_DeltaStateName}->set_update_{Definition_ProtoName}(true);
    if({Declare_FullStateName} != nullptr && {Declare_FullStateName}->{Definition_ProtoName}_size() > 0)
    {
      const_cast<{Declare_ProtoNamespace}::{Declare_ProtoStateMsgName}*>({Declare_FullStateName})->clear_{Definition_ProtoName}();
    }
  }
  else
  {
    {Declare_DeltaStateName}->clear_{Definition_ProtoName}();
  }
}
)EOF";

const static TCHAR* ArrPropDeco_SetDeltaStateByMemOffsetTemp =
	LR"EOF(
{
  bool bPropChanged = false;
  {Code_AssignPropPointers};
  const int32 ActorPropLength = PropAddr->Num();
  const int32 FullStateValueLength = {Code_ConditionFullStateIsNull}{Code_GetProtoFieldValueFrom}.size();
  if (ActorPropLength != FullStateValueLength)
  {
    bPropChanged = true;
  }
  for (int32 i = 0; i < ActorPropLength; ++i)
  {
  {Code_SetDeltaStateArrayInner}
  }
  if(bPropChanged)
  {
    bStateChanged = true;
	{Declare_DeltaStateName}->set_update_{Definition_ProtoName}(true);
    if({Declare_FullStateName} != nullptr && {Declare_FullStateName}->{Definition_ProtoName}_size() > 0)
    {
      const_cast<{Declare_ProtoNamespace}::{Declare_ProtoStateMsgName}*>({Declare_FullStateName})->clear_{Definition_ProtoName}();
    }
  }
  else if(!ForceMarge)
  {
    {Declare_DeltaStateName}->clear_{Definition_ProtoName}();
  }
}
)EOF";

const static TCHAR* ArrPropDeco_OnChangeStateTemp =
	LR"EOF(
{
  bool bPropChanged = false;
  if ({Code_HasProtoFieldValue})
  {
    {Code_SetPropertyValue}
  }
  else if({Code_GetProtoUpdateValue}) 
  {
    bPropChanged = {Declare_PropPtrName}->Num() != {Code_GetProtoFieldValueFrom}.size();
    {Declare_PropPtrName}->Empty();
  }
  if(bPropChanged)
  {
    bStateChanged = true;
    {Code_CallRepNotify}
  }
}
)EOF";

const static TCHAR* ArrPropDeco_SetPropertyValueTemp =
	LR"EOF(
const int32 ActorPropLength = {Declare_PropPtrName}->Num();
auto & MessageArr = {Code_GetProtoFieldValueFrom};
const int32 NewStateValueLength = MessageArr.size();
if (ActorPropLength != NewStateValueLength)
{ 
  bPropChanged = true; 
}
{Declare_PropPtrName}->SetNum(NewStateValueLength);
for (int32 i = 0; i < NewStateValueLength; ++i)
{
{Code_SetPropertyValueArrayInner}
}
)EOF";

const static TCHAR* ArrPropDeco_SetPropertyValueByMemOffsetTemp =
	LR"EOF(
{
  bool bPropChanged = false;
  {Code_AssignPropPointers};
  const int32 ActorPropLength = {Declare_PropPtrName}->Num();
  auto & MessageArr = {Code_GetProtoFieldValueFrom};
  const int32 NewStateValueLength = MessageArr.size();
  if (ActorPropLength != NewStateValueLength)
  { 
    bPropChanged = true;
  }
  {Declare_PropPtrName}->SetNum(NewStateValueLength);
  for (int32 i = 0; i < NewStateValueLength; ++i)
  {
  {Code_SetPropertyValueArrayInner}
  }
  if (bPropChanged)
  {
    bStateChanged = true;
  }
}
)EOF";


class FArrayPropertyDecorator : public FPropertyDecorator
{
public:
	FArrayPropertyDecorator(FProperty* InProperty, IPropertyDecoratorOwner* InOwner);

	virtual ~FArrayPropertyDecorator() override = default;
	
	virtual void PostInit() override;

	virtual FString GetPropertyName() override;

	virtual FString GetCPPType() override;
	
	virtual FString GetPropertyType() override;
	
	virtual FString GetCode_ActorPropEqualToProtoState(const FString& FromActor, const FString& FromState) override;
	virtual FString GetCode_ActorPropEqualToProtoState(const FString& FromActor, const FString& FromState, bool ForceFromPointer) override;
	
	virtual FString GetCode_SetDeltaState(const FString& TargetInstance, const FString& FullStateName, const FString& DeltaStateName,  bool ConditionFullStateIsNull) override;
	virtual FString GetCode_SetDeltaStateByMemOffset(const FString& ContainerName, const FString& FullStateName, const FString& DeltaStateName, bool ConditionFullStateIsNull) override;

	virtual FString GetCode_HasProtoFieldValueIn(const FString& StateName) override;

	virtual FString GetCode_OnStateChange(const FString& TargetInstanceName, const FString& NewStateName, bool NeedCallRepNotify = false) override;
	virtual FString GetCode_SetPropertyValueTo(const FString& TargetInstance, const FString& NewStateName, const FString& AfterSetValueCode) override;
	virtual FString GetCode_OnStateChangeByMemOffset(const FString& ContainerName, const FString& NewStateName) override;

	virtual TArray<FString> GetAdditionalIncludes() override;

	virtual FString GetCode_GetWorldRef() override;

	virtual TArray<TSharedPtr<FStructPropertyDecorator>> GetStructPropertyDecorators() override;

	virtual FString GetDefinition_ProtoField(int32& FieldNumber) override;
protected:
	TSharedPtr<FPropertyDecorator> InnerProperty;
};
