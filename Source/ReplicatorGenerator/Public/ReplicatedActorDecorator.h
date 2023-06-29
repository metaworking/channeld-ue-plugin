#pragma once
#include "IPropertyDecoratorOwner.h"
#include "PropertyDecorator.h"
#include "RPCDecorator.h"
#include "Manifest.h"

static const TCHAR* ActorComp_GetNetGUIDTemplate =
	LR"EOF(
uint32 {Declare_ReplicatorClassName}::GetNetGUID()
{
  if (!NetGUID.IsValid())
  {
    if ({Ref_TargetInstanceRef}.IsValid())
    {
      UWorld* World = {Ref_TargetInstanceRef}->GetWorld();
      if (World && World->GetNetDriver())
      {
        NetGUID = World->GetNetDriver()->GuidCache->GetNetGUID({Ref_TargetInstanceRef}->GetOwner());
      }
    }
  }
  return NetGUID.Value;
}
)EOF";

static const TCHAR* GameState_GetNetGUIDTemplate =
	LR"EOF(
uint32 {Declare_ReplicatorClassName}::GetNetGUID()
{
  return Channeld::GameStateNetId;
}
)EOF";

static const TCHAR* ActorDecor_GetStateFromChannelData =
	LR"EOF(
if({Code_Condition}) {
  auto States = {Declaration_ChannelDataMessage}->mutable_{Definition_ChannelDataFieldName}();
  if (States->contains(NetGUID))
  {
    bIsRemoved = false;
    return &States->at(NetGUID);
  }
}
)EOF";

static const TCHAR* ActorDecor_GetStateFromChannelData_Singleton =
	LR"EOF(
if({Code_Condition}) {
  bIsRemoved = false;
  return {Declaration_ChannelDataMessage}->has_{Definition_ChannelDataFieldName}() ? &{Declaration_ChannelDataMessage}->{Definition_ChannelDataFieldName}() : nullptr;
}
)EOF";

static const TCHAR* ActorDecor_GetStateFromChannelData_Removable =
	LR"EOF(
if({Code_Condition}) {
  auto States = {Declaration_ChannelDataMessage}->mutable_{Definition_ChannelDataFieldName}();
  if (States->contains(NetGUID))
  {
    auto State = &States->at(NetGUID);
    bIsRemoved = State->removed();
    return State;
  }
}
)EOF";

static const TCHAR* ActorDecor_ChannelDataProcessorMergeLoop =
	LR"EOF(
for (auto& Pair : Src->{Definition_ChannelDataFieldName}())
{
{Code_MergeLoopInner}
}
)EOF";

static const TCHAR* ActorDecor_ChannelDataProcessorMerge_Erase =
	LR"EOF(
if (Pair.second.removed())
{
{Code_MergeEraseInner}
}
else
{
{Code_DoMerge}
}
)EOF";
static const TCHAR* ActorDecor_GetStateFromChannelData_AC_Singleton =
	LR"EOF(
if({Code_Condition}) {
  auto States = {Declaration_ChannelDataMessage}->mutable_{Definition_ChannelDataFieldName}();
  const std::string name = TCHAR_TO_UTF8(*TargetObject->GetName());
  if (States->states().contains(name))
  {
	const auto State = &States->states().at(name);
	bIsRemoved = State->removed();
	return State;	
  }
}
)EOF";
static const TCHAR* ActorDecor_GetStateFromChannelData_AC =
	LR"EOF(
if({Code_Condition}) {
  auto States = {Declaration_ChannelDataMessage}->mutable_{Definition_ChannelDataFieldName}();
  if (States->contains(NetGUID))
  {
	const std::string name = TCHAR_TO_UTF8(*TargetObject->GetName());
	const auto NameStates = &States->at(NetGUID);
	if (NameStates->states().contains(name))
	{
		const auto State = &NameStates->states().at(name);
		bIsRemoved = State->removed();
		return State;	
	}
  }
}
)EOF";
static const TCHAR* ActorDecor_ChannelDataProcessorMerge_DoMarge =
	LR"EOF(
if (Dst->{Definition_ChannelDataFieldName}().contains(Pair.first))
{
  Dst->mutable_{Definition_ChannelDataFieldName}()->at(Pair.first).MergeFrom(Pair.second);
}
else
{
  Dst->mutable_{Definition_ChannelDataFieldName}()->emplace(Pair.first, Pair.second);
}
)EOF";

static const TCHAR* ActorDecor_ChannelDataProcessorMerge_Singleton =
	LR"EOF(
if (Src->has_{Definition_ChannelDataFieldName}())
{
  Dst->mutable_{Definition_ChannelDataFieldName}()->MergeFrom(Src->{Definition_ChannelDataFieldName}());
}
)EOF";

