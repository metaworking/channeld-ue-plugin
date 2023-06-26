#include "ReplicatorCodeGenerator.h"

#include "Manifest.h"
#include "PropertyDecoratorFactory.h"
#include "ReplicatorGeneratorDefinition.h"
#include "ReplicatorGeneratorUtils.h"
#include "GameFramework/PlayerState.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "ReplicatorTemplate/BlueprintReplicatorTemplate.h"
#include "ReplicatorTemplate/CppReplicatorTemplate.h"
#include "ReplicatorTemplate/GoProtoDataTemplate.h"
#include "PropertyDecorator.h"

bool FReplicatorCodeGenerator::RefreshModuleInfoByClassName()
{
	const FString BuildConfiguration = ANSI_TO_TCHAR(COMPILER_CONFIGURATION_NAME);
	const FString ManifestFilePath = FPaths::ProjectIntermediateDir() / TEXT("Build") / TEXT(CHANNELD_EXPAND_AND_QUOTE(UBT_COMPILED_PLATFORM)) / TEXT(CHANNELD_EXPAND_AND_QUOTE(UE_TARGET_NAME)) / BuildConfiguration / TEXT(CHANNELD_EXPAND_AND_QUOTE(UE_TARGET_NAME)) + TEXT(".uhtmanifest");

	bool bManifestSuccessfullyLoaded;
	FManifest Manifest = FManifest::LoadFromFile(ManifestFilePath, bManifestSuccessfullyLoaded);
	if (!bManifestSuccessfullyLoaded)
	{
		UE_LOG(LogChanneldRepGenerator, Error, TEXT("Failed to load manifest file: %s"), *ManifestFilePath);
		return false;
	}
	for (FManifestModule& ManifestModule : Manifest.Modules)
	{
		ProcessHeaderFiles(ManifestModule.PublicUObjectClassesHeaders, ManifestModule);
		ProcessHeaderFiles(ManifestModule.PublicUObjectHeaders, ManifestModule);
		ProcessHeaderFiles(ManifestModule.PrivateUObjectHeaders, ManifestModule);
	}

	return true;
}

FString FReplicatorCodeGenerator::GetClassHeadFilePath(const FString& ClassName)
{
	FCPPClassInfo* Result = CPPClassInfoMap.Find(ClassName);
	if (Result != nullptr)
	{
		return Result->HeadFilePath;
	}
	return TEXT("");
}

