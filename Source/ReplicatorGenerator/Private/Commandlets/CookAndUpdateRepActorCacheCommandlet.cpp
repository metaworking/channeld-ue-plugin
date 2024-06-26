﻿#include "Commandlets/CookAndUpdateRepActorCacheCommandlet.h"

#include "ReplicatorGeneratorUtils.h"
#include "Components/TimelineComponent.h"
#include "Persistence/RepActorCacheController.h"
#include "Persistence/StaticObjectExportController.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"

void FLoadedObjectListener::StartListen()
{
	GUObjectArray.AddUObjectCreateListener(this);
}

void FLoadedObjectListener::StopListen()
{
	GUObjectArray.RemoveUObjectCreateListener(this);
}

void FLoadedObjectListener::NotifyUObjectCreated(const UObjectBase* Object, int32 Index)
{
	CreatedObjects.Add((const UObject*)Object);
		
	const UClass* LoadedClass = Object->GetClass();
	while (LoadedClass != nullptr)
	{
		const FString ClassPath = LoadedClass->GetPathName();
		if (CheckedClasses.Contains(ClassPath))
		{
			break;
		}
		CheckedClasses.Add(ClassPath);

		if (LoadedClass == AActor::StaticClass() || LoadedClass == UActorComponent::StaticClass())
		{
			FilteredClasses.Add(LoadedClass);
			break;
		}
		if (ChanneldReplicatorGeneratorUtils::TargetToGenerateChannelDataField(LoadedClass))
		{
			FilteredClasses.Add(LoadedClass);
		}
		LoadedClass = LoadedClass->GetSuperClass();
	}
}

void FLoadedObjectListener::OnUObjectArrayShutdown()
{
	GUObjectArray.RemoveUObjectCreateListener(this);
}

UCookAndUpdateRepActorCacheCommandlet::UCookAndUpdateRepActorCacheCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

bool UCookAndUpdateRepActorCacheCommandlet::AddObjectAndPath(TArray<const UObject*>& NameStableObjects, TSet<FString>& AddedObjectPath, const UObject* Obj)
{
	if (!IsValid(Obj))
	{
		return false;
	}
			
	if (IsTransient(Obj))
	{
		FName ObjName = Obj->GetFName();
		if (ObjName.IsValid())
		{
			UE_LOG(LogTemp, Log, TEXT("Ignore transient object %s"), *ObjName.ToString());
		}
		return false;
	}
	
	if (Obj->IsA<UField>())
	{
		return false;
	}
	
	if (Obj->IsA<UEdGraphNode>())
	{
		return false;
	}

	if (!Obj->IsFullNameStableForNetworking())
	{
		return false;
	}
			
	FString ObjectPath = Obj->GetPathName();
	
	if (ObjectPath.Contains("dict_"))
	{
		return false;
	}
	
	if (AddedObjectPath.Contains(ObjectPath))
	{
		return false;
	}
	NameStableObjects.Add(Obj);
	AddedObjectPath.Add(ObjectPath);
	return true;
}

