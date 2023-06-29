#include "ChannelDataView.h"
#include "ChanneldGameInstanceSubsystem.h"
#include "ChanneldNetDriver.h"
#include "ChanneldUtils.h"
#include "EngineUtils.h"
#include "ChanneldMetrics.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Replication/ChanneldReplication.h"
#include "Replication/ChanneldReplicationComponent.h"

UChannelDataView::UChannelDataView(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

void UChannelDataView::RegisterChannelDataType(EChanneldChannelType ChannelType, const FString& MessageFullName)
{
	auto Msg = ChanneldUtils::CreateProtobufMessage(TCHAR_TO_UTF8(*MessageFullName));
	if (Msg)
	{
		RegisterChannelDataTemplate(static_cast<int>(ChannelType), Msg);
	}
	else
	{
		UE_LOG(LogChanneld, Error, TEXT("Failed to register channel data type by name: %s"), *MessageFullName);
	}
}

void UChannelDataView::Initialize(UChanneldConnection* InConn)
{
	if (Connection == nullptr)
	{
		Connection = InConn;

		LoadCmdLineArgs();

		Connection->AddMessageHandler(channeldpb::CHANNEL_DATA_UPDATE, this, &UChannelDataView::HandleChannelDataUpdate);
		Connection->AddMessageHandler(channeldpb::UNSUB_FROM_CHANNEL, this, &UChannelDataView::HandleUnsub);
	}

	const auto Settings = GetMutableDefault<UChanneldSettings>();

	for (auto& Pair : Settings->DefaultChannelDataMsgNames)
	{
		RegisterChannelDataType(Pair.Key, Pair.Value);
	}

	if (Connection->IsServer())
	{
		const float InitDelay = Settings->DelayViewInitInSeconds;
		if (InitDelay > 0)
		{
			FTimerHandle Handle;
			GetWorld()->GetTimerManager().SetTimer(Handle, [&](){InitServer();}, 1, false, InitDelay);
		}
		else
		{
			InitServer();
		}
	}
	else if (Connection->IsClient())
	{
		InitClient();
	}
	else
	{
		UE_LOG(LogChanneld, Warning, TEXT("Invalid connection type: %s"), 
			UTF8_TO_TCHAR(channeldpb::ConnectionType_Name(Connection->GetConnectionType()).c_str()));
		return;
	}
	
	UE_LOG(LogChanneld, Log, TEXT("%s initialized channels."), *this->GetClass()->GetName());
}

void UChannelDataView::InitServer()
{
	NetConnForSpawn = NewObject<UChanneldNetConnection>(GetTransientPackage(), UChanneldNetConnection::StaticClass());
	auto NetDriver = GetChanneldSubsystem()->GetNetDriver();
	NetConnForSpawn->InitBase(NetDriver, NetDriver->GetSocket(), FURL(), USOCK_Open);
	ChanneldUtils::InitNetConnForSpawn(NetConnForSpawn);

	// Add the GameStateBase (if it's an IChannelDataProvider).
	// Missing this step will cause client failing to begin play.
	AddObjectProvider(Channeld::GlobalChannelId, GetWorld()->GetAuthGameMode()->GameState);
	
	ReceiveInitServer();
}

void UChannelDataView::InitClient()
{
	ReceiveInitClient();
}

void UChannelDataView::UninitServer()
{
	ReceiveUninitServer();
}

void UChannelDataView::UninitClient()
{
	ReceiveUninitClient();
}

void UChannelDataView::Unintialize()
{
	// NetDriver = nullptr;
	
	if (Connection != nullptr)
	{
		Connection->RemoveMessageHandler(channeldpb::CHANNEL_DATA_UPDATE, this);
	}
	else
	{
		return;
	}

	if (Connection->IsServer())
	{
		UninitServer();
	}
	else if (Connection->IsClient())
	{
		UninitClient();
	}
	else
	{
		UE_LOG(LogChanneld, Warning, TEXT("Invalid connection type: %s"),
			UTF8_TO_TCHAR(channeldpb::ConnectionType_Name(Connection->GetConnectionType()).c_str()));
		return;
	}

	UE_LOG(LogChanneld, Log, TEXT("%s uninitialized channels."), *this->GetClass()->GetName());
}

void UChannelDataView::BeginDestroy()
{
	/*
	*/
	delete AnyForTypeUrl;

	for (auto Itr = ChannelDataTemplatesByTypeUrl.CreateIterator(); Itr; ++Itr)
	{
		delete Itr.Value();
	}
	// for (auto& Pair : ChannelDataTemplatesByTypeUrl)
	// {
	// 	delete Pair.Value;
	// }
	ChannelDataTemplatesByTypeUrl.Empty();

	for (auto Itr = ReceivedUpdateDataInChannels.CreateIterator(); Itr; ++Itr)
	{
		delete Itr.Value();
	}
	// for (auto& Pair : ReceivedUpdateDataInChannels)
	// {
	// 	delete Pair.Value;
	// }
	ReceivedUpdateDataInChannels.Empty();

	for (auto Itr = RemovedProvidersData.CreateIterator(); Itr; ++Itr)
	{
		delete Itr.Value();
	}
	// for (auto& Pair : RemovedProvidersData)
	// {
	// 	delete Pair.Value;
	// }
	RemovedProvidersData.Empty();
	
	ChannelDataProviders.Empty();

	Super::BeginDestroy();
}

void UChannelDataView::AddProvider(Channeld::ChannelId ChId, IChannelDataProvider* Provider)
{
	/*
	ensureMsgf(Provider->GetChannelType() != channeldpb::UNKNOWN, TEXT("Invalid channel type of data provider: %s"), *IChannelDataProvider::GetName(Provider));
	if (!ChannelDataTemplates.Contains(Provider->GetChannelType()))
	{
		RegisterChannelDataTemplate(Provider->GetChannelType(), Provider->GetChannelDataTemplate());
	}
	*/

	// Make sure provider is not set as removed when adding to a channel.
	Provider->SetRemoved(false);

	TSet<FProviderInternal>& Providers = ChannelDataProviders.FindOrAdd(ChId);
	if (Providers.Contains(Provider))
	{
		UE_LOG(LogChanneld, Verbose, TEXT("Channel data provider already exists in channel %d: %s"), ChId, *IChannelDataProvider::GetName(Provider));
		return;
	}
	
	Providers.Add(Provider);
	ChannelDataProviders[ChId] = Providers;
	UE_LOG(LogChanneld, Verbose, TEXT("Added channel data provider %s to channel %d"), *IChannelDataProvider::GetName(Provider), ChId);
	
	Provider->OnAddedToChannel(ChId);

	if (auto SavedUpdates = UnprocessedUpdateDataInChannels.Find(ChId))
	{
		UE_LOG(LogChanneld, Verbose, TEXT("Consuming %d saved ChannelDataUpdate(s) in channel %d"), SavedUpdates->Num(), ChId);

		for (auto Itr = SavedUpdates->CreateIterator(); Itr; ++Itr)
		{
			auto UpdateData = *Itr;
			Provider->OnChannelDataUpdated(UpdateData);
			Itr.RemoveCurrent();
			delete UpdateData;
		}
	}
}

void UChannelDataView::AddProviderToDefaultChannel(IChannelDataProvider* Provider)
{
	FNetworkGUID NetId = GetNetId(Provider);
	if (NetId.IsValid())
	{
		Channeld::ChannelId ChId = GetOwningChannelId(NetId);
		if (ChId != Channeld::InvalidChannelId)
		{
			AddProvider(ChId, Provider);
		}
		else
		{
			UE_LOG(LogChanneld, Warning, TEXT("Failed to add provider: no channelId mapping found for object: %s"), *IChannelDataProvider::GetName(Provider));
		}			
	}
	else
	{
		UE_LOG(LogChanneld, Warning, TEXT("Failed to add provider: can't find NetGUID for object: %s"), *IChannelDataProvider::GetName(Provider));
	}
}

bool UChannelDataView::IsObjectProvider(UObject* Obj)
{
	if (Obj == nullptr)
		return false;
	
	if (Obj->Implements<UChannelDataProvider>())
	{
		return true;
	}
	if (AActor* Actor = Cast<AActor>(Obj))
	{
		return Actor->GetComponentsByInterface(UChannelDataProvider::StaticClass()).Num() > 0;
	}
	
	return false;
}

void UChannelDataView::AddObjectProvider(Channeld::ChannelId ChId, UObject* Obj)
{
	if (Obj == nullptr)
		return;
	
	if (Obj->Implements<UChannelDataProvider>())
	{
		AddProvider(ChId, Cast<IChannelDataProvider>(Obj));
	}
	if (AActor* Actor = Cast<AActor>(Obj))
	{
		for (auto Comp : Actor->GetComponentsByInterface(UChannelDataProvider::StaticClass()))
		{
			AddProvider(ChId, Cast<IChannelDataProvider>(Comp));
		}
	}
}

void UChannelDataView::AddObjectProviderToDefaultChannel(UObject* Obj)
{
	if (Obj->Implements<UChannelDataProvider>())
	{
		AddProviderToDefaultChannel(Cast<IChannelDataProvider>(Obj));
	}
	if (AActor* Actor = Cast<AActor>(Obj))
	{
		for (const auto Comp : Actor->GetComponentsByInterface(UChannelDataProvider::StaticClass()))
		{
			AddProviderToDefaultChannel(Cast<IChannelDataProvider>(Comp));
		}
	}
}

void UChannelDataView::RemoveObjectProvider(Channeld::ChannelId ChId, UObject* Obj, bool bSendRemoved)
{
	if (Obj->Implements<UChannelDataProvider>())
	{
		RemoveProvider(ChId, Cast<IChannelDataProvider>(Obj), bSendRemoved);
	}
	if (AActor* Actor = Cast<AActor>(Obj))
	{
		for (const auto Comp : Actor->GetComponentsByInterface(UChannelDataProvider::StaticClass()))
		{
			RemoveProvider(ChId, Cast<IChannelDataProvider>(Comp), bSendRemoved);
		}
	}
}

void UChannelDataView::RemoveObjectProviderAll(UObject* Obj, bool bSendRemoved)
{
	if (Obj->Implements<UChannelDataProvider>())
	{
		RemoveProviderFromAllChannels(Cast<IChannelDataProvider>(Obj), bSendRemoved);
	}
	if (AActor* Actor = Cast<AActor>(Obj))
	{
		for (const auto Comp : Actor->GetComponentsByInterface(UChannelDataProvider::StaticClass()))
		{
			RemoveProviderFromAllChannels(Cast<IChannelDataProvider>(Comp), bSendRemoved);
		}
	}
}

void UChannelDataView::RemoveProvider(Channeld::ChannelId ChId, IChannelDataProvider* Provider, bool bSendRemoved)
{
	if (Provider->IsRemoved())
	{
		return;
	}
	
	Provider->SetRemoved(true);
	
	TSet<FProviderInternal>* Providers = ChannelDataProviders.Find(ChId);
	if (Providers != nullptr)
	{
		UE_LOG(LogChanneld, Verbose, TEXT("Removing channel data provider %s from channel %d"), *IChannelDataProvider::GetName(Provider), ChId);

		const auto ChannelInfo = Connection->SubscribedChannels.Find(ChId);

		// Don't send removal update to the spatial or entity channel
		if (bSendRemoved && ChannelInfo && ChannelInfo->ShouldSendRemovalUpdate())
		{
			// Collect the removed states immediately (before the provider gets destroyed completely)
			google::protobuf::Message* RemovedData = RemovedProvidersData.FindRef(ChId);
			if (!RemovedData)
			{
				const auto MsgTemplate = ChannelDataTemplates.FindRef(static_cast<int>(ChannelInfo->ChannelType));
				if (!ensureMsgf(MsgTemplate, TEXT("Can't find channel data message template of channel type: %s"), *UEnum::GetValueAsString(ChannelInfo->ChannelType)))
				{
					Providers->Remove(Provider);
					Provider->OnRemovedFromChannel(ChId);
					return;
				}
				RemovedData = MsgTemplate->New();
				RemovedProvidersData.Add(ChId, RemovedData);
			}
			Provider->UpdateChannelData(RemovedData);
		}
		else
		{
			Providers->Remove(Provider);
			Provider->OnRemovedFromChannel(ChId);
		}
	}
}

void UChannelDataView::RemoveProviderFromAllChannels(IChannelDataProvider* Provider, bool bSendRemoved)
{
	if (Connection == nullptr)
	{
		UE_LOG(LogChanneld, Error, TEXT("Unable to call UChannelDataView::RemoveProviderFromAllChannels. The connection to channeld hasn't been set up yet and there's no subscription to any channel."));
		return;
	}

	/*
	ensureMsgf(Provider->GetChannelType() != channeldpb::UNKNOWN, TEXT("Invalid channel type of data provider: %s"), *IChannelDataProvider::GetName(Provider));
	for (auto& Pair : Connection->SubscribedChannels)
	{
		if (static_cast<channeldpb::ChannelType>(Pair.Value.ChannelType) == Provider->GetChannelType())
		{
			RemoveProvider(Pair.Key, Provider, bSendRemoved);
			return;
		}
	}
	*/
	for (auto& Pair : ChannelDataProviders)
	{
		if (Pair.Value.Contains(Provider))
		{
			RemoveProvider(Pair.Key, Provider, bSendRemoved);
		}
	}
}

void UChannelDataView::MoveProvider(Channeld::ChannelId OldChId, Channeld::ChannelId NewChId, IChannelDataProvider* Provider, bool bSendRemoved)
{
	RemoveProvider(OldChId, Provider, bSendRemoved);
	if (!Connection->SubscribedChannels.Contains(NewChId))
	{
		UE_LOG(LogChanneld, Warning, TEXT("Moving a provider '%s' to channel %d which hasn't been subscribed yet."), *IChannelDataProvider::GetName(Provider), NewChId);
	}
	AddProvider(NewChId, Provider);
}

void UChannelDataView::MoveObjectProvider(Channeld::ChannelId OldChId, Channeld::ChannelId NewChId, UObject* Provider, bool bSendRemoved)
{
	if (Provider->Implements<UChannelDataProvider>())
	{
		MoveProvider(OldChId, NewChId, Cast<IChannelDataProvider>(Provider), bSendRemoved);
	}
	if (AActor* Actor = Cast<AActor>(Provider))
	{
		for (const auto Comp : Actor->GetComponentsByInterface(UChannelDataProvider::StaticClass()))
		{
			MoveProvider(OldChId, NewChId, Cast<IChannelDataProvider>(Comp), bSendRemoved);
		}
	}
}

void UChannelDataView::OnClientPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer, UChanneldNetConnection* NewPlayerConn)
{
	ensureMsgf(NewPlayer->GetComponentByClass(UChanneldReplicationComponent::StaticClass()), TEXT("PlayerController is missing UChanneldReplicationComponent. Failed to spawn PC, GameStateBase, and other pawns in the client."));
	/*
	// Actors may don't have UChanneldReplicationComponent created when OnServerSpawnedObject is called, so we should try to add the providers again.
	AddActorProvider(GameMode->GameState);
	AddActorProvider(NewPlayer);
	AddActorProvider(NewPlayer->PlayerState);
	*/
	
	// Send the GameStateBase to the new player. This is very important as later the GameStateBase will be fanned out to the client with bReplicatedHasBegunPlay=true, to trigger the client's BeginPlay().
	SendSpawnToConn(GameMode->GameState, NewPlayerConn, 0);
	
	/* Unfortunately, a couple of RPC on the PC is called in GameMode::PostLogin BEFORE invoking this event. So we need to handle the RPC properly.
	 * UPDATE: no need to worry - the client can queue unmapped RPC now!
	 */
	// Send the PC to the owning client after the PC and NetConn are mutually referenced.
	SendSpawnToConn(NewPlayer, NewPlayerConn, NewPlayerConn->GetConnId());
	SendSpawnToConn(NewPlayer->PlayerState, NewPlayerConn, NewPlayerConn->GetConnId());
	
	/* OnServerSpawnedActor() sends the spawning of the new player's pawn to other clients */

	/*
	// Send the existing player pawns to the new player.
	// PC only exists in owning client as an AutonomousProxy, so we don't send it.
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PC = Iterator->Get();
		if (PC && PC != NewPlayer)
		{
			if (PC->GetPawn())
			{
				NewPlayerConn->SendSpawnMessage(PC->GetPawn(), ENetRole::ROLE_SimulatedProxy);
			}
		}
	}
	*/

	// Send all the existing actors to the new player, including the static level actors.
	if (auto NetDriver = GetChanneldSubsystem()->GetNetDriver())
	{
		for(TActorIterator<AActor> It(GetWorld(), AActor::StaticClass()); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor != GameMode->GameState && Actor != NewPlayer && Actor != NewPlayer->PlayerState)
			{
				NetDriver->OnServerSpawnedActor(Actor);
			}
		}
	}
}

