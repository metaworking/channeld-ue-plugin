#include "PropertyDecorator/StructPropertyDecorator.h"

#include "PropertyDecoratorFactory.h"
#include "ReplicatorGeneratorDefinition.h"
#include "ReplicatorTemplate/CppReplicatorTemplate.h"

FStructPropertyDecorator::FStructPropertyDecorator(FProperty* InProperty, IPropertyDecoratorOwner* InOwner) : FPropertyDecorator(InProperty, InOwner)
{
	SetForceNotDirectlyAccessible(true);
	if (OriginalProperty != nullptr)
	{
		FPropertyDecoratorFactory& PropertyDecoratorFactory = FPropertyDecoratorFactory::Get();
		const FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(OriginalProperty);
		const UScriptStruct* ScriptStruct = StructProperty->Struct;
		for (TFieldIterator<FProperty> SIt(ScriptStruct, EFieldIteratorFlags::ExcludeSuper); SIt; ++SIt)
		{
			TSharedPtr<FPropertyDecorator> PropertyDecoratorPtr = PropertyDecoratorFactory.GetPropertyDecorator(*SIt, this);
			if (PropertyDecoratorPtr.IsValid())
			{
				PropertyDecoratorPtr->SetForceNotDirectlyAccessible(true);
				Properties.Add(PropertyDecoratorPtr);
			}
		}
	}
}

void FStructPropertyDecorator::PostInit()
{
	int32 IllegalPropNameIndex = 0;
	for (TSharedPtr<FPropertyDecorator> PropertyDecorator : Properties)
	{
		PropertyDecorator->Init(
			[&IllegalPropNameIndex]()
			{
				return FString::Printf(TEXT("_IllegalNameProp_%d_"), ++IllegalPropNameIndex);
			}
		);
	}
}

FString FStructPropertyDecorator::GetCompilableCPPType()
{
	return FString::Printf(TEXT("FCopy_%s"), *GetCPPType());
}

FString FStructPropertyDecorator::GetProtoFieldType()
{
	return FString::Printf(TEXT("%s.%s"), *GetProtoPackageName(), *GetProtoStateMessageType());
}

FString FStructPropertyDecorator::GetProtoStateMessageType()
{
	return GetCPPType() + TEXT("State");
}

FString FStructPropertyDecorator::GetPropertyType()
{
	return TEXT("FStructProperty");
}

FString FStructPropertyDecorator::GetDeclaration_PropertyPtr()
{
	return FString::Printf(TEXT("%s %s"), *GetDeclaration_PropPtrGroupStructName(), *GetPointerName());
}

FString FStructPropertyDecorator::GetCode_AssignPropPointer(const FString& Container, const FString& AssignTo, int32 MemOffset)
{
	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(TEXT("Ref_AssignTo"), AssignTo);
	FormatArgs.Add(TEXT("Ref_ContainerAddr"), Container);
	FormatArgs.Add(TEXT("Declare_PropertyCPPType"), GetCPPType());
	FormatArgs.Add(TEXT("Num_PropMemOffset"), MemOffset);
	FormatArgs.Add(TEXT("Declare_PropPtrGroupStructName"), GetDeclaration_PropPtrGroupStructName());

	return FString::Format(StructPropDeco_AssignPropPtrTemp, FormatArgs);
}

TArray<FString> FStructPropertyDecorator::GetAdditionalIncludes()
{
	TSet<FString> IncludeFileSet{GenManager_GlobalStructHeaderFile, GenManager_GlobalStructProtoHeaderFile};
	for (auto PropDecorator : Properties)
	{
		IncludeFileSet.Append(PropDecorator->GetAdditionalIncludes());
	}
	return IncludeFileSet.Array();
}

FString FStructPropertyDecorator::GetCode_ActorPropEqualToProtoState(const FString& FromActor, const FString& FromState)
{
	return TEXT("false");
}

FString FStructPropertyDecorator::GetCode_ActorPropEqualToProtoState(const FString& FromActor, const FString& FromState, bool ForceFromPointer)
{
	return TEXT("false");
}

FString FStructPropertyDecorator::GetCode_SetDeltaState(const FString& TargetInstance, const FString& FullStateName, const FString& DeltaStateName, bool ConditionFullStateIsNull)
{
	return FString::Printf(
		TEXT("if (%s.Merge(%s&%s->%s(), %s->mutable_%s(), %s))\n{\n  bStateChanged = true;\n}\n"),
		*GetPointerName(),
		ConditionFullStateIsNull ? TEXT("bIsFullStateNull ? nullptr : ") : TEXT(""),
		*FullStateName, *GetProtoFieldName(), *DeltaStateName, *GetProtoFieldName(), *Owner->GetCode_GetWorldRef()
	);
}