bool FReplicatorCodeGenerator::Generate(
	const TArray<FChannelDataInfo>& ChannelDataInfos,
	const FString& ProtoPackageName,
	const FString& ProtoMessageSuffix,
	const FString& GoPackageImportPath,
	FGeneratedCodeBundle& ReplicationCodeBundle
)
{
	TSet<const UClass*> ReplicationActorClassSet;
	TArray<const UClass*> ReplicationActorClasses;
	for (const FChannelDataInfo& ChannelDataInfo : ChannelDataInfos)
	{
		for (const FChannelDataInfo::FStateInfo& StateInfo : ChannelDataInfo.StateInfos)
		{
			if (!ReplicationActorClassSet.Contains(StateInfo.RepActorClass))
			{
				ReplicationActorClassSet.Add(StateInfo.RepActorClass);
				ReplicationActorClasses.Add(StateInfo.RepActorClass);
			}
		}
	}

	// Clean global variables, make sure it's empty for this generation
	TargetActorSameNameCounter.Empty();
	TargetClassSameNameNumber.Empty();

	FString Message;
	FString RegistrationIncludeCode;

	// Generate replicators code
	TArray<TSharedPtr<FReplicatedActorDecorator>> ActorDecoratorsToGenReplicator;
	FString RegisterReplicatorCode;
	for (const UClass* ReplicationActorClass : ReplicationActorClasses)
	{
		TSharedPtr<FReplicatedActorDecorator> ActorDecorator;
		if (!CreateDecorateActor(ActorDecorator, Message, ReplicationActorClass, FChannelDataStateSchema(), ProtoPackageName, ProtoMessageSuffix, GoPackageImportPath))
		{
			UE_LOG(LogChanneldRepGenerator, Error, TEXT("%s"), *Message);
			continue;
		}
		if (ActorDecorator->IsChanneldUEBuiltinType())
		{
			// Skip generate replicator for channeld ue builtin replication actors, they are written in ChanneldUE module.
			continue;
		}
		FReplicatorCode GeneratedResult;
		if (!GenerateReplicatorCode(ActorDecorator, GeneratedResult, Message))
		{
			UE_LOG(LogChanneldRepGenerator, Error, TEXT("%s"), *Message);
			continue;
		}
		ReplicationCodeBundle.ReplicatorCodes.Add(GeneratedResult);
		ActorDecoratorsToGenReplicator.Add(ActorDecorator);
		RegistrationIncludeCode.Append(GeneratedResult.IncludeActorCode + TEXT("\n"));
		RegistrationIncludeCode.Append(FString::Printf(TEXT("#include \"%s\"\n"), *GeneratedResult.HeadFileName));
		RegisterReplicatorCode.Append(GeneratedResult.RegisterReplicatorCode + TEXT("\n"));
	}

	// Channel data
	FString RegisterChannelDataProcessorCode, DeleteChannelDataProcessorCode, ChannelDataProcessorPtrDecls,
	        ChannelDataRegistrationGoCode;
	ReplicationCodeBundle.ChannelDataMerge_GoCode.Append(FString::Printf(TEXT("package %s\n"), *ProtoPackageName));
	// Entity channel data.go imports anypb
	bool bHasEntityChannelData = ChannelDataInfos.ContainsByPredicate([](const auto& Info) {return Info.Schema.ChannelType == EChanneldChannelType::ECT_Entity;});
	ReplicationCodeBundle.ChannelDataMerge_GoCode.Append(FString::Format(CodeGen_Go_Data_ImportTemplate,
		FStringFormatNamedArguments{{"Code_AnypbImport", bHasEntityChannelData	? TEXT("\"google.golang.org/protobuf/types/known/anypb\"") : TEXT("")}}));
	for (const FChannelDataInfo& ChannelDataInfo : ChannelDataInfos)
	{
		ReplicationCodeBundle.ChannelDataCodes.Add(FChannelDataCode());
		FChannelDataCode& ChannelDataCode = ReplicationCodeBundle.ChannelDataCodes.Last();
		if (!GenerateChannelDataCode(ChannelDataInfo, GoPackageImportPath, ProtoPackageName, ProtoMessageSuffix, ChannelDataCode, Message))
		{
			UE_LOG(LogChanneldRepGenerator, Error, TEXT("%s"), *Message);
		}
		RegistrationIncludeCode.Append(ChannelDataCode.IncludeProcessorCode + TEXT("\n"));
		RegisterChannelDataProcessorCode.Append(ChannelDataCode.RegisterProcessorCode + TEXT("\n"));
		DeleteChannelDataProcessorCode.Append(ChannelDataCode.DeleteProcessorPtrCode + TEXT("\n"));
		ChannelDataProcessorPtrDecls.Append(ChannelDataCode.ProcessorPtrDecl + TEXT("\n"));
		ChannelDataRegistrationGoCode.Append(ChannelDataCode.Registration_GoCode + TEXT("\n"));
		ReplicationCodeBundle.ChannelDataMerge_GoCode.Append(ChannelDataCode.Merge_GoCode + TEXT("\n"));
	}
	FStringFormatNamedArguments GoRegFormatArgs;
	GoRegFormatArgs.Add("Definition_GoImportPath", GoPackageImportPath);
	GoRegFormatArgs.Add("Definition_GenPackageName", ProtoPackageName);
	GoRegFormatArgs.Add("Code_Registration", ChannelDataRegistrationGoCode);
	ReplicationCodeBundle.ChannelDataRegistration_GoCode = FString::Format(CodeGen_Go_RegistrationTemplate, GoRegFormatArgs);

	// Registration codes
	{
		FStringFormatNamedArguments RegistrationFormatArgs;

		// Registration include
		RegistrationFormatArgs.Add(TEXT("Code_IncludeHeaders"), RegistrationIncludeCode);

		// Register replicators
		RegistrationFormatArgs.Add(TEXT("Code_ReplicatorRegister"), RegisterReplicatorCode);

		// Register channel data processor
		RegistrationFormatArgs.Add(TEXT("Code_ChannelDataProcessorRegister"), RegisterChannelDataProcessorCode);
		RegistrationFormatArgs.Add(TEXT("Code_DeleteChannelDataProcessor"), DeleteChannelDataProcessorCode);
		RegistrationFormatArgs.Add(TEXT("Declaration_Variables"), ChannelDataProcessorPtrDecls);
		ReplicationCodeBundle.ReplicatorRegistrationHeadCode = FString::Format(*CodeGen_RegistrationTemp, RegistrationFormatArgs);
	}

	// Type definitions
	ReplicationCodeBundle.TypeDefinitionsHeadCode = CodeGen_ChanneldGeneratedTypesHeadTemp;
	ReplicationCodeBundle.TypeDefinitionsCppCode = FString::Printf(TEXT("#include \"%s\"\nDEFINE_LOG_CATEGORY(LogChanneldGen);"), *GenManager_TypeDefinitionHeadFile);

	// Global struct codes
	auto GlobalStructDecorators = GetAllStructPropertyDecorators(ActorDecoratorsToGenReplicator);
	ReplicationCodeBundle.GlobalStructCodes.Append(TEXT("#pragma once\n"));
	for(auto HeaderFile : FPropertyDecorator::GetGlobalIncludeFile())
	{
		ReplicationCodeBundle.GlobalStructCodes.Append(FString::Printf(TEXT("#include \"%s\"\n"), *HeaderFile));
	}
	ReplicationCodeBundle.GlobalStructCodes.Append(TEXT("#include \"ChanneldUtils.h\"\n"));
	ReplicationCodeBundle.GlobalStructCodes.Append(FString::Printf(TEXT("#include \"%s\"\n"), *GenManager_GlobalStructProtoHeaderFile));
	for (auto StructDecorator : GlobalStructDecorators)
	{
		ReplicationCodeBundle.GlobalStructCodes.Append(StructDecorator->GetDeclaration_PropPtrGroupStruct());
		ReplicationCodeBundle.GlobalStructProtoDefinitions.Append(StructDecorator->GetDefinition_ProtoStateMessage());
	}

	FStringFormatNamedArguments ProtoFormatArgs;
	ProtoFormatArgs.Add(TEXT("Declare_ProtoPackageName"), ProtoPackageName);
	ProtoFormatArgs.Add(TEXT("Code_Import"), TEXT("import \"unreal_common.proto\";"));
	ProtoFormatArgs.Add(
		TEXT("Option"),
		GoPackageImportPath.IsEmpty() ? TEXT("") : FString::Printf(TEXT("option go_package = \"%s\";"), *GoPackageImportPath)
	);
	ProtoFormatArgs.Add(TEXT("Definition_ProtoStateMsg"), ReplicationCodeBundle.GlobalStructProtoDefinitions);
	ReplicationCodeBundle.GlobalStructProtoDefinitions = FString::Format(CodeGen_ProtoTemplate, ProtoFormatArgs);

	return true;
}