FNetworkGUID UChannelDataView::GetNetId(UObject* Obj, bool bAssignOnServer/* = true*/) const
{
	if (const auto NetDriver = GetChanneldSubsystem()->GetNetDriver())
	{
		if (NetDriver->IsServer() && bAssignOnServer)
		{
			return NetDriver->GuidCache->GetOrAssignNetGUID(Obj);
		}
		else
		{
			return NetDriver->GuidCache->GetNetGUID(Obj);
		}
	}
	return FNetworkGUID();
}

FNetworkGUID UChannelDataView::GetNetId(IChannelDataProvider* Provider) const
{
	return GetNetId(Provider->GetTargetObject());
}

bool UChannelDataView::OnServerSpawnedObject(UObject* Obj, const FNetworkGUID NetId)
{
	if (!NetId.IsValid())
		return false;

	Channeld::ChannelId ChId = GetChanneldSubsystem()->LowLevelSendToChannelId.Get();
	SetOwningChannelId(NetId, ChId);
	// NetIdOwningChannels.Add(NetId, ChId);
	// UE_LOG(LogChanneld, Log, TEXT("Set up mapping of netId: %d -> channelId: %d, spawned: %s"), NetId.Value, ChId, *GetNameSafe(Obj));

	AddObjectProvider(ChId, Obj);

	return true;
}