FString FStructPropertyDecorator::GetCode_SetDeltaStateByMemOffset(const FString& ContainerName, const FString& FullStateName, const FString& DeltaStateName, bool ConditionFullStateIsNull)
{
	return FString::Printf(
		TEXT("{\nvoid* PropAddr = (uint8*)%s + %d; if (%s::Merge(PropAddr, %s&%s->%s(), %s->mutable_%s(), World, ForceMarge))\n{\n  bStateChanged = true;\n}\n}\n"),
		*ContainerName, GetMemOffset(),
		*GetDeclaration_PropPtrGroupStructName(),
		ConditionFullStateIsNull ? TEXT("bIsFullStateNull ? nullptr : ") : TEXT(""),
		*FullStateName, *GetProtoFieldName(), *DeltaStateName, *GetProtoFieldName()
	);
}

FString FStructPropertyDecorator::GetCode_SetDeltaStateArrayInner(const FString& PropertyPointer, const FString& FullStateName, const FString& DeltaStateName, bool ConditionFullStateIsNull)
{
	FStringFormatNamedArguments FormatArgs;
	// FormatArgs.Add(TEXT("Code_BeforeCondition"), ConditionFullStateIsNull ? TEXT("bIsFullStateNull ? true :") : TEXT(""));
	FormatArgs.Add(TEXT("Declare_PropertyPtr"), PropertyPointer);
	FormatArgs.Add(TEXT("Declare_DeltaStateName"), DeltaStateName);
	FormatArgs.Add(TEXT("Declare_FullStateName"), FullStateName);
	FormatArgs.Add(TEXT("Definition_ProtoName"), GetProtoFieldName());
	FormatArgs.Add(TEXT("Declare_PropPtrGroupStructName"), GetDeclaration_PropPtrGroupStructName());
	FormatArgs.Add(TEXT("Code_GetWorldRef"), Owner->GetCode_GetWorldRef());
	return FString::Format(StructPropDeco_SetDeltaStateArrayInnerTemp, FormatArgs);
}

FString FStructPropertyDecorator::GetCode_SetPropertyValueTo(const FString& TargetInstance, const FString& NewStateName, const FString& AfterSetValueCode)
{
	return FString::Printf(
		TEXT("if (%s.SetPropertyValue(&%s->%s(), %s))\n{\nbStateChanged = true;\n%s\n}\n"),
		*GetPointerName(), *NewStateName, *GetProtoFieldName(), *Owner->GetCode_GetWorldRef(),
		*AfterSetValueCode
	);
}

FString FStructPropertyDecorator::GetCode_OnStateChangeByMemOffset(const FString& ContainerName, const FString& NewStateName)
{
	return FString::Printf(
		TEXT("{\nvoid* PropAddr = (uint8*)%s + %d; if (%s::SetPropertyValue(PropAddr, &%s->%s(), %s))\n{\n  bStateChanged = true;\n}\n}\n"),
		*ContainerName, GetMemOffset(),
		*GetDeclaration_PropPtrGroupStructName(), *NewStateName, *GetProtoFieldName(), *GetCode_GetWorldRef()
	);
}

FString FStructPropertyDecorator::GetCode_SetPropertyValueArrayInner(const FString& PropertyPointer, const FString& NewStateName)
{
	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(TEXT("Declare_PropertyPtr"), PropertyPointer);
	FormatArgs.Add(TEXT("Declare_PropPtrGroupStructName"), GetDeclaration_PropPtrGroupStructName());
	FormatArgs.Add(TEXT("Code_GetWorldRef"), Owner->GetCode_GetWorldRef());
	return FString::Format(StructPropDeco_SetPropertyValueArrayInnerTemp, FormatArgs);
}