bool FReplicatorCodeGenerator::GenerateReplicatorCode(
	const TSharedPtr<FReplicatedActorDecorator>& ActorDecorator
	, FReplicatorCode& GeneratedResult
	, FString& ResultMessage
)
{
	GeneratedResult.ActorDecorator = ActorDecorator;
	const UClass* TargetActorClass = ActorDecorator->GetTargetClass();

	bool bIsBlueprint = ActorDecorator->IsBlueprintType();

	GeneratedResult.IncludeActorCode = bIsBlueprint ? TEXT("") : FString::Printf(TEXT("#include \"%s\"\n"), *ActorDecorator->GetIncludeActorHeaderPath());

	FString TargetInstanceRef = TEXT("Actor");
	FString TargetBaseClassName;
	FString OnStateChangedAdditionalCondition;
	FString TickAdditionalCondition;
	FString IsClientCode;
	if (TargetActorClass->IsChildOf(USceneComponent::StaticClass()))
	{
		if (bIsBlueprint)
		{
			TargetBaseClassName = TEXT("USceneComponent");
		}
		TargetInstanceRef = TEXT("SceneComp");
		OnStateChangedAdditionalCondition = FString::Printf(TEXT(" || !%s->GetOwner()"), *TargetInstanceRef);
		TickAdditionalCondition = FString::Printf(TEXT(" || !%s->GetOwner()"), *TargetInstanceRef);
		IsClientCode = FString::Printf(TEXT("%s->GetOwner()->HasAuthority()"), *TargetInstanceRef);
	}
	else if (TargetActorClass->IsChildOf(UActorComponent::StaticClass()))
	{
		if (bIsBlueprint)
		{
			TargetBaseClassName = TEXT("UActorComponent");
		}
		TargetInstanceRef = TEXT("ActorComp");
		OnStateChangedAdditionalCondition = FString::Printf(TEXT(" || !%s->GetOwner()"), *TargetInstanceRef);
		TickAdditionalCondition = FString::Printf(TEXT(" || !%s->GetOwner()"), *TargetInstanceRef);
		IsClientCode = FString::Printf(TEXT("%s->GetOwner()->HasAuthority()"), *TargetInstanceRef);
	}
	else
	{
		if (bIsBlueprint)
		{
			TargetBaseClassName = TEXT("AActor");
		}
		IsClientCode = FString::Printf(TEXT("%s->HasAuthority()"), *TargetInstanceRef);
	}
	ActorDecorator->SetInstanceRefName(TargetInstanceRef);

	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(TEXT("Declare_ReplicatorClassName"), ActorDecorator->GetReplicatorClassName());
	FormatArgs.Add(TEXT("Declare_TargetClassName"), ActorDecorator->GetActorCPPClassName());
	FormatArgs.Add(TEXT("Ref_TargetInstanceRef"), TargetInstanceRef);
	FormatArgs.Add(TEXT("Declare_ProtoNamespace"), ActorDecorator->GetProtoNamespace());
	FormatArgs.Add(TEXT("Declare_ProtoStateMsgName"), ActorDecorator->GetProtoStateMessageType());
	FormatArgs.Add(TEXT("Code_OnStateChangedAdditionalCondition"), OnStateChangedAdditionalCondition);
	FormatArgs.Add(TEXT("Code_TickAdditionalCondition"), TickAdditionalCondition);
	FormatArgs.Add(TEXT("Code_IsClient"), IsClientCode);

	if (bIsBlueprint)
	{
		FormatArgs.Add(TEXT("Declare_TargetBaseClassName"), TargetBaseClassName);
	}

	FString OverrideGetNetGUIDImplCode = ActorDecorator->GetCode_OverrideGetNetGUID();

	// ---------- Head code ----------
	FormatArgs.Add(TEXT("Code_IncludeActorHeader"), GeneratedResult.IncludeActorCode);
	FormatArgs.Add(TEXT("File_ProtoPbHead"), ActorDecorator->GetProtoDefinitionsBaseFileName() + CodeGen_ProtoPbHeadExtension);
	FormatArgs.Add(TEXT("Code_AdditionalInclude"), ActorDecorator->GetAdditionalIncludeFiles());
	FormatArgs.Add(TEXT("Declare_IndirectlyAccessiblePropertyPtrs"), ActorDecorator->GetCode_IndirectlyAccessiblePropertyPtrDeclarations());
	FormatArgs.Add(
		TEXT("Code_OverrideGetNetGUID"),
		OverrideGetNetGUIDImplCode.IsEmpty() ? TEXT("") : TEXT("virtual uint32 GetNetGUID() override;")
	);

	// RPC
	FormatArgs.Add(TEXT("Declare_OverrideSerializeAndDeserializeFunctionParams"), ActorDecorator->GetRPCNum() > 0 ? CodeGen_SerializeAndDeserializeFunctionParams : TEXT(""));

	GeneratedResult.HeadCode = FString::Format(bIsBlueprint ? CodeGen_BP_HeadCodeTemplate : CodeGen_CPP_HeadCodeTemplate, FormatArgs);
	GeneratedResult.HeadFileName = ActorDecorator->GetReplicatorClassName(false) + CodeGen_HeadFileExtension;
	// ---------- Head code ----------

	// ---------- Cpp code ----------
	FString CppCodeBuilder;
	CppCodeBuilder.Append(FString::Printf(TEXT("#include \"%s\"\n"), *GeneratedResult.HeadFileName));
	CppCodeBuilder.Append(FString::Printf(TEXT("#include \"%s\"\n\n"), *GenManager_TypeDefinitionHeadFile));

	FormatArgs.Add(
		TEXT("Code_AssignPropertyPointers"),
		ActorDecorator->GetCode_AssignPropertyPointers()
	);
	CppCodeBuilder.Append(FString::Format(
		bIsBlueprint ? CodeGen_BP_ConstructorImplTemplate : CodeGen_CPP_ConstructorImplTemplate,
		FormatArgs
	));
	CppCodeBuilder.Append(FString::Format(CodeGen_CPP_DestructorImplTemplate, FormatArgs));
	CppCodeBuilder.Append(FString::Format(CodeGen_CPP_GetDeltaStateImplTemplate, FormatArgs));
	CppCodeBuilder.Append(FString::Format(CodeGen_CPP_ClearStateImplTemplate, FormatArgs));
	if (!OverrideGetNetGUIDImplCode.IsEmpty())
	{
		CppCodeBuilder.Append(OverrideGetNetGUIDImplCode);
	}

	FormatArgs.Add(
		TEXT("Code_AllPropertiesSetDeltaState"),
		ActorDecorator->GetCode_AllPropertiesSetDeltaState(TEXT("FullState"), TEXT("DeltaState"))
	);
	CppCodeBuilder.Append(FString::Format(CodeGen_CPP_TickImplTemplate, FormatArgs));

	FormatArgs.Add(
		TEXT("Code_AllPropertyOnStateChanged"),
		ActorDecorator->GetCode_AllPropertiesOnStateChange(TEXT("NewState"))
	);
	CppCodeBuilder.Append(FString::Format(CodeGen_CPP_OnStateChangedImplTemplate, FormatArgs));

	if (ActorDecorator->GetRPCNum() > 0)
	{
		FormatArgs.Add(
			TEXT("Code_SerializeFunctionParams"),
			ActorDecorator->GetCode_SerializeFunctionParams()
		);
		FormatArgs.Add(
			TEXT("Code_DeserializeFunctionParams"),
			ActorDecorator->GetCode_DeserializeFunctionParams()
		);
		CppCodeBuilder.Append(FString::Format(CodeGen_CPP_RPCTemplate, FormatArgs));
	}

	GeneratedResult.CppCode = CppCodeBuilder;
	GeneratedResult.CppFileName = ActorDecorator->GetReplicatorClassName(false) + CodeGen_CppFileExtension;
	// ---------- Cpp code ----------

	// ---------- Protobuf ----------
	FStringFormatNamedArguments ProtoFormatArgs;
	ProtoFormatArgs.Add(TEXT("Declare_ProtoPackageName"), ActorDecorator->GetProtoPackageName());
	ProtoFormatArgs.Add(TEXT("Code_Import"),
	                    FString::Printf(TEXT("import \"%s\";\nimport \"%s\";"), *GenManager_GlobalStructProtoFile, *GenManager_UnrealCommonProtoFile)
	);
	ProtoFormatArgs.Add(
		TEXT("Option"),
		ActorDecorator->GetGoPackageImportPath().IsEmpty() ? TEXT("") : FString::Printf(TEXT("option go_package = \"%s\";"), *ActorDecorator->GetGoPackageImportPath())
	);
	ProtoFormatArgs.Add(
		TEXT("Definition_ProtoStateMsg"),
		ActorDecorator->GetDefinition_ProtoStateMessage()
	);
	GeneratedResult.ProtoDefinitionsFile = FString::Format(CodeGen_ProtoTemplate, ProtoFormatArgs);

	GeneratedResult.ProtoFileName = ActorDecorator->GetProtoDefinitionsFileName();
	// ---------- Protobuf ----------

	// ---------- Register ----------
	FString IsMapInChannelData = ActorDecorator->IsSingletonInChannelData() ? TEXT("false") : TEXT("true");
	if (!bIsBlueprint)
	{
		GeneratedResult.RegisterReplicatorCode = FString::Printf(
			TEXT("REGISTER_REPLICATOR_BASE(%s, %s, false, %s);"),
			*ActorDecorator->GetReplicatorClassName(),
			*ActorDecorator->GetActorCPPClassName(),
			*IsMapInChannelData
		);
	}
	else
	{
		GeneratedResult.RegisterReplicatorCode = FString::Printf(
			TEXT("REGISTER_REPLICATOR_BP_BASE(%s, \"%s\", false, %s);"),
			*ActorDecorator->GetReplicatorClassName(),
			*TargetActorClass->GetPathName(),
			*IsMapInChannelData
		);
	}
	// ---------- Register ----------

	return true;
}