int32 UCookAndUpdateRepActorCacheCommandlet::Main(const FString& CmdLineParams)
{
	FReplicatorGeneratorManager& GeneratorManager = FReplicatorGeneratorManager::Get();

	TSet<FSoftClassPath> LoadedRepClasses;

	// Pass 1: Iterate in-memory classes
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (ChanneldReplicatorGeneratorUtils::TargetToGenerateChannelDataField(*It))
		{
			LoadedRepClasses.Add(*It);
		}
	}

	// Pass 2: Add the new classes in the cook process
	FLoadedObjectListener ObjLoadedListener;
	ObjLoadedListener.StartListen();

	const FString AdditionalParam(TEXT(" -SkipShaderCompile"));
	FString NewCmdLine = CmdLineParams;
	NewCmdLine.Append(AdditionalParam);
	int32 Result = Super::Main(NewCmdLine);
	
	ObjLoadedListener.StopListen();
	if (Result != 0)
	{
		return Result;
	}

	LoadedRepClasses.Append(ObjLoadedListener.FilteredClasses);
	TArray<const UClass*> TargetClasses;
	bool bHasTimelineComponent = false;
	for (const FSoftClassPath& ObjSoftPath : LoadedRepClasses)
	{
		if (const UClass* LoadedClass = ObjSoftPath.TryLoadClass<UObject>())
		{
			TargetClasses.Add(LoadedClass);
			if (LoadedClass == UTimelineComponent::StaticClass())
			{
				bHasTimelineComponent = true;
			}
			else if (!bHasTimelineComponent)
			{
				if (ChanneldReplicatorGeneratorUtils::HasTimelineComponent(LoadedClass))
				{
					TargetClasses.Add(UTimelineComponent::StaticClass());
					bHasTimelineComponent = true;
				}
			}
		}
	}

	if (NewCmdLine.Contains("-exportstatic"))
	{
		TArray<const UObject*> NameStableObjects;
		TSet<FString> AddedObjectPath;

		for (auto Obj : ObjLoadedListener.CreatedObjects)
		{
			if (Obj.IsValid())
			{
				AddObjectAndPath(NameStableObjects, AddedObjectPath, Obj.Get());
			}
		}

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray< FString > ContentPaths;
		ContentPaths.Add(TEXT("/Game"));
		ContentPaths.Add(TEXT("/Script"));

		AssetRegistry.ScanPathsSynchronous(ContentPaths);

		TArray< FAssetData > AssetList;
		AssetRegistry.GetAllAssets(AssetList);
		for (auto AssetData : AssetList)
		{
#if ENGINE_MAJOR_VERSION >= 5
			FString ObjectPath = AssetData.GetObjectPathString();
#else
			FString ObjectPath = AssetData.ObjectPath.ToString();
#endif
			UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);

			if (AddObjectAndPath(NameStableObjects, AddedObjectPath, Object))
			{
				AddObjectAndPath(NameStableObjects, AddedObjectPath, Object->GetOuter());

				// Blueprint CDO
				if (Object->IsA<UBlueprint>())
				{
					FString GeneratedClassNameString = FString::Printf(TEXT("%s_C"), *ObjectPath);
					if (Object->IsA<AActor>())
					{
						UClass* Class = LoadClass<AActor>(nullptr, *GeneratedClassNameString);
						if (Class)
						{
							NameStableObjects.Add(Class->GetDefaultObject(true));
						}
					}
					else
					{
						UClass* Class = LoadClass<UObject>(nullptr, *GeneratedClassNameString);
						if (Class)
						{
							NameStableObjects.Add(Class->GetDefaultObject(true));
						}
					}
				}
			}
		}

		UStaticObjectExportController* StaticObjectExportController = GEditor->GetEditorSubsystem<
			UStaticObjectExportController>();
		if (!StaticObjectExportController->SaveStaticObjectExportInfo(NameStableObjects))
		{
			UE_LOG(LogChanneldRepGenerator, Error, TEXT("SaveStaticObjectExportInfo failed"));
		}
	}
	else
	{
		IFileManager::Get().Delete(*(GenManager_ChanneldStaticObjectExportPath));
	}
	
	URepActorCacheController* RepActorCacheController = GEditor->GetEditorSubsystem<URepActorCacheController>();
	if (RepActorCacheController == nullptr || !RepActorCacheController->SaveRepActorCache(TargetClasses))
	{
		return 1;
	}

	return 0;
}

bool UCookAndUpdateRepActorCacheCommandlet::IsTransient(const UObject* Obj)
{
	if (!IsValid(Obj))
	{
		return false;
	}

	if ((Obj->GetFlags() & RF_Transient) > 0)
	{
		return true;
	}

	return IsTransient(Obj->GetOuter());
}
