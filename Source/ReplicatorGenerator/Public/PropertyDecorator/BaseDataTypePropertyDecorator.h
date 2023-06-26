#pragma once
#include "ArrayPropertyDecorator.h"
#include "PropertyDecorator.h"
#include "StringPropertyDecorator.h"
#include "TextPropertyDecorator.h"
#include "NamePropertyDecorator.h"
#include "StructPropertyDecorator.h"
#include "UnrealObjectPropertyDecorator.h"

#define BASE_DATA_TYPE_PROPERTY_DECORATOR_BASE(ClassName, PropertyType, CPPType, ProtoType) \
class ClassName : public FPropertyDecorator \
{ \
public: \
	ClassName(FProperty* InProperty, IPropertyDecoratorOwner* InOwner) : FPropertyDecorator(InProperty, InOwner) \
	{ \
		ProtoFieldType = TEXT(#ProtoType); \
	} \
	virtual FString GetCPPType() override \
	{ \
		return TEXT(#CPPType); \
	} \
	virtual FString GetPropertyType() override \
	{ \
		return TEXT(#PropertyType); \
	} \
}

#define BASE_DATA_TYPE_PROPERTY_DECORATOR_BUILDER(ClassName, PropertyDecorator, PropertyType) \
class ClassName: public FPropertyDecoratorBuilder \
{ \
public: \
virtual ~ClassName() override {}; \
virtual bool IsSpecialProperty(FProperty* Property) override \
{ \
return Property->IsA<PropertyType>(); \
} \
virtual FPropertyDecorator* ConstructPropertyDecorator(FProperty* Property, IPropertyDecoratorOwner* InOwner) override \
{ \
return new PropertyDecorator(Property, InOwner); \
} \
}

#define BASE_DATA_TYPE_PROPERTY_DECORATOR(ClassName, PropertyType, CPPType) \
	BASE_DATA_TYPE_PROPERTY_DECORATOR_BASE(ClassName, PropertyType, CPPType, CPPType)

#define BASE_DATA_TYPE_PROPERTY_DECORATOR_WITH_BUILDER_BASE(BuilderClassName, DecoratorClassName, PropertyType, CPPType, ProtoType) \
	BASE_DATA_TYPE_PROPERTY_DECORATOR_BASE(DecoratorClassName, PropertyType, CPPType, ProtoType); \
	BASE_DATA_TYPE_PROPERTY_DECORATOR_BUILDER(BuilderClassName, DecoratorClassName, PropertyType)

#define BASE_DATA_TYPE_PROPERTY_DECORATOR_WITH_BUILDER(BuilderClassName, DecoratorClassName, PropertyType, CPPType) \
	BASE_DATA_TYPE_PROPERTY_DECORATOR_WITH_BUILDER_BASE(BuilderClassName, DecoratorClassName, PropertyType, CPPType, CPPType)

BASE_DATA_TYPE_PROPERTY_DECORATOR_WITH_BUILDER_BASE(FBytePropertyDecoratorBuilder, FBytePropertyDecorator, FByteProperty, uint8, uint32);

BASE_DATA_TYPE_PROPERTY_DECORATOR_WITH_BUILDER(FUInt32PropertyDecoratorBuilder, FUInt32PropertyDecorator, FUInt32Property, uint32);

BASE_DATA_TYPE_PROPERTY_DECORATOR_WITH_BUILDER(FIntPropertyDecoratorBuilder, FIntPropertyDecorator, FIntProperty, int32);

BASE_DATA_TYPE_PROPERTY_DECORATOR_WITH_BUILDER(FUInt64PropertyDecoratorBuilder, FUInt64PropertyDecorator, FUInt64Property, uint64);

BASE_DATA_TYPE_PROPERTY_DECORATOR_WITH_BUILDER(FInt64PropertyDecoratorBuilder, FInt64PropertyDecorator, FInt64Property, int64);

BASE_DATA_TYPE_PROPERTY_DECORATOR_WITH_BUILDER(FFloatPropertyDecoratorBuilder, FFloatPropertyDecorator, FFloatProperty, float);

BASE_DATA_TYPE_PROPERTY_DECORATOR_WITH_BUILDER(FDoublePropertyDecoratorBuilder, FDoublePropertyDecorator, FDoubleProperty, double);

BASE_DATA_TYPE_PROPERTY_DECORATOR_BUILDER(FArrayPropertyDecoratorBuilder, FArrayPropertyDecorator, FArrayProperty);

BASE_DATA_TYPE_PROPERTY_DECORATOR_BUILDER(FStructPropertyDecoratorBuilder, FStructPropertyDecorator, FStructProperty);

BASE_DATA_TYPE_PROPERTY_DECORATOR_BUILDER(FStrPropertyDecoratorBuilder, FStringPropertyDecorator, FStrProperty);
BASE_DATA_TYPE_PROPERTY_DECORATOR_BUILDER(FTextPropertyDecoratorBuilder, FTextPropertyDecorator, FTextProperty);
BASE_DATA_TYPE_PROPERTY_DECORATOR_BUILDER(FNamePropertyDecoratorBuilder, FNamePropertyDecorator, FNameProperty);

BASE_DATA_TYPE_PROPERTY_DECORATOR_BUILDER(FUObjPropertyDecoratorBuilder, FUnrealObjectPropertyDecorator, FObjectProperty);