void UChannelDataView::SendSpawnToClients(UObject* Obj, uint32 OwningConnId)
{
	const auto NetDriver = GetChanneldSubsystem()->GetNetDriver();
	if (!NetDriver)
	{
		UE_LOG(LogChanneld, Error, TEXT("UChannelDataView::SendSpawnToClients: Unable to get ChanneldNetDriver"));
		return;
	}
	
	for (auto& Pair : NetDriver->GetClientConnectionMap())
	{
		if (IsValid(Pair.Value))
		{
			SendSpawnToConn(Obj, Pair.Value, OwningConnId);
		}
	}
}

void UChannelDataView::SendDestroyToClients(UObject* Obj, const FNetworkGUID NetId)
{
	// Don't broadcast the destroy of objects that are only spawned in the owning client.
	if (Obj->IsA<APlayerController>())
	{
		return;
	}
	
	const auto NetDriver = GetChanneldSubsystem()->GetNetDriver();
	if (!NetDriver)
	{
		UE_LOG(LogChanneld, Error, TEXT("UChannelDataView::SendDestroyToClients: Unable to get ChanneldNetDriver"));
		return;
	}
	
	for (auto& Pair : NetDriver->GetClientConnectionMap())
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->SendDestroyMessage(Obj);
		}
	}
}