bool FReplicatorCodeGenerator::GenerateChannelDataCode(
	const FChannelDataInfo& ChannelDataInfo
	, const FString& GoPackageImportPath
	, const FString& ProtoPackageName
	, const FString& ProtoMessageSuffix
	, FChannelDataCode& GeneratedResult
	, FString& ResultMessage
)
{
	const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, TEXT("EChanneldChannelType"), true);
	if (!EnumPtr)
	{
		ResultMessage = TEXT("Cannot find EChanneldChannelType enum");
		return false;
	}
	FText DisplayName;
	if (!EnumPtr->FindDisplayNameTextByValue(DisplayName, static_cast<int64>(ChannelDataInfo.Schema.ChannelType)))
	{
		ResultMessage = FString::Printf(TEXT("Cannot find display name for EChanneldChannelType enum value %d"), ChannelDataInfo.Schema.ChannelType);
		return false;
	}
	FString ChannelTypeName = DisplayName.ToString();
	// The display name of EChanneldChannelType enum may contain invalid characters.
	if (ChanneldReplicatorGeneratorUtils::IsCompilableClassName(ChannelTypeName))
	{
		ChannelTypeName = ChanneldReplicatorGeneratorUtils::ReplaceUncompilableChar(ChannelTypeName, TEXT(""));
	}

	GeneratedResult.ChannelType = ChannelDataInfo.Schema.ChannelType;

	const FString ChannelDataProcessorNamespace = FString::Printf(TEXT("%sChannelDataProcessor"), *ChannelTypeName);
	const FString ChannelDataProcessorClassName = FString::Printf(TEXT("F%s"), *ChannelDataProcessorNamespace);
	const FString ChannelDataProtoMsgName = FString::Printf(TEXT("%sChannelData%s"), *ChannelTypeName, *ProtoMessageSuffix);

	GeneratedResult.ChannelDataMsgName = FString::Printf(TEXT("%s.%s"), *ProtoPackageName, *ChannelDataProtoMsgName);
	GeneratedResult.ProcessorHeadFileName = FString::Printf(TEXT("%s%s"), *ChannelDataProcessorNamespace, *CodeGen_HeadFileExtension);
	GeneratedResult.ProtoBaseFileName = FString::Printf(TEXT("%s%s"), *ChannelDataProtoMsgName, TEXT(""));
	GeneratedResult.ProtoFileName = FString::Printf(TEXT("%s%s"), *GeneratedResult.ProtoBaseFileName, *CodeGen_ProtoFileExtension);
	GeneratedResult.IncludeProcessorCode = FString::Printf(TEXT("#include \"%s\""), *GeneratedResult.ProcessorHeadFileName);

	// Register channel data processor
	const FString CDPPointerName = FString::Printf(TEXT("%sPtr"), *ChannelDataProcessorNamespace);
	GeneratedResult.RegisterProcessorCode = FString::Printf(
		TEXT("%s = new %s::%s();\nChanneldReplication::RegisterChannelDataProcessor(TEXT(\"%s.%s\"), %s);\n"),
		*CDPPointerName,
		*ChannelDataProcessorNamespace, *ChannelDataProcessorClassName,
		*ProtoPackageName, *ChannelDataProtoMsgName,
		*CDPPointerName
	);
	GeneratedResult.DeleteProcessorPtrCode = FString::Printf(TEXT("delete %s;"), *CDPPointerName);
	GeneratedResult.ProcessorPtrDecl = FString::Printf(TEXT("%s::%s* %s;\n"), *ChannelDataProcessorNamespace, *ChannelDataProcessorClassName, *CDPPointerName);

	TArray<TSharedPtr<FReplicatedActorDecorator>> ActorDecoratorsToGenChannelData;

	FString Message;
	for (const FChannelDataInfo::FStateInfo& StateInfo : ChannelDataInfo.StateInfos)
	{
		TSharedPtr<FReplicatedActorDecorator> ActorDecorator;
		if (!CreateDecorateActor(ActorDecorator, Message, StateInfo.RepActorClass, StateInfo.Setting, ProtoPackageName, ProtoMessageSuffix, GoPackageImportPath, false))
		{
			UE_LOG(LogChanneldRepGenerator, Warning, TEXT("Failed to create ActorDecorator: %s"), *Message);
			return false;
		}
		if (ActorDecorator->IsSkipGenChannelDataState())
		{
			continue;
		}
		ActorDecoratorsToGenChannelData.Add(ActorDecorator);
	}

	// Generate ChannelDataProcessor Proto definition file
	if (!GenerateChannelDataProtoDefFile(
		ActorDecoratorsToGenChannelData
		, ChannelDataInfo.Schema.ChannelType
		, ChannelDataProtoMsgName
		, ProtoPackageName
		, GoPackageImportPath
		, GeneratedResult.ProtoDefsFile
	))
	{
		ResultMessage = TEXT("Failed to generate channel data proto definition file");
		return false;
	}

	TArray<TSharedPtr<FReplicatedActorDecorator>> SortedReplicationActorClasses = ActorDecoratorsToGenChannelData;
	TArray<TSharedPtr<FReplicatedActorDecorator>> ChildrenOfAActor;

	int32 AActorIndex = INDEX_NONE;
	for (int32 i = 0; i < SortedReplicationActorClasses.Num(); ++i)
	{
		const UClass* TargetClass = SortedReplicationActorClasses[i]->GetTargetClass();
		if (TargetClass == AActor::StaticClass())
		{
			AActorIndex = i;
		}
		else if (TargetClass->IsChildOf(AActor::StaticClass()))
		{
			ChildrenOfAActor.Add(SortedReplicationActorClasses[i]);
		}
	}
	// In runtime, all AActor children's state will be removed from channel data while AActor's state is removed.
	// If we remove AActor children's states before merge them, the AActor's state will add to channel data again.
	// So we need to make sure AActor is the last one in the list for generating merge channel data code.
	if (AActorIndex == INDEX_NONE)
	{
		UE_LOG(LogChanneldRepGenerator, Warning, TEXT("AActor is not included in channel data"));
	}
	else
	{
		Swap(SortedReplicationActorClasses[AActorIndex], SortedReplicationActorClasses[SortedReplicationActorClasses.Num() - 1]);
	}


	// Generate ChannelDataProcessor CPP code
	if (!GenerateChannelDataProcessorCode(
		SortedReplicationActorClasses,
		ChildrenOfAActor,
		ChannelDataInfo.Schema.ChannelType,
		ChannelDataProtoMsgName,
		ChannelDataProcessorNamespace,
		ChannelDataProcessorClassName,
		GeneratedResult.ProtoBaseFileName + CodeGen_ProtoPbHeadExtension,
		ProtoPackageName,
		GeneratedResult.ProcessorHeadCode
	))
	{
		ResultMessage = TEXT("Failed to generate ChannelDataProcessor code");
		return false;
	}

	// Generate ChannelDataProcessor Go code

	if (!GenerateChannelDataMerge_GoCode(
		SortedReplicationActorClasses,
		ChildrenOfAActor,
		ChannelDataInfo.Schema.ChannelType,
		ChannelDataProtoMsgName,
		ProtoPackageName,
		GeneratedResult.Merge_GoCode
	))
	{
		ResultMessage = TEXT("Failed to generate channel data merge go code");
		return false;
	}

	if (!GenerateChannelDataRegistration_GoCode(
		ChannelTypeName,
		ChannelDataProtoMsgName,
		ProtoPackageName,
		GeneratedResult.Registration_GoCode
	))
	{
		ResultMessage = TEXT("Failed to generate channel data registration go code");
		return false;
	}

	return true;
}

