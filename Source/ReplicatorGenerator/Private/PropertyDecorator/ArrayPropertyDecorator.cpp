#include "PropertyDecorator/ArrayPropertyDecorator.h"
#include "PropertyDecorator/StructPropertyDecorator.h"
#include "PropertyDecoratorFactory.h"

FArrayPropertyDecorator::FArrayPropertyDecorator(FProperty* InProperty, IPropertyDecoratorOwner* InOwner)
	: FPropertyDecorator(InProperty, InOwner)
{
	check(InProperty != nullptr)
	SetForceNotDirectlyAccessible(true);
	FPropertyDecoratorFactory& Factory = FPropertyDecoratorFactory::Get();
	InnerProperty = Factory.GetPropertyDecorator(CastFieldChecked<FArrayProperty>(InProperty)->Inner, this);
	ProtoFieldRule = TEXT("repeated");
	ProtoFieldType = InnerProperty->GetProtoFieldType();
}

void FArrayPropertyDecorator::PostInit()
{
	InnerProperty->Init(
		[this]()
		{
			return CompilablePropName + TEXT("_InnerProp");
		}
	);
}

FString FArrayPropertyDecorator::GetPropertyName()
{
	return InnerProperty->GetPropertyName();
}

FString FArrayPropertyDecorator::GetCPPType()
{
	return FString::Printf(TEXT("TArray<%s>"), *InnerProperty->GetCompilableCPPType());
}

FString FArrayPropertyDecorator::GetPropertyType()
{
	return TEXT("FArrayProperty");
}

FString FArrayPropertyDecorator::GetCode_ActorPropEqualToProtoState(const FString& FromActor, const FString& FromState)
{
	return TEXT("false");
}

FString FArrayPropertyDecorator::GetCode_ActorPropEqualToProtoState(const FString& FromActor, const FString& FromState, bool ForceFromPointer)
{
	return TEXT("false");
}

FString FArrayPropertyDecorator::GetCode_SetDeltaState(const FString& TargetInstance, const FString& FullStateName, const FString& DeltaStateName, bool ConditionFullStateIsNull)
{
	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(TEXT("Declare_PropPtrName"), GetPointerName());
	FormatArgs.Add(TEXT("Definition_ProtoName"), GetProtoFieldName());
	FormatArgs.Add(TEXT("Declare_FullStateName"), FullStateName);
	FormatArgs.Add(TEXT("Declare_DeltaStateName"), DeltaStateName);
	FormatArgs.Add(TEXT("Declare_ProtoNamespace"), Owner->GetProtoNamespace());
	FormatArgs.Add(TEXT("Declare_ProtoStateMsgName"), Owner->GetProtoStateMessageType());
	FormatArgs.Add(TEXT("Definition_ProtoName"), GetProtoFieldName());

	FormatArgs.Add(TEXT("Code_GetProtoFieldValueFrom"), GetCode_GetProtoFieldValueFrom(TEXT("FullState")));
	FormatArgs.Add(TEXT("Code_ConditionFullStateIsNull"), ConditionFullStateIsNull ? TEXT("bIsFullStateNull ? 0 : ") : TEXT(""));
	FormatArgs.Add(
		TEXT("Code_SetDeltaStateArrayInner"),
		InnerProperty->GetCode_SetDeltaStateArrayInner(GetPointerName(), FullStateName, DeltaStateName, ConditionFullStateIsNull)
	);
	return FString::Format(ArrPropDeco_SetDeltaStateTemplate, FormatArgs);
}

FString FArrayPropertyDecorator::GetCode_SetDeltaStateByMemOffset(const FString& ContainerName, const FString& FullStateName, const FString& DeltaStateName, bool ConditionFullStateIsNull)
{
	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(
		TEXT("Code_AssignPropPointers"),
		GetCode_AssignPropPointer(
			ContainerName,
			FString::Printf(TEXT("%s* PropAddr"), *GetCPPType())
		)
	);
	FormatArgs.Add(TEXT("Declare_FullStateName"), FullStateName);
	FormatArgs.Add(TEXT("Declare_DeltaStateName"), DeltaStateName);
	FormatArgs.Add(TEXT("Declare_ProtoNamespace"), Owner->GetProtoNamespace());
	FormatArgs.Add(TEXT("Declare_ProtoStateMsgName"), Owner->GetProtoStateMessageType());
	FormatArgs.Add(TEXT("Definition_ProtoName"), GetProtoFieldName());

	FormatArgs.Add(TEXT("Code_BeforeCondition"), ConditionFullStateIsNull ? TEXT("bIsFullStateNull ? true :") : TEXT(""));
	FormatArgs.Add(TEXT("Code_GetProtoFieldValueFrom"), GetCode_GetProtoFieldValueFrom(TEXT("FullState")));
	FormatArgs.Add(TEXT("Code_ConditionFullStateIsNull"), ConditionFullStateIsNull ? TEXT("bIsFullStateNull ? 0 : ") : TEXT(""));
	FormatArgs.Add(
		TEXT("Code_SetDeltaStateArrayInner"),
		InnerProperty->GetCode_SetDeltaStateArrayInner(TEXT("PropAddr"), FullStateName, DeltaStateName, ConditionFullStateIsNull)
	);
	return FString::Format(ArrPropDeco_SetDeltaStateByMemOffsetTemp, FormatArgs);
}