void UChannelDataView::SendSpawnToConn(UObject* Obj, UChanneldNetConnection* NetConn, uint32 OwningConnId)
{
	ENetRole Role = ROLE_None;
	if (const AActor* Actor = Cast<AActor>(Obj))
	{
		Role = Actor->GetRemoteRole();
	}
	NetConn->SendSpawnMessage(Obj, Role, Channeld::InvalidChannelId, OwningConnId);
}

void UChannelDataView::OnDestroyedActor(AActor* Actor, const FNetworkGUID NetId)
{
	if (!NetId.IsValid())
		return;

 	Channeld::ChannelId RemovedChId = NetIdOwningChannels.Remove(NetId);
	UE_LOG(LogChanneld, Log, TEXT("Removed mapping of netId: %d (%d) -> channelId: %d"), NetId.Value, ChanneldUtils::GetNativeNetId(NetId.Value), RemovedChId);

	RemoveObjectProviderAll(Actor, false);
}

void UChannelDataView::SetOwningChannelId(const FNetworkGUID NetId, Channeld::ChannelId ChId)
{
	if (!NetId.IsValid())
		return;
	
	NetIdOwningChannels.Add(NetId, ChId);
	UE_LOG(LogChanneld, Log, TEXT("Set up mapping of netId: %d (%d) -> channelId: %d"), NetId.Value, ChanneldUtils::GetNativeNetId(NetId.Value), ChId);

	/*
	if (Connection->IsServer())
	{
		if (AActor* Actor = Cast<AActor>(GetObjectFromNetGUID(NetId)))
		{
			if (Connection->OwnedChannels.Contains(ChId))
			{
				Actor->SetRole(ROLE_Authority);
			}
			else
			{
				Actor->SetRole(ROLE_SimulatedProxy);
			}
			UE_LOG(LogChanneld, Verbose, TEXT("[Server] Set %s's NetRole to %s"), *Actor->GetName(), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), Actor->GetLocalRole()));
		}
	}
	*/
}