bool FReplicatorCodeGenerator::GenerateChannelDataProcessorCode(
	const TArray<TSharedPtr<FReplicatedActorDecorator>>& TargetActors,
	const TArray<TSharedPtr<FReplicatedActorDecorator>>& ChildrenOfAActor,
	const EChanneldChannelType ChannelType,
	const FString& ChannelDataMessageName,
	const FString& ChannelDataProcessorNamespace,
	const FString& ChannelDataProcessorClassName,
	const FString& ChannelDataProtoHeadFileName,
	const FString& ProtoPackageName,
	FString& ChannelDataProcessorCode
)
{
	FString ChannelDataProcessor_IncludeCode = FString::Printf(TEXT("#include \"%s\"\n"), *GenManager_TypeDefinitionHeadFile);
	FString ChannelDataProcessor_ConstPathFNameVarDecl;
	FString ChannelDataProcessor_RemovedStateDecl;
	FString ChannelDataProcessor_InitRemovedStateCode;
	FString ChannelDataProcessor_MergeCode;
	FString ChannelDataProcessor_GetStateCode;
	FString ChannelDataProcessor_SetStateCode;
	FString ChannelDataProcessor_GetRelevantNetGUIDsCode;

	// Handle Merge/GetState/SetStsate of UnrealObjectRef for the Entity channel data
	if (ChannelType == EChanneldChannelType::ECT_Entity)
	{
		ChannelDataProcessor_MergeCode.Append(CodeGen_MergeObjectState);
	}
	ChannelDataProcessor_GetStateCode.Append(FString::Format(ChannelType == EChanneldChannelType::ECT_Entity ?
		CodeGen_GetObjectStateFromEntityChannelData : CodeGen_GetObjectStateFromChannelData,
		FStringFormatNamedArguments{{"Declaration_ChannelDataMessage", ChannelDataMessageName}}));
	ChannelDataProcessor_SetStateCode.Append(FString::Format(ChannelType == EChanneldChannelType::ECT_Entity ?
		CodeGen_SetObjectStateToEntityChannelData : CodeGen_SetObjectStateToChannelData,
		FStringFormatNamedArguments{{"Declaration_ChannelDataMessage", ChannelDataMessageName}}));

	bool bHasAActor = false;
	int32 ConstPathFNameVarDeclIndex = 0;
	for (const TSharedPtr<FReplicatedActorDecorator> ActorDecorator : TargetActors)
	{
		const UClass* TargetClass = ActorDecorator->GetTargetClass();

		if (TargetClass == AActor::StaticClass())
		{
			bHasAActor = true;
		}
		if (!ActorDecorator->IsBlueprintType())
		{
			ChannelDataProcessor_IncludeCode.Append(FString::Printf(TEXT("#include \"%s\"\n"), *ActorDecorator->GetIncludeActorHeaderPath()));
		}
		ActorDecorator->SetConstClassPathFNameVarName(FString::Printf(TEXT("PathFName_%d"), ++ConstPathFNameVarDeclIndex));
		ChannelDataProcessor_ConstPathFNameVarDecl.Append(ActorDecorator->GetCode_ConstPathFNameVarDecl() + TEXT("\n"));
		ChannelDataProcessor_RemovedStateDecl.Append(ActorDecorator->GetDeclaration_ChanneldDataProcessor_RemovedStata() + TEXT("\n"));
		ChannelDataProcessor_InitRemovedStateCode.Append(ActorDecorator->GetCode_ChanneldDataProcessor_InitRemovedState());
		ChannelDataProcessor_MergeCode.Append(ActorDecorator->GetCode_ChannelDataProcessor_Merge(ChildrenOfAActor));
		
		ChannelDataProcessor_GetStateCode.Append(
			FString::Printf(
				TEXT("%s%s"),
				ChannelDataProcessor_GetStateCode.IsEmpty() ? TEXT("") : TEXT("else "),
				*ActorDecorator->GetCode_ChannelDataProcessor_GetStateFromChannelData(ChannelDataMessageName)
			)
		);
		ChannelDataProcessor_SetStateCode.Append(
			FString::Printf(
				TEXT("%s%s"),
				ChannelDataProcessor_SetStateCode.IsEmpty() ? TEXT("") : TEXT("else "),
				*ActorDecorator->GetCode_ChannelDataProcessor_SetStateToChannelData(ChannelDataMessageName)
			)
		);

		if ((TargetClass != AActor::StaticClass() &&
				TargetClass != UActorComponent::StaticClass() &&
				!TargetClass->IsChildOf(AGameStateBase::StaticClass()) &&
				!TargetClass->IsChildOf(APlayerState::StaticClass()) &&
				!TargetClass->IsChildOf(AController::StaticClass()) &&
				!ActorDecorator->IsSingletonInChannelData()) ||
			TargetClass->IsChildOf(USceneComponent::StaticClass())
		)
		{
			FStringFormatNamedArguments FormatArgs;
			FormatArgs.Add(TEXT("Declaration_CDP_ProtoVar"), ChannelDataMessageName);
			FormatArgs.Add(TEXT("Definition_StateMapName"), ActorDecorator->GetDefinition_ChannelDataFieldNameCpp());
			ChannelDataProcessor_GetRelevantNetGUIDsCode.Append(FString::Format(CodeGen_GetRelevantNetIdByStateTemplate, FormatArgs));
		}
	}
	FStringFormatNamedArguments CDPFormatArgs;
	CDPFormatArgs.Add(TEXT("Code_IncludeAdditionHeaders"), ChannelDataProcessor_IncludeCode);
	CDPFormatArgs.Add(TEXT("File_ChannelDataProtoHeader"), ChannelDataProtoHeadFileName);
	CDPFormatArgs.Add(TEXT("Declaration_CDP_Namespace"), ChannelDataProcessorNamespace);
	CDPFormatArgs.Add(TEXT("Declaration_CDP_ClassName"), ChannelDataProcessorClassName);
	CDPFormatArgs.Add(TEXT("Definition_CDP_ProtoNamespace"), TEXT("generatedchanneldata"));
	CDPFormatArgs.Add(TEXT("Code_ConstClassPathFNameVariable"), ChannelDataProcessor_ConstPathFNameVarDecl);
	CDPFormatArgs.Add(TEXT("Definition_ChanneldUEBuildInProtoNamespace"), GenManager_ChanneldUEBuildInProtoPackageName);
	CDPFormatArgs.Add(TEXT("Definition_CDP_ProtoNamespace"), ProtoPackageName);
	CDPFormatArgs.Add(TEXT("Declaration_RemovedState"), ChannelDataProcessor_RemovedStateDecl);
	CDPFormatArgs.Add(TEXT("Code_InitRemovedState"), ChannelDataProcessor_InitRemovedStateCode);
	CDPFormatArgs.Add(TEXT("Definition_CDP_ProtoMsgName"), ChannelDataMessageName);

	CDPFormatArgs.Add(TEXT("Code_Merge"), ChannelDataProcessor_MergeCode);
	CDPFormatArgs.Add(TEXT("Declaration_CDP_ProtoVar"), ChannelDataMessageName);
	CDPFormatArgs.Add(TEXT("Code_GetStateFromChannelData"), ChannelDataProcessor_GetStateCode.IsEmpty() ? TEXT("") : ChannelDataProcessor_GetStateCode + TEXT("else"));
	CDPFormatArgs.Add(TEXT("Code_SetStateToChannelData"), ChannelDataProcessor_SetStateCode.IsEmpty() ? TEXT("") : ChannelDataProcessor_SetStateCode + TEXT("else"));

	ChannelDataProcessorCode = FString::Format(*CodeGen_ChannelDataProcessorCPPTemp, CDPFormatArgs);
	return true;
}