static const TCHAR* ActorDecor_SetStateToChannelData =
	LR"EOF(
if({Code_Condition}) {
  if (State)
  {
    auto States = {Declaration_ChannelDataMessage}->mutable_{Definition_ChannelDataFieldName}();
    (*States)[NetGUID] = *static_cast<const {Definition_ProtoNamespace}::{Definition_ProtoStateMsgName}*>(State);
  }
}
)EOF";

static const TCHAR* ActorDecor_SetStateToChannelData_Singleton =
	LR"EOF(
if({Code_Condition}) {
  {Declaration_ChannelDataMessage}->mutable_{Definition_ChannelDataFieldName}()->MergeFrom(*static_cast<const {Definition_ProtoNamespace}::{Definition_ProtoStateMsgName}*>(State));
}
)EOF";

static const TCHAR* ActorDecor_SetStateToChannelData_AC_Singleton =
	LR"EOF(
if({Code_Condition}) {
  auto AccessibleState = State != nullptr ? State : Removed{Definition_ProtoStateMsgName}.Get();
  if (AccessibleState)
  {
	auto States = {Declaration_ChannelDataMessage}->mutable_actorcomponentstates();
	const unrealpb::ActorComponentState state = *static_cast<const unrealpb::ActorComponentState*>(AccessibleState);
	const std::string compname = TCHAR_TO_UTF8(*TargetObject->GetName());
	States->mutable_states()->emplace(compname, state);
  }
}
)EOF";

static const TCHAR* ActorDecor_SetStateToChannelData_AC =
	LR"EOF(
if({Code_Condition}) {
  auto AccessibleState = State != nullptr ? State : Removed{Definition_ProtoStateMsgName}.Get();
  if (AccessibleState)
  {
	auto States = {Declaration_ChannelDataMessage}->mutable_actorcomponentstatess();
	const unrealpb::ActorComponentState state = *static_cast<const unrealpb::ActorComponentState*>(AccessibleState);
	const std::string compname = TCHAR_TO_UTF8(*TargetObject->GetName());
	(*States)[NetGUID].mutable_states()->emplace(compname, state);
  }
}
)EOF";
static const TCHAR* ActorDecor_SetStateToChannelData_Removable =
	LR"EOF(
if({Code_Condition}) {
  auto AccessibleState = State != nullptr ? State : Removed{Definition_ProtoStateMsgName}.Get();
  if (AccessibleState)
  {
  	auto States = {Declaration_ChannelDataMessage}->mutable_{Definition_ChannelDataFieldName}();
    (*States)[NetGUID] = *static_cast<const {Definition_ProtoNamespace}::{Definition_ProtoStateMsgName}*>(AccessibleState);
  }
}
)EOF";

class FReplicatedActorDecorator : public IPropertyDecoratorOwner
{
public:
	FReplicatedActorDecorator(
		const UClass* TargetActorClass
		, const TFunction<void(FString& TargetActorName, bool IsActorNameCompilable)>& SetCompilableName
		, const FString& ProtoPackageName
		, const FString& ProtoStateMessageTypeSuffix
		, const FString& GoPackageName
		, bool IsSingletonInChannelData = false
		, bool IsSkipGenChannelDataState = false
	);

	virtual ~FReplicatedActorDecorator() = default;

	TArray<TSharedPtr<FPropertyDecorator>>& GetPropertyDecorators() { return Properties; }

	/**
	 * Create property decorators and RPC decorators
	 */
	void InitPropertiesAndRPCs();

	/**
	 * Is target actor class a blueprint class
	 */
	virtual bool IsBlueprintType() override;

	/**
	 * Some classes are only one instance in the whole server cluster, such as GameState.
	 * Normally, the singleton instance is owned by the master server.
	 */
	virtual bool IsSingletonInChannelData();

	/**
	 * Is the replicator of the target class has been implemented by ChanneldUE,
	 * and the state message is contained in the unreal_common.proto.
	 *
	 * TODO: The list of builtin types should be maintained by ChanneldUE module.
	 */
	virtual bool IsChanneldUEBuiltinType();

	virtual bool IsSkipGenChannelDataState();

	/**
	 * Set module info if the target actor class is a cpp class.
	 * Please call this function before calling GetActorHeaderIncludePath().
	 */
	void SetModuleInfo(const FModuleInfo& InModuleBelongTo);

	FORCEINLINE const UClass* GetTargetClass() const { return TargetClass; }

	/**
      * Get target actor name
      */
	FString GetActorName();

	/**
	 * Get target actor asset path
	 */
	FString GetActorPathName();

	/**
      * Get target origin actor name
      */
	FString GetOriginActorName();

	/**
      * Get target outer package path name
      */
	FString GetPackagePathName();

	/**
	 * Get target actor cpp class name
	 */
	FString GetActorCPPClassName();