Channeld::ChannelId UChannelDataView::GetOwningChannelId(const FNetworkGUID NetId) const
{
	const Channeld::ChannelId* ChId = NetIdOwningChannels.Find(NetId);
	if (ChId)
	{
		return *ChId;
	}
	return Channeld::InvalidChannelId;
}

Channeld::ChannelId UChannelDataView::GetOwningChannelId(AActor* Actor) const
{
	/* Actors don't necessarily have a NetConnection to have to NetId.
	if (const auto NetConn = Actor->GetNetConnection())
	{
		if (const auto NetDriver = Cast<UChanneldNetDriver>(NetConn->Driver))
		{
			const FNetworkGUID NetId = NetDriver->GuidCache->GetNetGUID(Actor);
			if (NetId.IsValid())
			{
				return GetOwningChannelId(NetId);
			}
			else
			{
				UE_LOG(LogChanneld, Log, TEXT("No NetGUID has been assigned to Actor %s"), *GetNameSafe(Actor));
			}
		}
	}

	return Channeld::InvalidChannelId;
	*/
	
	return GetOwningChannelId(GetNetId(Actor));
}

bool UChannelDataView::SendMulticastRPC(AActor* Actor, const FString& FuncName, TSharedPtr<google::protobuf::Message> ParamsMsg, const FString& SubObjectPathName)
{
	unrealpb::RemoteFunctionMessage RpcMsg;
	RpcMsg.mutable_targetobj()->set_netguid(GetNetId(Actor).Value);
	RpcMsg.set_functionname(TCHAR_TO_UTF8(*FuncName), FuncName.Len());
	RpcMsg.set_subobjectpath(TCHAR_TO_UTF8(*SubObjectPathName), SubObjectPathName.Len());
	if (ParamsMsg)
	{
		RpcMsg.set_paramspayload(ParamsMsg->SerializeAsString());
		UE_LOG(LogChanneld, VeryVerbose, TEXT("Serialized RPC parameters to %d bytes"), RpcMsg.paramspayload().size());
	}

	auto ChId = GetOwningChannelId(Actor);
	// Does this server owns the channel that owns the actor?
	if (auto ChannelInfo = Connection->OwnedChannels.Find(ChId))
	{
		if (ChannelInfo->ChannelType == EChanneldChannelType::ECT_Global || ChannelInfo->ChannelType == EChanneldChannelType::ECT_SubWorld)
		{
			Connection->Broadcast(ChId, unrealpb::RPC, RpcMsg, channeldpb::ALL_BUT_SERVER);
		}
		else if (ChannelInfo->ChannelType == EChanneldChannelType::ECT_Spatial)
		{
			Connection->Broadcast(ChId, unrealpb::RPC, RpcMsg, channeldpb::ADJACENT_CHANNELS | channeldpb::ALL_BUT_SERVER);
		}
		else
		{
			UE_LOG(LogChanneld, Warning, TEXT("Multicast RPC is only supported in Global, SubWorld and Spatial channels. ChannelId: %d, ChannelType: %d"), ChId, (int32)ChannelInfo->ChannelType);
			GEngine->GetEngineSubsystem<UChanneldMetrics>()->OnDroppedRPC(RpcMsg.functionname());
			return false;
		}
	}
	else
	{
		// Forward the RPC to the server that owns the channel / actor.
		Connection->Broadcast(ChId, unrealpb::RPC, RpcMsg, channeldpb::SINGLE_CONNECTION);
		UE_LOG(LogChanneld, Log, TEXT("Forwarded RPC %s::%s to the owner of channel %d"), *Actor->GetName(), *FuncName, ChId);
	}

	if (auto NetDriver = GetChanneldSubsystem()->GetNetDriver())
	{
		NetDriver->OnSentRPC(RpcMsg);
	}
	
	return true;
}

