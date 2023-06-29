#include "ChanneldReplicationComponent.h"
#include "ChanneldGameInstanceSubsystem.h"
#include "View/ChannelDataView.h"
#include "Engine/ActorChannel.h"
#include "Engine/PackageMapClient.h"
#include "Misc/NetworkGuid.h"
#include "ChanneldReplication.h"
#include "ChanneldNetDriver.h"
#include "ChanneldSettings.h"
#include "GameFramework/GameStateBase.h"

UChanneldReplicationComponent::UChanneldReplicationComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UChanneldReplicationComponent::InitOnce()
{
	if (!GetOwner())
	{
		return;
	}

	if (bInitialized)
	{
		return;
	}

	UGameInstance* GameInstance = GetOwner()->GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	auto Settings = GetMutableDefault<UChanneldSettings>();
	if (!Settings->bEnableNetworking || Settings->bSkipCustomReplication)
	{
		return;
	}

	// TODO: OwnerOnly replication should use private channels
	if (GetOwner()->GetIsReplicated() || GetOwner()->bOnlyRelevantToOwner)
	{
		auto OwnerReplicators = ChanneldReplication::FindAndCreateReplicators(GetOwner());
		if (OwnerReplicators.Num() > 0)
		{
			// for (auto OwnerReplicator : OwnerReplicators)
			// {
			// 	TUniquePtr<FChanneldReplicatorBase> Ptr(OwnerReplicator);
			// 	Replicators.Add(Ptr);
			// }
			Replicators.Append(OwnerReplicators);
		}
		else
		{
			UE_LOG(LogChanneld, Warning, TEXT("Unable to add replicator for owner actor: %s"), *GetOwner()->GetFullName());
		}
	}
	for (auto RepComp : GetOwner()->GetReplicatedComponents())
	{
		// ActorComponent skips the ChanneldObjectReplicator
		auto CompReplicators = ChanneldReplication::FindAndCreateReplicators(RepComp, UObject::StaticClass());
		if (CompReplicators.Num() > 0)
		{
			Replicators.Append(CompReplicators);
		}
		else
		{
			UE_LOG(LogChanneld, Warning, TEXT("Unable to add replicator for component '%s' of actor: %s"), *RepComp->GetName(), *GetOwner()->GetName());
		}
	}

	/* Server and client have different processes of adding the DataProvider.
	// Make sure the DataProvider is always registered
	GameInstance->GetSubsystem<UChanneldGameInstanceSubsystem>()->RegisterDataProvider(this);
	*/
	
	bInitialized = true;
}

void UChanneldReplicationComponent::BeginPlay()
{
	Super::BeginPlay();

	InitOnce();

	if (auto NetDriver = Cast<UChanneldNetDriver>(GetWorld()->GetNetDriver()))
	{
		NetDriver->OnServerBeginPlay(this);
	}
}

void UChanneldReplicationComponent::UninitOnce()
{
	if (bUninitialized)
		return;

	if (auto ChanneldSubsystem = GetOwner()->GetGameInstance()->GetSubsystem<UChanneldGameInstanceSubsystem>())
	{
		if (auto View = ChanneldSubsystem->GetChannelDataView())
		{
			View->RemoveProviderFromAllChannels(this, GetNetMode() == ENetMode::NM_DedicatedServer);
		}
	}
	
	bUninitialized = true;
}

void UChanneldReplicationComponent::EndPlay(EEndPlayReason::Type Reason)
{
	Super::EndPlay(Reason);

	//if (Reason != EEndPlayReason::LevelTransition)
	{
		UninitOnce();
	}
}

void UChanneldReplicationComponent::OnAddedToChannel(Channeld::ChannelId ChId)
{
	AddedToChannelIds.Add(ChId);
	OnComponentAddedToChannel.Broadcast(ChId);
}

void UChanneldReplicationComponent::OnRemovedFromChannel(Channeld::ChannelId ChId)
{
	AddedToChannelIds.Remove(ChId);
	OnComponentRemovedFromChannel.Broadcast(ChId);
}

/*
channeldpb::ChannelType UChanneldReplicationComponent::GetChannelType()
{
	return static_cast<channeldpb::ChannelType>(ChannelType);
}

google::protobuf::Message* UChanneldReplicationComponent::GetChannelDataTemplate() const
{
	auto MsgObj = ProvideChannelDataTemplate();
	if (MsgObj)
	{
		return MsgObj->GetMessage();
	}
	else
	{
		UE_LOG(LogChanneld, Error, TEXT("ChanneldActor '%s' failed to provider channel data template"), *GetFName().ToString());
		return nullptr;
	}
}

void UChanneldReplicationComponent::SetChannelId(Channeld::ChannelId ChId)
{
	OwningChannelId = ChId;
}
ChannelId UChanneldReplicationComponent::GetChannelId()
{
	return OwningChannelId;
}
*/

bool UChanneldReplicationComponent::IsRemoved()
{
	return bRemoved;
}

void UChanneldReplicationComponent::SetRemoved(bool bInRemoved)
{
	bRemoved = bInRemoved;
}