FString FStructPropertyDecorator::GetDeclaration_PropPtrGroupStruct()
{
	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(TEXT("Declare_PropPtrGroupStructName"), GetDeclaration_PropPtrGroupStructName());
	FString AssignPropertyPointerCodes;
	FString AssignPropPointersForRPCCodes;
	FString DeclarePropPtrCodes;
	FString SetDeltaStateCodes;
	FString StaticSetDeltaStateCodes;
	FString OnStateChangeCodes;
	FString StaticOnStateChangeCodes;
	FString StructCopyCode;
	int32 PrevPropMemEnd = 0;

	bool bFirstProperty = true;
	for (TSharedPtr<FPropertyDecorator> PropDecorator : Properties)
	{
		DeclarePropPtrCodes.Append(FString::Printf(TEXT("%s;\n"), *PropDecorator->GetDeclaration_PropertyPtr()));

		AssignPropertyPointerCodes.Append(
			FString::Printf(
				TEXT("{\n%s;\n}\n"),
				*PropDecorator->GetCode_AssignPropPointer(TEXT("Container"), PropDecorator->GetPointerName())
			)
		);

		if (PropDecorator->HasAnyPropertyFlags(CPF_OutParm))
		{
			AssignPropPointersForRPCCodes.Append(
				FString::Printf(
					TEXT("{\n%s;\nOutParams = OutParams->NextOutParm;\n}\n"),
					*PropDecorator->GetCode_AssignPropPointer(TEXT("OutParams->PropAddr"), PropDecorator->GetPointerName(), 0)
				)
			);
		}
		else
		{
			AssignPropPointersForRPCCodes.Append(
				FString::Printf(
					TEXT("{\n%s;\n}\n"),
					*PropDecorator->GetCode_AssignPropPointer(TEXT("Params"), PropDecorator->GetPointerName())
				)
			);
		}


		SetDeltaStateCodes.Append(
			PropDecorator->GetCode_SetDeltaState(
				TEXT("this"), TEXT("FullState"), TEXT("DeltaState"), true
			)
		);
		StaticSetDeltaStateCodes.Append(
			PropDecorator->GetCode_SetDeltaStateByMemOffset(
				TEXT("Container"), TEXT("FullState"), TEXT("DeltaState"), true
			)
		);

		OnStateChangeCodes.Append(
			PropDecorator->GetCode_OnStateChange(
				TEXT("this"), TEXT("NewState")
			)
		);

		StaticOnStateChangeCodes.Append(
			PropDecorator->GetCode_OnStateChangeByMemOffset(
				TEXT("Container"), TEXT("NewState")
			)
		);

		StructCopyCode.Append(
			FString::Printf(TEXT("%s %s;\n"), *PropDecorator->GetCompilableCPPType(), *PropDecorator->GetPropertyName())
		);
		if (bFirstProperty) { bFirstProperty = false; }
	}
	FormatArgs.Add(TEXT("Code_AssignPropPointers"), *AssignPropertyPointerCodes);
	FormatArgs.Add(TEXT("Code_AssignPropPointersForRPC"), *AssignPropPointersForRPCCodes);
	FormatArgs.Add(TEXT("Declare_PropertyPointers"), *DeclarePropPtrCodes);
	FormatArgs.Add(TEXT("Declare_ProtoNamespace"), GetProtoNamespace());
	FormatArgs.Add(TEXT("Declare_ProtoStateMsgName"), GetProtoStateMessageType());
	FormatArgs.Add(TEXT("Code_SetDeltaStates"), SetDeltaStateCodes);
	FormatArgs.Add(TEXT("Code_StaticSetDeltaStates"), StaticSetDeltaStateCodes);
	FormatArgs.Add(TEXT("Code_OnStateChange"), OnStateChangeCodes);
	FormatArgs.Add(TEXT("Code_StaticOnStateChange"), StaticOnStateChangeCodes);

	FormatArgs.Add(TEXT("Declare_PropCompilableStructName"), GetCompilableCPPType());
	FormatArgs.Add(TEXT("Code_StructCopyProperties"), StructCopyCode);

	return FString::Format(StructPropDeco_PropPtrGroupStructTemp, FormatArgs);
}

FString FStructPropertyDecorator::GetDeclaration_PropPtrGroupStructName()
{
	return FString::Printf(TEXT("F%sPropPtrGroup"), *GetCPPType());
}

FString FStructPropertyDecorator::GetDefinition_ProtoStateMessage()
{
	FString FieldDefinitionCodes;
	int32 i = 0;
	for (TSharedPtr<FPropertyDecorator> PropDecorator : Properties)
	{
		FieldDefinitionCodes.Append(
			FString::Printf(
				TEXT("%s %s %s = %d;\n"),
				*PropDecorator->GetProtoFieldRule(),
				*PropDecorator->GetProtoFieldType(),
				*PropDecorator->GetProtoFieldName(),
				++i
			)
		);
	}
	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(TEXT("Declare_StateMessageType"), GetProtoStateMessageType());
	FormatArgs.Add(TEXT("Declare_ProtoFields"), FieldDefinitionCodes);
	FormatArgs.Add(TEXT("Declare_SubProtoFields"), TEXT(""));

	return FString::Format(CodeGen_ProtoStateMessageTemplate, FormatArgs);
}

FString FStructPropertyDecorator::GetCode_GetWorldRef()
{
	return FString("World");
}

bool FStructPropertyDecorator::IsStruct()
{
	return true;
}

TArray<TSharedPtr<FStructPropertyDecorator>> FStructPropertyDecorator::GetStructPropertyDecorators()
{
	TArray<TSharedPtr<FStructPropertyDecorator>> StructPropertyDecorators;
	for (TSharedPtr<FPropertyDecorator>& Property : Properties)
	{
		StructPropertyDecorators.Append(Property->GetStructPropertyDecorators());
		if (Property->IsStruct())
		{
			StructPropertyDecorators.Add(StaticCastSharedPtr<FStructPropertyDecorator>(Property));
		}
	}
	TArray<TSharedPtr<FStructPropertyDecorator>> NonRepetitionStructPropertyDecorators;
	TSet<FString> StructPropertyDecoratorNames;
	for (TSharedPtr<FStructPropertyDecorator>& StructPropertyDecorator : StructPropertyDecorators)
	{
		if (!StructPropertyDecoratorNames.Contains(StructPropertyDecorator->GetCPPType()))
		{
			StructPropertyDecoratorNames.Add(StructPropertyDecorator->GetCPPType());
			NonRepetitionStructPropertyDecorators.Add(StructPropertyDecorator);
		}
	}
	return NonRepetitionStructPropertyDecorators;
}