	virtual UFunction* FindFunctionByName(const FName& FuncName) override;

	/**
	 * Get code of include target actor header
	 */
	FString GetIncludeActorHeaderPath();

	/**
     * Get code of additional include files 
     */
	FString GetAdditionalIncludeFiles();

	virtual TArray<FString> GetAdditionalIncludes() override
	{
		return TArray<FString>();
	}

	/**
	 * Get class name of generated replicator
	 * FChanneld[TargetActorName]Replicator
	 */
	FString GetReplicatorClassName(bool WithPrefix = true);

	/**
	 * Get all declaration of externally inaccessible property pointer
	 */
	FString GetCode_IndirectlyAccessiblePropertyPtrDeclarations();

	/**
	 * Get all code of assign property pointer
	 */
	FString GetCode_AssignPropertyPointers();

	/**
	 * Get protobuf package name
	 */
	virtual FString GetProtoPackageName() override;

	/**
	 * Get protobuf package path for generated Go code.
	 * If the state's package name equals the channel data's package name, return an empty string.
	 */
	FString GetProtoPackagePathGo(const FString& ChannelDataPackageName);

	/**
	 * Get protobuf c++ code namespace
	 */
	virtual FString GetProtoNamespace() override;

	/**
	 * Get protobuf definitions base file name (without extension)
	 */
	virtual FString GetProtoDefinitionsBaseFileName();

	/**
	 * Get protobuf definitions file name
	 */
	virtual FString GetProtoDefinitionsFileName();

	/**
	 * Get go package name. Used for go proto code generation
	 */
	virtual FString GetGoPackageImportPath();

	/**
	 * Get the suffix of the state message type
	 */
	virtual FString GetProtoStateMessageTypeSuffix();

	/**
	 * Get message type for replicated actor properties mapping
	 */
	virtual FString GetProtoStateMessageType() override;

	virtual FString GetProtoStateMessageTypeGo();

	/**
	 * Get code that sets whole delta state
	 */
	FString GetCode_AllPropertiesSetDeltaState(const FString& FullStateName, const FString& DeltaStateName);

	/**
	 * Get code that handles state changed
	 */
	FString GetCode_AllPropertiesOnStateChange(const FString& NewStateName);

	/**
	 * Get protobuf message definition
	 *
	 * For example:
	 *   message CharacterState {
	 *     optional bool bIsCrouched = 1;
     *   }
     *   
	 */
	FString GetDefinition_ProtoStateMessage();

	int32 GetRPCNum();

	FString GetCode_SerializeFunctionParams();
	FString GetCode_DeserializeFunctionParams();

	FString GetInstanceRefName() const;
	void SetInstanceRefName(const FString& InstanceRefName);

	virtual FString GetCode_GetWorldRef() override;

	virtual FString GetCode_OverrideGetNetGUID();

	virtual void SetConstClassPathFNameVarName(const FString& VarName);

	virtual FString GetDefinition_ChannelDataFieldNameProto();
	virtual FString GetDefinition_ChannelDataFieldNameCpp();
	virtual FString GetDefinition_ChannelDataFieldNameGo();

	virtual FString GetCode_ConstPathFNameVarDecl();

	virtual FString GetCode_ChannelDataProcessor_IsTargetClass();

	virtual FString GetDeclaration_ChanneldDataProcessor_RemovedStata();

	virtual FString GetCode_ChanneldDataProcessor_InitRemovedState();

	virtual FString GetCode_ChannelDataProcessor_Merge(const TArray<TSharedPtr<FReplicatedActorDecorator>>& ActorChildren);

	virtual FString GetCode_ChannelDataProcessor_GetStateFromChannelData(const FString& ChannelDataMessageName);

	virtual FString GetCode_ChannelDataProcessor_SetStateToChannelData(const FString& ChannelDataMessageName);

	virtual FString GetCode_ChannelDataProtoFieldDefinition(const int32& FieldNum);

	virtual bool IsStruct() override;

	virtual TArray<TSharedPtr<FStructPropertyDecorator>> GetStructPropertyDecorators() override;

protected:
	const UClass* TargetClass;
	FString TargetActorCompilableName;
	FString InstanceRefName;
	FString ProtoPackageName;
	FString ProtoStateMessageTypeSuffix;
	FString GoPackageImportPath;
	FModuleInfo ModuleBelongTo;
	TArray<TSharedPtr<FPropertyDecorator>> Properties;
	TArray<TSharedPtr<FRPCDecorator>> RPCs;

	bool bSingletonInChannelData;
	bool bChanneldUEBuiltinType;
	bool bSkipGenChannelDataState;

	bool bBlueprintGenerated;
	FString ReplicatorClassName;

	FString VariableName_ConstClassPathFName;
};