bool FReplicatorCodeGenerator::GenerateChannelDataProtoDefFile(
	const TArray<TSharedPtr<FReplicatedActorDecorator>>& TargetActors,
	const EChanneldChannelType ChannelType,
	const FString& ChannelDataMessageName,
	const FString& ProtoPackageName,
	const FString& GoPackageImportPath,
	FString& ChannelDataProtoFile
)
{
	FString ChannelDataFields;
	FString ImportCode = FString::Printf(TEXT("import \"%s\";\n"), *GenManager_UnrealCommonProtoFile);
	int32 I = 0;
	// Entity channel data always has the UnrealObjectRef field
	if (ChannelType == EChanneldChannelType::ECT_Entity)
	{
		I++;
		ChannelDataFields.Append("optional unrealpb.UnrealObjectRef objRef = 1;\n");
	}
	for (const TSharedPtr<FReplicatedActorDecorator> ActorDecorator : TargetActors)
	{
		ChannelDataFields.Append(ActorDecorator->GetCode_ChannelDataProtoFieldDefinition(++I));
		if (!ActorDecorator->IsChanneldUEBuiltinType())
		{
			ImportCode.Append(FString::Printf(TEXT("import \"%s\";\n"), *ActorDecorator->GetProtoDefinitionsFileName()));
		}
	}
	FStringFormatNamedArguments FormatArgs;
	FormatArgs.Add(TEXT("Code_Import"), ImportCode);
	FormatArgs.Add(TEXT("Option"), FString::Printf(TEXT("option go_package = \"%s\";\n"), *GoPackageImportPath));
	FormatArgs.Add(TEXT("Declare_ProtoPackageName"), ProtoPackageName);
	FormatArgs.Add(TEXT("Definition_ProtoStateMsg"), FString::Printf(TEXT("message %s {\n%s}\n"), *ChannelDataMessageName, *ChannelDataFields));
	ChannelDataProtoFile = FString::Format(CodeGen_ProtoTemplate, FormatArgs);
	return true;
}

