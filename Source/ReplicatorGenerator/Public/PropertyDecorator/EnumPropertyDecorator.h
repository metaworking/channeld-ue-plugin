#pragma once
#include "PropertyDecorator.h"

class FEnumPropertyDecorator : public FPropertyDecorator
{
public:
	FEnumPropertyDecorator(FProperty* InProperty, IPropertyDecoratorOwner* InOwner)
		: FPropertyDecorator(InProperty, InOwner)
	{
		ProtoFieldType = TEXT("int32");
		FString EnumName = InProperty->GetCPPType();

		if (InProperty->IsA<FByteProperty>())
		{
			auto ByteProperty = CastField<FByteProperty>(OriginalProperty);
			EnumName = ByteProperty->Enum->CppType;
		}
		if(EnumName.StartsWith("EGameplayTargetingConfirmation") || EnumName.StartsWith("EGameplayAbilityTargetingLocationType::Type") || EnumName.StartsWith("EAbilityGenericReplicatedEvent"))
		{
			AddGlobalIncludeFile("Abilities/GameplayAbilityTargetTypes.h");
		}
		if(EnumName.StartsWith("EGameplayAbilityActivationMode") || EnumName.StartsWith("EGameplayEffectGrantedAbilityRemovePolicy"))
		{
			AddGlobalIncludeFile("GameplayEffectTypes.h");
		}
		if(EnumName.StartsWith("EGameplayModEvaluationChannel") || EnumName.StartsWith("EGameplayModOp") || EnumName.StartsWith("EGameplayEffectAttributeCaptureSource") || EnumName.StartsWith("EGameplayEffectStackingType")
			||EnumName.StartsWith("EGameplayCueEvent") || EnumName.StartsWith("EGameplayTagEventType"))
		{
			AddGlobalIncludeFile("GameplayAbilitySpec.h");
		}
	}
	virtual FString GetPropertyType() override;
	virtual FString GetCode_SetProtoFieldValueTo(const FString& StateName, const FString& GetValueCode) override;
	virtual FString GetCode_SetPropertyValueTo(const FString& TargetInstance, const FString& NewStateName, const FString& AfterSetValueCode) override;
	virtual FString GetCode_GetPropertyValueFrom(const FString& TargetInstance) override
	{
		return GetCode_GetPropertyValueFrom(TargetInstance, !IsDirectlyAccessible());
	}
	virtual FString GetCode_GetPropertyValueFrom(const FString& TargetInstance, bool ForceFromPointer) override;
	virtual FString GetCode_GetProtoFieldValueFrom(const FString& StateName) override;
};