void UChannelDataView::OnDisconnect()
{
	for (auto& Pair : ChannelDataProviders)
	{
		for (FProviderInternal& Provider : Pair.Value)
		{
			if (Provider.IsValid())
			{
				Provider->SetRemoved(true);
			}
		}
	}

	// Force to send the channel update data with the removed states to channeld
	SendAllChannelUpdates();
}

int32 UChannelDataView::SendChannelUpdate(Channeld::ChannelId ChId)
{
	auto ChannelInfo = Connection->SubscribedChannels.Find(ChId);
	if (ChannelInfo == nullptr)
	{
		UE_LOG(LogChanneld, Warning, TEXT("Failed to SendChannelUpdate due to no subscription found for channel %d"), ChId);
		return 0;
	}
	if (ChannelInfo->SubOptions.DataAccess != EChannelDataAccess::EDA_WRITE_ACCESS && !Connection->OwnedChannels.Contains(ChId))
	{
		return 0;
	}
	
	TSet<FProviderInternal>* Providers = ChannelDataProviders.Find(ChId);
	if (Providers == nullptr || Providers->Num() == 0)
	{
		return 0;
	}

	auto MsgTemplate = ChannelDataTemplates.FindRef(static_cast<int>(ChannelInfo->ChannelType));
	if (!ensureMsgf(MsgTemplate, TEXT("Can't find channel data message template of channel type: %d"), ChannelInfo->ChannelType))
	{
		return 0;
	}

	auto DeltaChannelData = MsgTemplate->New(&ArenaForSend);

	int UpdateCount = 0;
	int RemovedCount = 0;
	for (auto Itr = Providers->CreateIterator(); Itr; ++Itr)
	{
		auto Provider = Itr.ElementIt->Value;
		if (Provider.IsValid())
		{
			/* Pre-replication logic should be implemented in the replicator.
			Provider->GetTargetObject()->CallPreReplication();
			*/
			if (Provider->UpdateChannelData(DeltaChannelData))
			{
				UpdateCount++;
			}
			if (Provider->IsRemoved())
			{
				Itr.RemoveCurrent();
				RemovedCount++;
				Provider->OnRemovedFromChannel(ChId);
			}
		}
		else
		{
			Itr.RemoveCurrent();
			RemovedCount++;
		}
	}
	if (RemovedCount > 0)
	{
		UE_LOG(LogChanneld, Log, TEXT("Removed %d channel data provider(s) from channel %d"), RemovedCount, ChId);
	}

	if (UpdateCount > 0 || (RemovedCount > 0 && ChannelInfo->ShouldSendRemovalUpdate()))
	{
		// Merge removed states
		google::protobuf::Message* RemovedData;
		if (RemovedProvidersData.RemoveAndCopyValue(ChId, RemovedData))
		{
			DeltaChannelData->MergeFrom(*RemovedData);
			delete RemovedData;
		}
				
		channeldpb::ChannelDataUpdateMessage UpdateMsg;
		UpdateMsg.mutable_data()->PackFrom(*DeltaChannelData);
		Connection->Send(ChId, channeldpb::CHANNEL_DATA_UPDATE, UpdateMsg);

		UE_LOG(LogChanneld, Verbose, TEXT("Sent %s update: %s"), UTF8_TO_TCHAR(DeltaChannelData->GetTypeName().c_str()), UTF8_TO_TCHAR(DeltaChannelData->DebugString().c_str()));
	}

	return UpdateCount;
}

int32 UChannelDataView::SendAllChannelUpdates()
{
	if (Connection == nullptr)
		return 0;

	int32 TotalUpdateCount = 0;
	for (auto& Pair : Connection->SubscribedChannels)
	{
		// Skip sending updates for spatial channels. The channel data is maintained by channeld (by handling the Spawn and Destroy messages)
		if (Pair.Value.ChannelType == EChanneldChannelType::ECT_Spatial)
		{
			continue;
		}
		
		TotalUpdateCount += SendChannelUpdate(Pair.Key);
	}

	ArenaForSend.Reset();

	if (TotalUpdateCount > 0)
	{
		UChanneldMetrics* Metrics = GEngine->GetEngineSubsystem<UChanneldMetrics>();
		Metrics->ReplicatedProviders_Counter->Increment(TotalUpdateCount);
	}

	return TotalUpdateCount;
}

