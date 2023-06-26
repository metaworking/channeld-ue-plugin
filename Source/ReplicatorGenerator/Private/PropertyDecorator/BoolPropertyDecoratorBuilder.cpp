#include "PropertyDecorator/BoolPropertyDecoratorBuilder.h"

#include "PropertyDecorator/BoolPropertyDecorator.h"

bool FBoolPropertyDecoratorBuilder::IsSpecialProperty(FProperty* Property)
{
	if (Property->IsA<FBoolProperty>())
	{
		return true;
	}
	return false;
}

FPropertyDecorator* FBoolPropertyDecoratorBuilder::ConstructPropertyDecorator(FProperty* Property, IPropertyDecoratorOwner* InOwner)
{
	return new FBoolPropertyDecorator(Property, InOwner);
}
