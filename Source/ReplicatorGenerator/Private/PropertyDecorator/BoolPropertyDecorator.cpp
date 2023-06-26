#include "PropertyDecorator/BoolPropertyDecorator.h"

#include "ChanneldUtils.h"
static void DoDetermineBitfieldOffsetAndMask(uint32& Offset, uint8& BitMask, std::function<void (void*)> SetBit,
									  const SIZE_T SizeOf)
{
	TUniquePtr<uint8[]> Buffer = MakeUnique<uint8[]>(SizeOf);

	SetBit(Buffer.Get());

	// Here we are making the assumption that bitfields are aligned in the struct. Probably true.
	// If not, it may be ok unless we are on a page boundary or something, but the check will fire in that case.
	// Have faith.
	for (uint32 TestOffset = 0; TestOffset < SizeOf; TestOffset++)
	{
		if (uint8 Mask = Buffer[TestOffset])
		{
			Offset = TestOffset;
			BitMask = Mask;
			check(FMath::RoundUpToPowerOfTwo(BitMask) == BitMask); // better be only one bit on
			break;
		}
	}
};

FString FBoolPropertyDecorator::GetCode_GetPropertyValueFrom(const FString& TargetInstance, bool ForceFromPointer/* = false */)
{
	if (!ForceFromPointer)
	{
		return FString::Printf(TEXT("%s->%s"), *TargetInstance, *GetPropertyName());
	}
	// BitBool Property
	FBoolProperty* BoolProperty = CastField<FBoolProperty>(OriginalProperty);
	if (BoolProperty && !BoolProperty->IsNativeBool())
	{
		uint32 Offset = 0;
		uint8 BitMask = 0;
		int OuterSize = this->OriginalProperty->GetOwnerProperty()->GetSize();
		DoDetermineBitfieldOffsetAndMask(Offset, BitMask,
										 [BoolProperty](void* Obj) { BoolProperty->SetPropertyValue(Obj, true); },
										 OuterSize);
		uint8 FieldMask = BitMask;
		uint8 ByteOffset = Offset;

		return FString::Printf(TEXT("!!(*((uint8*)(%s) + %d) & %d)"), *GetPointerName(),
							   ByteOffset, FieldMask);
	}
	return TEXT("*") + GetPointerName();
}

FString FBoolPropertyDecorator::GetCode_SetPropertyValueTo(const FString& TargetInstance, const FString& NewStateName, const FString& AfterSetValueCode)
{
	const FString ValueStr = GetCode_GetPropertyValueFrom(TargetInstance);
	const FString FieldValueStr = GetCode_GetProtoFieldValueFrom(NewStateName);

	FBoolProperty* BoolProperty = CastField<FBoolProperty>(OriginalProperty);
	if (this->IsDirectlyAccessible())
	{
		return FString::Printf(
			TEXT("%s = %s;\nbStateChanged = true;\n%s"), *ValueStr, *FieldValueStr, *AfterSetValueCode);
	}
	if (BoolProperty && !BoolProperty->IsNativeBool())
	{

		uint32 Offset = 0;
		uint8 BitMask = 0;
		DoDetermineBitfieldOffsetAndMask(Offset, BitMask,
										 [BoolProperty](void* Obj) { BoolProperty->SetPropertyValue(Obj, true); },
										 this->OriginalProperty->GetOwnerProperty()->GetSize());

		uint8 FieldMask = BitMask;
		uint8 ByteOffset = Offset;
		return FString::Printf(
			TEXT(
				"uint8* ByteValue = (uint8*)(%s) + %d;\n*ByteValue = ((*ByteValue) & ~%d) | (%s ? %d : 0); bStateChanged = true; \n%s"),
			*GetPointerName(),
			ByteOffset,
			BitMask, *FieldValueStr,
			FieldMask, *AfterSetValueCode);
	}
	
	return FString::Printf(TEXT("*%s = %s;\nbStateChanged = true;\n%s"), *GetPointerName(), *FieldValueStr, *AfterSetValueCode);
}