FString FArrayPropertyDecorator::GetCode_HasProtoFieldValueIn(const FString& StateName)
{
	return FString::Printf(TEXT("%s.size() > 0"), *GetCode_GetProtoFieldValueFrom(StateName));
}

FString FArrayPropertyDecorator::GetCode_OnStateChange(const FString& TargetInstanceName, const FString& NewStateName, bool NeedCallRepNotify)
{
	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(TEXT("Code_HasProtoFieldValue"), GetCode_HasProtoFieldValueIn(NewStateName));
	FormatArgs.Add(
		TEXT("Code_SetPropertyValue"),
		GetCode_SetPropertyValueTo(
			TargetInstanceName, NewStateName, TEXT("")
		)
	);
	FormatArgs.Add(TEXT("Declare_PropPtrName"), GetPointerName());
	FormatArgs.Add(TEXT("Code_GetProtoFieldValueFrom"), GetCode_GetProtoFieldValueFrom(NewStateName));
	FormatArgs.Add(
		TEXT("Code_CallRepNotify"),
		NeedCallRepNotify ? GetCode_CallRepNotify(TargetInstanceName) : TEXT("")
	);
	return FString::Format(ArrPropDeco_OnChangeStateTemp, FormatArgs);
}

FString FArrayPropertyDecorator::GetCode_SetPropertyValueTo(const FString& TargetInstance, const FString& NewStateName, const FString& AfterSetValueCode)
{
	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(TEXT("Declare_PropPtrName"), GetPointerName());
	FormatArgs.Add(TEXT("Code_GetProtoFieldValueFrom"), GetCode_GetProtoFieldValueFrom(NewStateName));
	FormatArgs.Add(TEXT("Code_SetPropertyValueArrayInner"), InnerProperty->GetCode_SetPropertyValueArrayInner(GetPointerName(), NewStateName));

	return FString::Format(ArrPropDeco_SetPropertyValueTemp, FormatArgs);
}

FString FArrayPropertyDecorator::GetCode_OnStateChangeByMemOffset(const FString& ContainerName, const FString& NewStateName)
{
	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(
		TEXT("Code_AssignPropPointers"),
		GetCode_AssignPropPointer(
			ContainerName,
			FString::Printf(TEXT("%s* PropAddr"), *GetCPPType())
		)
	);
	FormatArgs.Add(TEXT("Declare_PropPtrName"), TEXT("PropAddr"));
	FormatArgs.Add(TEXT("Code_GetProtoFieldValueFrom"), GetCode_GetProtoFieldValueFrom(NewStateName));
	FormatArgs.Add(TEXT("Code_SetPropertyValueArrayInner"), InnerProperty->GetCode_SetPropertyValueArrayInner(TEXT("PropAddr"), NewStateName));

	return FString::Format(ArrPropDeco_SetPropertyValueByMemOffsetTemp, FormatArgs);
}

TArray<FString> FArrayPropertyDecorator::GetAdditionalIncludes()
{
	return InnerProperty->GetAdditionalIncludes();
}

FString FArrayPropertyDecorator::GetCode_GetWorldRef()
{
	return Owner->GetCode_GetWorldRef();
}

TArray<TSharedPtr<FStructPropertyDecorator>> FArrayPropertyDecorator::GetStructPropertyDecorators()
{
	TArray<TSharedPtr<FStructPropertyDecorator>> StructPropertyDecorators;
	StructPropertyDecorators.Append(InnerProperty->GetStructPropertyDecorators());
	if (InnerProperty->IsStruct())
	{
		StructPropertyDecorators.Add(StaticCastSharedPtr<FStructPropertyDecorator>(InnerProperty));
	}
	TArray<TSharedPtr<FStructPropertyDecorator>> NonRepetitionStructPropertyDecorators;
	TSet<FString> StructPropertyDecoratorNames;
	for (TSharedPtr<FStructPropertyDecorator>& StructPropertyDecorator : StructPropertyDecorators)
	{
		if (!StructPropertyDecoratorNames.Contains(StructPropertyDecorator->GetPropertyName()))
		{
			StructPropertyDecoratorNames.Add(StructPropertyDecorator->GetPropertyName());
			NonRepetitionStructPropertyDecorators.Add(StructPropertyDecorator);
		}
	}
	return NonRepetitionStructPropertyDecorators;
}