bool FReplicatorCodeGenerator::GenerateChannelDataMerge_GoCode(
	const TArray<TSharedPtr<FReplicatedActorDecorator>>& TargetActors,
	const TArray<TSharedPtr<FReplicatedActorDecorator>>& ChildrenOfAActor,
	const EChanneldChannelType ChannelType,
	const FString& ChannelDataMessageName,
	const FString& ProtoPackageName,
	FString& GoCode
)
{
	// The first letter of Go proto type name should be upper case.
	FString ChannelDataProtoMsgGoName = ChannelDataMessageName;
	ChannelDataProtoMsgGoName[0] = toupper(ChannelDataProtoMsgGoName[0]);

	// Generate code: Implement [channeld.MergeableChannelData]
	FString MergeStateCode = TEXT("");
	bool bHasMergeStateInMap = false;
	{
		FStringFormatNamedArguments FormatArgs;

		FString MergeActorStateCode = TEXT("");

		for (const TSharedPtr<FReplicatedActorDecorator> ActorDecorator : TargetActors)
		{
			FormatArgs.Add("Definition_StatePackagePath", ActorDecorator->GetProtoPackagePathGo(ProtoPackageName));
			FString StateClassName = ActorDecorator->GetProtoStateMessageTypeGo();
			FormatArgs.Add("Definition_StateClassName", StateClassName);

			if (ActorDecorator->IsSingletonInChannelData())
			{
				FormatArgs.Add("Definition_StateVarName", StateClassName);
				bHasMergeStateInMap = true;
				MergeStateCode.Append(FString::Format(CodeGen_Go_MergeStateTemplate, FormatArgs));
			}
			else
			{
				FormatArgs.Add("Definition_NewStateVarName", "new" + StateClassName);
				FormatArgs.Add("Definition_OldStateVarName", "old" + StateClassName);
				FormatArgs.Add("Definition_StateMapName", ActorDecorator->GetDefinition_ChannelDataFieldNameGo());

				if (ActorDecorator->GetTargetClass() == AActor::StaticClass())
				{
					FString DeleteStateCode = TEXT("");
					FStringFormatNamedArguments DeletionFormatArgs;
					for (const auto& Deletion : TargetActors)
					{
						if (Deletion->IsSingletonInChannelData())
						{
							continue;
						}
						if (Deletion->GetTargetClass()->IsChildOf<UActorComponent>())
						{
							continue;
						}
						DeletionFormatArgs.Add("Definition_StateMapName", Deletion->GetDefinition_ChannelDataFieldNameGo());
						DeleteStateCode.Append(FString::Format(CodeGen_Go_DeleteStateInMapTemplate, DeletionFormatArgs));
					}
					FormatArgs.Add("Code_DeleteFromStates", DeleteStateCode);
					// Don't add the code yet
					MergeActorStateCode = FString::Format(CodeGen_Go_MergeActorStateInMapTemplate, FormatArgs);
				}
				else
				{
					bHasMergeStateInMap = true;
					MergeStateCode.Append(FString::Format(ActorDecorator->GetTargetClass()->IsChildOf<UActorComponent>() ? CodeGen_Go_MergeCompStateInMapTemplate : CodeGen_Go_MergeStateInMapTemplate, FormatArgs));
				}
			}
		}
		// Add ActorState's merge code at last
		MergeStateCode.Append(MergeActorStateCode);
	}
	FString CheckHandoverCode = TEXT("");
	FString NotifyHandoverCode = TEXT("");
	// Only generate handover code for entity channel data
	if (ChannelType == EChanneldChannelType::ECT_Entity)
	{
		bool HasActor = false;
		bool HasSceneComponent = false;
		for (const TSharedPtr<FReplicatedActorDecorator> ActorDecorator : TargetActors)
		{
			if (!HasActor) HasActor = ActorDecorator->GetTargetClass() == AActor::StaticClass();
			if (!HasSceneComponent) HasSceneComponent = ActorDecorator->GetTargetClass() == USceneComponent::StaticClass();
		}
		if (HasActor || HasSceneComponent)
		{
			FString CheckHandoverInStatesCode = TEXT("");
			if (HasActor) CheckHandoverInStatesCode.Append(CodeGen_Go_ActorCheckHandoverTemplate);
			if (HasSceneComponent) CheckHandoverInStatesCode.Append(CodeGen_Go_SceneCompCheckHandoverTemplate);
			CheckHandoverCode = FString::Format(CodeGen_Go_CheckHandoverTemplate, FStringFormatNamedArguments{ { "Code_CheckHandoverInStates", CheckHandoverInStatesCode } });
			NotifyHandoverCode = CodeGen_Go_NotifyHandover;
		}
	}
	{
		FStringFormatNamedArguments FormatArgs;
		FormatArgs.Add("Definition_ChannelDataMsgName", ChannelDataProtoMsgGoName);
		FormatArgs.Add("Code_CheckHandover", CheckHandoverCode);
		FormatArgs.Add("Code_MergeStates", MergeStateCode);
		FormatArgs.Add("Code_NotifyHandover", NotifyHandoverCode);
		FormatArgs.Add("Decl_ChannelDataMsgVar", bHasMergeStateInMap ? TEXT("srcData") : TEXT("_"));
		GoCode.Append(FString::Format(CodeGen_Go_MergeTemplate, FormatArgs));
	}

	// GoCode.Append(TEXT("\treturn nil\n}\n"));

	// Entity channel data needs to implemented a couple of more interfaces
	if (ChannelType == EChanneldChannelType::ECT_Entity)
	{
		FStringFormatNamedArguments FormatArgs;
		FormatArgs.Add("Definition_ChannelDataMsgName", ChannelDataProtoMsgGoName);
		GoCode.Append(FString::Format(CodeGen_Go_EntityChannelDataTemplate, FormatArgs));
	}

	return true;
}

bool FReplicatorCodeGenerator::GenerateChannelDataRegistration_GoCode(
	const FString& ChannelTypeName,
	const FString& ChannelDataMessageName,
	const FString& ProtoPackageName,
	FString& GoCode
)
{
	// The first letter of Go proto type name should be upper case.
	FString ChannelDataProtoMsgGoName = ChannelDataMessageName;
	ChannelDataProtoMsgGoName[0] = toupper(ChannelDataProtoMsgGoName[0]);

	// The definition of channel type name in channeld go code is upper case, e.g. "ChannelType_GLOBAL"
	GoCode = FString::Printf(TEXT("channeld.RegisterChannelDataType(channeldpb.ChannelType_%s, &%s.%s{})"), *ChannelTypeName.ToUpper(), *ProtoPackageName, *ChannelDataProtoMsgGoName);
	return true;
}