bool UChanneldReplicationComponent::UpdateChannelData(google::protobuf::Message* ChannelData)
{
	if (bUninitialized)
	{
		return false;
	}

	AActor* Owner = GetOwner();
	
	if (!Owner->HasAuthority())
	{
		return false;
	}

	if (!IsRemoved())
	{
		// Apply AActor::NetUpdateFrequency
		float t = GetWorld()->GetTimeSeconds();
		if (t - LastUpdateTime < 1.0f / Owner->NetUpdateFrequency)
		{
			return false;
		}
		LastUpdateTime = t;
	}

	auto Processor = ChanneldReplication::FindChannelDataProcessor(UTF8_TO_TCHAR(ChannelData->GetTypeName().c_str()));
	ensureMsgf(Processor, TEXT("Unable to find channel data processor for message: %s"), UTF8_TO_TCHAR(ChannelData->GetTypeName().c_str()));
	if (!Processor)
	{
		return false;
	}

	if (!Processor->UpdateChannelData(Owner, ChannelData))
	{
		return false;
	}
	
	bool bUpdated = IsRemoved();
	for (auto& Replicator : Replicators)
	{
		uint32 NetGUID = Replicator->GetNetGUID();
		if (NetGUID == 0)
		{
			UE_LOG(LogChanneld, Warning, TEXT("Replicator of '%s' doesn't has a NetGUID yet, skip setting channel data"), *Replicator->GetTargetClass()->GetName());
			continue;
		}
		
		if (IsRemoved())
		{
			Processor->SetStateToChannelData(nullptr, ChannelData, Replicator->GetTargetClass(), Replicator->GetTargetObject(), NetGUID);
			continue;
		}
		
		Replicator->Tick(FApp::GetDeltaTime());
		if (Replicator->IsStateChanged())
		{
			Processor->SetStateToChannelData(Replicator->GetDeltaState(), ChannelData, Replicator->GetTargetClass(), Replicator->GetTargetObject(), NetGUID);
			Replicator->ClearState();
			bUpdated = true;
		}
	}

	return bUpdated;
}

void UChanneldReplicationComponent::OnChannelDataUpdated(google::protobuf::Message* ChannelData)
{
	if (bUninitialized)
	{
		return;
	}
	
	if (GetOwner()->HasAuthority())
	{
		return;
	}

	auto Processor = ChanneldReplication::FindChannelDataProcessor(UTF8_TO_TCHAR(ChannelData->GetTypeName().c_str()));
	ensureMsgf(Processor, TEXT("Unable to find channel data processor for message: %s"), UTF8_TO_TCHAR(ChannelData->GetTypeName().c_str()));
	if (!Processor)
	{
		return;
	}

	if (!Processor->OnChannelDataUpdated(GetOwner(), ChannelData))
	{
		return;
	}
	
	GetOwner()->PreNetReceive();

	for (auto& Replicator : Replicators)
	{
		auto TargetObj = Replicator->GetTargetObject();
		if (!TargetObj)
		{
			UE_LOG(LogChanneld, Warning, TEXT("Replicator's target object is missing (maybe already destroyed)"));
			continue;
		}
		uint32 NetGUID = Replicator->GetNetGUID();
		if (NetGUID == 0)
		{
			// TODO: delay the state update
			UE_LOG(LogChanneld, Warning, TEXT("Replicator of %s.%s doesn't have a valid NetGUID (maybe the NetDriver is not initialized yet) to find the state."), 
				*Replicator->GetTargetObject()->GetName(), *Replicator->GetTargetClass()->GetName());
			continue;
		}
		bool bIsRemoved = false;
		auto State = Processor->GetStateFromChannelData(ChannelData, Replicator->GetTargetClass(), Replicator->GetTargetObject(), NetGUID, bIsRemoved);
		if (State)
		{
			if (bIsRemoved)
			{
				if (TargetObj == GetOwner())
				{
					GetOwner()->Destroy(true);
				}
				else if (TargetObj->IsA<UActorComponent>())
				{
					GetOwner()->RemoveOwnedComponent(Cast<UActorComponent>(TargetObj));
				}
				continue;
			}
			Replicator->OnStateChanged(State);
		}
	}

	if (auto Owner = GetOwner())
	{
		Owner->PostNetReceive();
	}
}

TSharedPtr<google::protobuf::Message> UChanneldReplicationComponent::SerializeFunctionParams(UObject* Object, UFunction* Func, void* Params, FOutParmRec* OutParams, bool& bSuccess)
{
	for (auto& Replicator : Replicators)
	{
		if (Replicator->GetTargetObject() == Object)
		{
			auto ParamsMsg = Replicator->SerializeFunctionParams(Func, Params, OutParams, bSuccess);
			if (bSuccess)
			{
				return ParamsMsg;
			}
		}
	}

	bSuccess = false;
	return nullptr;
}

TSharedPtr<void> UChanneldReplicationComponent::DeserializeFunctionParams(UObject* Object, UFunction* Func, const std::string& ParamsPayload, bool& bSuccess, bool& bDeferredRPC)
{
	for (auto& Replicator : Replicators)
	{
		if (Replicator->GetTargetObject() == Object)
		{
			TSharedPtr<void> Params = Replicator->DeserializeFunctionParams(Func, ParamsPayload, bSuccess, bDeferredRPC);
			if (bSuccess)
			{
				return Params;
			}
		}
	}

	bSuccess = false;
	return nullptr;
}
