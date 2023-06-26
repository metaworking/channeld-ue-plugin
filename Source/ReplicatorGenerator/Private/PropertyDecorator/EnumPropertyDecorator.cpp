#include "PropertyDecorator/EnumPropertyDecorator.h"

FString FEnumPropertyDecorator::GetPropertyType()
{
	if (OriginalProperty->IsA<FByteProperty>())
	{
		return "FByteProperty";
	}
	return "FEnumProperty";
}

FString FEnumPropertyDecorator::GetCode_SetProtoFieldValueTo(const FString& StateName, const FString& GetValueCode)
{
	return FString::Printf(TEXT("%s->set_%s((int32)(%s))"), *StateName, *GetProtoFieldName(), *GetValueCode);
}

FString FEnumPropertyDecorator::GetCode_SetPropertyValueTo(const FString& TargetInstance, const FString& NewStateName,
                                                           const FString& AfterSetValueCode)
{
	const FString ValueStr = FPropertyDecorator::GetCode_GetPropertyValueFrom(TargetInstance, !IsDirectlyAccessible());
	const FString FieldValueStr = GetCode_GetProtoFieldValueFrom(NewStateName);

	return FString::Printf(TEXT("%s = %s;\nbStateChanged = true;\n%s"), *ValueStr, *FieldValueStr, *AfterSetValueCode);
}

FString FEnumPropertyDecorator::GetCode_GetPropertyValueFrom(const FString& TargetInstance,
                                                             bool ForceFromPointer)
{
	FString EnumName = OriginalProperty->GetCPPType();

	if (OriginalProperty->IsA<FByteProperty>())
	{
		auto ByteProperty = CastField<FByteProperty>(OriginalProperty);
		EnumName = ByteProperty->Enum->CppType;
	}
	if (!ForceFromPointer)
	{
		return FString::Printf(TEXT("(%s)(%s->%s)"), *EnumName, *TargetInstance, *GetPropertyName());
	}

	return FString::Printf(TEXT("(%s)(*%s)"), *EnumName, *GetPointerName());
}

FString FEnumPropertyDecorator::GetCode_GetProtoFieldValueFrom(const FString& StateName)
{
	FString EnumName = OriginalProperty->GetCPPType();

	if (OriginalProperty->IsA<FByteProperty>())
	{
		auto ByteProperty = CastField<FByteProperty>(OriginalProperty);
		EnumName = ByteProperty->Enum->CppType;
	}

	return FString::Printf(TEXT("(%s)(%s->%s())"), *EnumName, *StateName, *GetProtoFieldName());
}