UChanneldGameInstanceSubsystem* UChannelDataView::GetChanneldSubsystem() const
{
	// The subsystem owns the view.
	return Cast<UChanneldGameInstanceSubsystem>(GetOuter());
/*
	UWorld* World = GetWorld();
	// The client may still be in pending net game
	if (World == nullptr)
	{
		// The subsystem owns the view.
		return Cast<UChanneldGameInstanceSubsystem>(GetOuter());
	}
	if (World)
	{
		UGameInstance* GameInstance = World->GetGameInstance();
		if (GameInstance)
		{
			auto Result = GameInstance->GetSubsystem<UChanneldGameInstanceSubsystem>();
			//UE_LOG(LogChanneld, Log, TEXT("Found ChanneldGameInstanceSubsystem: %d"), Result == nullptr ? 0 : 1);
			return Result;
		}
	}
	return nullptr;
*/
}

UObject* UChannelDataView::GetObjectFromNetGUID(const FNetworkGUID& NetId)
{
	if (auto NetDriver = GetChanneldSubsystem()->GetNetDriver())
	{
		return NetDriver->GuidCache->GetObjectFromNetGUID(NetId, true);
	}
	return nullptr;
}

void UChannelDataView::HandleUnsub(UChanneldConnection* _, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
{
	auto UnsubMsg = static_cast<const channeldpb::UnsubscribedFromChannelResultMessage*>(Msg);
	UE_LOG(LogChanneld, Log, TEXT("Received unsub of conn(%d), connType=%s, channelType=%s, channelId=%d"),
		UnsubMsg->connid(),
		UTF8_TO_TCHAR(channeldpb::ConnectionType_Name(UnsubMsg->conntype()).c_str()),
		UTF8_TO_TCHAR(channeldpb::ChannelType_Name(UnsubMsg->channeltype()).c_str()),
		ChId);

	// When current connection unsubs from a channel, remove all providers in that channel.
	if (UnsubMsg->connid() == Connection->GetConnId())
	{
		TSet<FProviderInternal> Providers;
		if (ChannelDataProviders.RemoveAndCopyValue(ChId, Providers))
		{
			UE_LOG(LogChanneld, Log, TEXT("Received Unsub message. Removed all data providers(%d) from channel %d"), Providers.Num(), ChId);
			OnRemovedProvidersFromChannel(ChId, UnsubMsg->channeltype(), Providers);
		}
	}

	if (Connection->IsServer())
	{
		if (UnsubMsg->conntype() == channeldpb::CLIENT && Connection->OwnedChannels.Contains(ChId))
		{
			ServerHandleClientUnsub(UnsubMsg->connid(), UnsubMsg->channeltype(), ChId);
		}
	}
}

void UChannelDataView::ServerHandleClientUnsub(Channeld::ConnectionId ClientConnId, channeldpb::ChannelType ChannelType, Channeld::ChannelId ChId)
{
	if (auto NetDriver = GetChanneldSubsystem()->GetNetDriver())
	{
		UE_LOG(LogChanneld, Log, TEXT("Client leaves the game, removing the connection: %d"), ClientConnId);
		NetDriver->RemoveChanneldClientConnection(ClientConnId);
	}
}

void UChannelDataView::HandleChannelDataUpdate(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
{
	auto UpdateMsg = static_cast<const channeldpb::ChannelDataUpdateMessage*>(Msg);
	
	FString TypeUrl(UTF8_TO_TCHAR(UpdateMsg->data().type_url().c_str()));
	auto MsgTemplate = ChannelDataTemplatesByTypeUrl.FindRef(TypeUrl);
	if (MsgTemplate == nullptr)
	{
		UE_LOG(LogChanneld, Error, TEXT("Unable to find channel data template by typeUrl: %s"), *TypeUrl);
		return;
	}

	google::protobuf::Message* UpdateData;
	if (ReceivedUpdateDataInChannels.Contains(ChId))
	{
		UpdateData = ReceivedUpdateDataInChannels[ChId];
	}
	else
	{
		UpdateData = MsgTemplate->New();
		ReceivedUpdateDataInChannels.Add(ChId, UpdateData);
	}

	UE_LOG(LogChanneld, Verbose, TEXT("Received %s channel %d update(%d B): %s"), *GetChanneldSubsystem()->GetChannelTypeNameByChId(ChId), ChId, UpdateMsg->data().value().size(), UTF8_TO_TCHAR(UpdateMsg->DebugString().c_str()));

	const FName MessageName = UTF8_TO_TCHAR(UpdateData->GetTypeName().c_str());
	auto Processor = ChanneldReplication::FindChannelDataProcessor(MessageName);
	if (Processor)
	{
		// Use the message template as the temporary message to unpack the any data.
		if (!UpdateMsg->data().UnpackTo(MsgTemplate))
		{
			UE_LOG(LogChanneld, Warning, TEXT("Failed to unpack %s channel data, typeUrl: %s"), *GetChanneldSubsystem()->GetChannelTypeNameByChId(ChId), UTF8_TO_TCHAR(UpdateMsg->data().type_url().c_str()));
			return;
		}
		if (!Processor->Merge(MsgTemplate, UpdateData))
		{
			UE_LOG(LogChanneld, Warning, TEXT("Failed to merge %s channel data: %s"), *GetChanneldSubsystem()->GetChannelTypeNameByChId(ChId), UTF8_TO_TCHAR(MsgTemplate->DebugString().c_str()));
			return;
		}
	}
	else
	{
		UE_LOG(LogChanneld, Log, TEXT("ChannelDataProcessor not found for type: %s, fall back to ParsePartialFromString. Risk: The state with the same NetId will be overwritten instead of merged."), *MessageName.ToString());
		// Call ParsePartial instead of Parse to keep the existing value from being reset.
		if (!UpdateData->ParsePartialFromString(UpdateMsg->data().value()))
		{
			UE_LOG(LogChanneld, Error, TEXT("Failed to parse %s channel data, typeUrl: %s"), *GetChanneldSubsystem()->GetChannelTypeNameByChId(ChId), UTF8_TO_TCHAR(UpdateMsg->data().type_url().c_str()));
			return;
		}
	}

	if (CheckUnspawnedObject(ChId, UpdateData))
	{
		UE_LOG(LogChanneld, Verbose, TEXT("Resolving unspawned object, the channel data will not be consumed."));
		return;
	}
	
	ConsumeChannelUpdateData(ChId, UpdateData);
}

bool UChannelDataView::ConsumeChannelUpdateData(Channeld::ChannelId ChId, google::protobuf::Message* UpdateData)
{
	TSet<FProviderInternal>* Providers = ChannelDataProviders.Find(ChId);
	if (Providers == nullptr || Providers->Num() == 0)
	{
		UE_LOG(LogChanneld, Log, TEXT("No provider registered for channel %d. The update will not be applied."), ChId);

		/* Saving the update data with removed=true can caused the provider gets destroyed immediately after AddProvider.
		auto UnprocessedUpdateData = UpdateData->New();
		UnprocessedUpdateData->CopyFrom(*UpdateData);
		UnprocessedUpdateDataInChannels.FindOrAdd(ChId).Add(UnprocessedUpdateData);
		*/
		
		return false;
	}

	// The set can be changed during the iteration, when a new provider is created from the UnrealObjectRef during any replicator's OnStateChanged(),
	// or the provider's owner actor got destroyed by removed=true. So we use a const array to iterate.
	TArray<FProviderInternal> ProvidersArr = Providers->Array();
	bool bConsumed = false;
	for (FProviderInternal& Provider : ProvidersArr)
	{
		if (Provider.IsValid() && !Provider->IsRemoved())
		{
			Provider->OnChannelDataUpdated(UpdateData);
			bConsumed = true;
		}
	}

	// All new states are consumed, now we reset the cached object for the next parse.
	if (bConsumed)
	{
		UpdateData->Clear();
	}
	
	return bConsumed;
}

/* The following comment no longer applies since we added caching for ChanneldUtils::GetRefOfObject.
// Warning: DO NOT use this function before sending the Spawn message!
// Calling Provider->UpdateChannelData can cause FChanneldActorReplicator::Tick to be called, which will call
// ChanneldUtils::GetRefOfObject and export the NetGUIDs, causing the UnrealObjectRef in the Spawn message
// missing the contexts, so the client will fail to Spawn the object.
*/
const google::protobuf::Message* UChannelDataView::GetEntityData(UObject* Obj)
{
	IChannelDataProvider* Provider= nullptr;
	if (Obj->GetClass()->ImplementsInterface(UChannelDataProvider::StaticClass()))
	{
		Provider = Cast<IChannelDataProvider>(Obj);
	}
	else if (AActor* Actor = Cast<AActor>(Obj))
	{
		auto Comps = Actor->GetComponentsByInterface(UChannelDataProvider::StaticClass());
		if (Comps.Num() > 0)
		{
			Provider = Cast<IChannelDataProvider>(Comps[0]);
		}
	}

	if (Provider == nullptr)
	{
		return nullptr;
	}

	auto MsgTemplate = ChannelDataTemplates.FindRef(channeldpb::ENTITY);
	if (!ensureMsgf(MsgTemplate, TEXT("Can't find channel data message template of entity channel.")))
	{
		return nullptr;
	}

	auto ChannelData = MsgTemplate->New(&ArenaForSend);
	Provider->UpdateChannelData(ChannelData);

	return ChannelData;
}