void FReplicatorCodeGenerator::ProcessHeaderFiles(const TArray<FString>& Files, const FManifestModule& ManifestModule)
{
	FPlatformFileManager& FileManager = FPlatformFileManager::Get();
	for (const FString& HeaderFilePath : Files)
	{
		if (!FileManager.GetPlatformFile().FileExists(*HeaderFilePath))
		{
			continue;
		}
		FString Code;
		// Load source code files, and capture all 'UCLASS' marked classes.
		FFileHelper::LoadFileToString(Code, *HeaderFilePath);
		FRegexPattern MatherPatter(FString(TEXT(R"EOF(UCLASS\(.*\)\s*class\s+(?:\w+_API\s+)?([\w_]+)\s+\:)EOF")));
		FRegexMatcher Matcher(MatherPatter, Code);
		while (Matcher.FindNext())
		{
			FString CapturedCppClassName = Matcher.GetCaptureGroup(1);

			FModuleInfo ModuleInfo;
			ModuleInfo.Name = ManifestModule.Name;
			// Normalize path
			ModuleInfo.BaseDirectory = ManifestModule.BaseDirectory;
			ModuleInfo.BaseDirectory.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
			ModuleInfo.IncludeBase = ManifestModule.IncludeBase;
			ModuleInfo.IncludeBase.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
			FString RelativeToModule = HeaderFilePath;
			RelativeToModule.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
			RelativeToModule = RelativeToModule.Replace(*ModuleInfo.IncludeBase, TEXT(""), ESearchCase::CaseSensitive);
			ModuleInfo.RelativeToModule = RelativeToModule;
			ModuleInfo.bIsBuildInEngine = FPaths::IsUnderDirectory(ModuleInfo.IncludeBase, FPaths::EngineDir());
			ModuleInfoByClassName.Add(CapturedCppClassName, ModuleInfo);
			FCPPClassInfo CPPClassInfo;
			CPPClassInfo.ModuleInfo = ModuleInfo;
			CPPClassInfo.HeadFilePath = HeaderFilePath;
			CPPClassInfoMap.Add(CapturedCppClassName, CPPClassInfo);
		}
	}
}

bool FReplicatorCodeGenerator::CreateDecorateActor(
	TSharedPtr<FReplicatedActorDecorator>& OutActorDecorator,
	FString& OutResultMessage,
	const UClass* TargetActorClass,
	const FChannelDataStateSchema& ChannelDataStateSetting,
	const FString& ProtoPackageName,
	const FString& ProtoMessageSuffix,
	const FString& GoPackageImportPath,
	bool bInitPropertiesAndRPCs
)
{
	FReplicatedActorDecorator* ActorDecorator = new FReplicatedActorDecorator(
		TargetActorClass
		, [this, TargetActorClass](FString& TargetActorName, bool IsActorNameCompilable)
		{
			if (!IsActorNameCompilable)
			{
				TargetActorName = TEXT("l_") + ChanneldReplicatorGeneratorUtils::ReplaceUncompilableChar(TargetActorName, TEXT("_"));
			}
			int32* SameNameNumberPtr;
			if ((SameNameNumberPtr = TargetClassSameNameNumber.Find(TargetActorClass)) != nullptr)
			{
				if (*SameNameNumberPtr > 0)
				{
					TargetActorName = FString::Printf(TEXT("%s_%d"), *TargetActorName, *SameNameNumberPtr);
				}
			}
			else
			{
				// TargetActorName is case sensitive
				const FString TargetActorNameLower = TargetActorName.ToLower();
				if (TargetActorSameNameCounter.Contains(TargetActorNameLower))
				{
					TargetActorName = FString::Printf(TEXT("%s_%d"), *TargetActorName, TargetActorSameNameCounter[TargetActorNameLower]);
					TargetClassSameNameNumber.Add(TargetActorClass, TargetActorSameNameCounter[TargetActorNameLower]);
					++TargetActorSameNameCounter[TargetActorNameLower];
				}
				else
				{
					TargetClassSameNameNumber.Add(TargetActorClass, 0);
					TargetActorSameNameCounter.Add(TargetActorNameLower, 1);
				}
			}
		}
		, ProtoPackageName
		, ProtoMessageSuffix
		, GoPackageImportPath
		, ChannelDataStateSetting.bSingleton
		, ChannelDataStateSetting.bSkip
	);
	// If the target class is c++ class, we need to find the module it belongs to.
	// The module info is used to generate the include code in head file.
	if (!ActorDecorator->IsBlueprintType() && !ModuleInfoByClassName.Contains(ActorDecorator->GetActorCPPClassName()))
	{
		OutResultMessage = FString::Printf(TEXT("Can not find the module which the class '%s' belongs to"), *ActorDecorator->GetActorCPPClassName());
		delete ActorDecorator;
		ActorDecorator = nullptr;
		OutActorDecorator = nullptr;
		return false;
	}
	if (!ActorDecorator->IsBlueprintType())
	{
		ActorDecorator->SetModuleInfo(ModuleInfoByClassName.FindChecked(ActorDecorator->GetActorCPPClassName()));
	}
	if (bInitPropertiesAndRPCs)
	{
		ActorDecorator->InitPropertiesAndRPCs();
	}
	OutActorDecorator = MakeShareable(ActorDecorator);
	return true;
}

TArray<TSharedPtr<FStructPropertyDecorator>> FReplicatorCodeGenerator::GetAllStructPropertyDecorators(const TArray<TSharedPtr<FReplicatedActorDecorator>>& ActorDecorator) const
{
	TArray<TSharedPtr<FStructPropertyDecorator>> AllStructPropertyDecorators;
	for (const TSharedPtr<FReplicatedActorDecorator>& ActorDecoratorPtr : ActorDecorator)
	{
		AllStructPropertyDecorators.Append(ActorDecoratorPtr->GetStructPropertyDecorators());
	}
	TArray<TSharedPtr<FStructPropertyDecorator>> NonRepetitionStructPropertyDecorators;
	TSet<FString> StructPropertyDecoratorNames;
	for (const TSharedPtr<FStructPropertyDecorator>& StructPropertyDecorator : AllStructPropertyDecorators)
	{
		if (StructPropertyDecoratorNames.Contains(StructPropertyDecorator->GetCPPType()))
		{
			continue;
		}
		StructPropertyDecoratorNames.Add(StructPropertyDecorator->GetCPPType());
		NonRepetitionStructPropertyDecorators.Add(StructPropertyDecorator);
	}
	return NonRepetitionStructPropertyDecorators;
}
