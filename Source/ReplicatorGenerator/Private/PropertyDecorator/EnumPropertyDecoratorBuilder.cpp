#include "PropertyDecorator/EnumPropertyDecoratorBuilder.h"
#include "PropertyDecorator/EnumPropertyDecorator.h"

bool FEnumPropertyDecoratorBuilder::IsSpecialProperty(FProperty* Property)
{
	if(Property->IsA<FNumericProperty>())
	{
		auto NumericProperty = CastField<FNumericProperty>(Property);
		if(NumericProperty->IsEnum())
		{
			return true;
		}
	}

	if (Property->IsA<FEnumProperty>())
	{
		return true;
	}
	return false;
}

FPropertyDecorator* FEnumPropertyDecoratorBuilder::ConstructPropertyDecorator(FProperty* Property, IPropertyDecoratorOwner* InOwner)
{
	return new FEnumPropertyDecorator(Property, InOwner);
}
