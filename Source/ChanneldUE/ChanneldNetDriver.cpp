#include "ChanneldNetDriver.h"

#include "Net/DataReplication.h"
#include "Net/RepLayout.h"
#include "Misc/ScopeExit.h"
#include "google/protobuf/message_lite.h"
#include "Engine/NetConnection.h"
#include "PacketHandler.h"
#include "Net/Core/Misc/PacketAudit.h"
#include "ChanneldGameInstanceSubsystem.h"
#include "ChanneldUtils.h"
#include "ChanneldSettings.h"
#include "ChanneldMetrics.h"
#include "GameFramework/PlayerState.h"
#include "Replication/ChanneldReplicationComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "ChanneldNetConnection.h"
#include "Engine/World.h"

UChanneldNetDriver::UChanneldNetDriver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Set default values so the plugin users don't have to set them in the INI.
	NetServerMaxTickRate = 30;
}

UChanneldNetConnection* UChanneldNetDriver::AddChanneldClientConnection(Channeld::ConnectionId ClientConnId, Channeld::ChannelId ChId)
{
	auto ClientConnection = NewObject<UChanneldNetConnection>(GetTransientPackage(), NetConnectionClass);
	ClientConnection->bDisableHandshaking = GetMutableDefault<UChanneldSettings>()->bDisableHandshaking;
	// Server always sees a connected client (forwarded from channeld) as authenticated.
	ClientConnection->bChanneldAuthenticated = true;
	ClientConnection->InitRemoteConnection(this, GetSocket(), InitBaseURL, ConnIdToAddr(ClientConnId).Get(), EConnectionState::USOCK_Open);

	Notify->NotifyAcceptedConnection(ClientConnection);
	AddClientConnection(ClientConnection);

	ClientConnectionMap.Add(ClientConnId, ClientConnection);

	if (ChannelDataView.IsValid())
	{
		ChannelDataView->OnAddClientConnection(ClientConnection, ChId);
	}

	UE_LOG(LogChanneld, Log, TEXT("Server added client connection %d, total connections: %d (%d)"), ClientConnId, ClientConnections.Num(), ClientConnectionMap.Num());

	if (!ClientConnection->bDisableHandshaking && ConnectionlessHandler.IsValid() && StatelessConnectComponent.IsValid())
	{
		ClientConnection->bInConnectionlessHandshake = true;
	}
	return ClientConnection;
}

void UChanneldNetDriver::RemoveChanneldClientConnection(Channeld::ConnectionId ClientConnId)
{
	UChanneldNetConnection* ClientConn;
	if (ClientConnectionMap.RemoveAndCopyValue(ClientConnId, ClientConn))
	{
		if (ChannelDataView.IsValid())
		{
			ChannelDataView->OnRemoveClientConnection(ClientConn);
		}
		// CleanUp will call Driver->RemoveClientConnection()
		ClientConn->CleanUp();
		
		UE_LOG(LogChanneld, Log, TEXT("Server removed client connection %d, total connections: %d (%d)"), ClientConnId, ClientConnections.Num(), ClientConnectionMap.Num());
	}
	else
	{
		UE_LOG(LogChanneld, Warning, TEXT("Server failed to remove client connection %d, total connections: %d (%d)"), ClientConnId, ClientConnections.Num(), ClientConnectionMap.Num());
	}
}

void UChanneldNetDriver::HandleSpawnObject(TSharedRef<unrealpb::SpawnObjectMessage> SpawnMsg)
{
	FNetworkGUID NetId = FNetworkGUID(SpawnMsg->obj().netguid());
	
	// If the object with the same NetId exists, destroy it before spawning a new one.
	UObject* OldObj = ChanneldUtils::GetObjectByRef(&SpawnMsg->obj(), GetWorld(), false);
	if (OldObj)
	{
		UE_LOG(LogChanneld, Log, TEXT("[Client] Found spawned object %s of duplicated NetId: %d, will skip."), *GetNameSafe(OldObj), SpawnMsg->obj().netguid());

		/* Found a lot of duplicated WorldSettings and Character in the log, so just skip them for now.
		GuidCache->ObjectLookup.Remove(NetId);
		GuidCache->NetGUIDLookup.Remove(OldObj);
			
		if (AActor* OldActor = Cast<AActor>(OldObj))
		{
			GetWorld()->DestroyActor(OldActor, true);
		}
		*/
		return;
	}

	if (ChannelDataView.IsValid() && SpawnMsg->has_channelid())
	{
		// Set up the mapping before actually spawn it, so AddProvider() can find the mapping.
		ChannelDataView->SetOwningChannelId(NetId, SpawnMsg->channelid());
			
		// Also add the mapping of all context NetGUIDs
		for (auto& ContextObj : SpawnMsg->obj().context())
		{
			ChannelDataView->SetOwningChannelId(ContextObj.netguid(), SpawnMsg->channelid());
		}
	}
		
	UObject* NewObj = ChanneldUtils::GetObjectByRef(&SpawnMsg->obj(), GetWorld());
	if (NewObj)
	{
		// if (SpawnMsg->has_channelid())
		// {
		// 	ChannelDataView->OnSpawnedObject(SpawnedObj, FNetworkGUID(SpawnMsg->obj().netguid()), SpawnMsg->channelid());
		// }

		ENetRole LocalRole = static_cast<ENetRole>(SpawnMsg->localrole());
		if (AActor* NewActor = Cast<AActor>(NewObj))
		{
				/*
			// The first PlayerController on client side is AutonomousProxy; others are SimulatedProxy.
			if (NewActor->IsA<APlayerController>())
			{
				LocalRole = NewActor == GetWorld()->GetFirstPlayerController() ? ENetRole::ROLE_AutonomousProxy : ENetRole::ROLE_SimulatedProxy;
				// TEST: don't spawn other PC!
				if (LocalRole == ENetRole::ROLE_SimulatedProxy)
				{
					GetWorld()->DestroyActor(NewActor);
					return;
				}
				NewActor->SetRole(LocalRole);
			}
			else*/ if (SpawnMsg->has_localrole())
			{
				NewActor->SetRole(LocalRole);
			}

			if (SpawnMsg->mutable_obj()->owningconnid() > 0)
			{
				ChanneldUtils::SetActorRoleByOwningConnId(NewActor, SpawnMsg->mutable_obj()->owningconnid());
				LocalRole = NewActor->GetLocalRole();
			}
		}

		if (ChannelDataView.IsValid())
		{
			ChannelDataView->AddObjectProviderToDefaultChannel(NewObj);
			ChannelDataView->OnNetSpawnedObject(NewObj, SpawnMsg->channelid());
		}

		UE_LOG(LogChanneld, Verbose, TEXT("[Client] Spawned object from message: %s, NetId: %d, owning channel: %d, local role: %d"), *NewObj->GetName(), SpawnMsg->obj().netguid(), SpawnMsg->channelid(), LocalRole);
	}
	else
	{
		UE_LOG(LogChanneld, Warning, TEXT("[Client] Failed to spawn object from msg: %s"), UTF8_TO_TCHAR(SpawnMsg->DebugString().c_str()));
	}
}

void UChanneldNetDriver::OnUserSpaceMessageReceived(uint32 MsgType, Channeld::ChannelId ChId, Channeld::ConnectionId ClientConnId, const std::string& Payload)
{
	if (MsgType == unrealpb::LOW_LEVEL)
	{
		if (Payload.size() == 0)
		{
			UE_LOG(LogChanneld, Warning, TEXT("Empty payload for LowLeveSend, ClientConnId: %d"), ClientConnId);
			return;
		}
		
		if (ConnToChanneld->IsClient())
		{
			const auto MyServerConnection = GetServerConnection();
			if (MyServerConnection)
			{
				MyServerConnection->ReceivedRawPacket((uint8*)Payload.data(), Payload.size());
			}
			else
			{
				UE_LOG(LogChanneld, Error, TEXT("ServerConnection doesn't exist"));
			}
		}
		else
		{
			auto ClientConnection = ClientConnectionMap.FindRef(ClientConnId);
			// Server's ClientConnection is created when the first packet (NMT_Hello) from client arrives.
			if (ClientConnection == nullptr)
			{
				ClientConnection = AddChanneldClientConnection(ClientConnId, ChId);
			}
			ClientConnection->ReceivedRawPacket((uint8*)Payload.data(), Payload.size());
		}
	}
	else if (MsgType == unrealpb::RPC)
	{
		auto RpcMsg = MakeShared<unrealpb::RemoteFunctionMessage>();
		if (!RpcMsg->ParseFromString(Payload))
		{
			UE_LOG(LogChanneld, Error, TEXT("Failed to parse RemoteFunctionMessage"));
			return;
		}

		HandleCustomRPC(RpcMsg);
		OnReceivedRPC(*RpcMsg);
	}
	else if (MsgType == unrealpb::SPAWN)
	{
		auto SpawnMsg = MakeShared<unrealpb::SpawnObjectMessage>();
		if (!SpawnMsg->ParseFromString(Payload))
		{
			UE_LOG(LogChanneld, Error, TEXT("Failed to parse SpawnObjectMessage"));
			return;
		}

		HandleSpawnObject(SpawnMsg);
	}
	else if (MsgType == unrealpb::DESTROY)
	{
		auto DestroyMsg = MakeShared<unrealpb::DestroyObjectMessage>();
		if (!DestroyMsg->ParseFromString(Payload))
		{
			UE_LOG(LogChanneld, Error, TEXT("Failed to parse DestroyObjectMessage"));
			return;
		}

		UObject* ObjToDestroy = GuidCache->GetObjectFromNetGUID(FNetworkGUID(DestroyMsg->netid()), true);
		if (ObjToDestroy)
		{
			UE_LOG(LogChanneld, Verbose, TEXT("[Client] Destroying object from message: %s, NetId: %d"), *GetNameSafe(ObjToDestroy), DestroyMsg->netid());
			
			if (AActor* Actor = Cast<AActor>(ObjToDestroy))
			{
				GetWorld()->DestroyActor(Actor, true);
			}
			else
			{
				ObjToDestroy->ConditionalBeginDestroy();
			}
		}
		else
		{
			UE_LOG(LogChanneld, Warning, TEXT("[Client] Failed to destroy object from msg: %s"), UTF8_TO_TCHAR(DestroyMsg->DebugString().c_str()));
		}
	}
}

void UChanneldNetDriver::OnReceivedRPC(const unrealpb::RemoteFunctionMessage& RpcMsg)
{
	UChanneldMetrics* Metrics = GEngine->GetEngineSubsystem<UChanneldMetrics>();
	Metrics->ReceivedRPCs_Counter->Increment();
#if !UE_BUILD_SHIPPING
	Metrics->ReceivedRPCs->Add({{"funcName", RpcMsg.functionname()}}).Increment();
#endif
}

void UChanneldNetDriver::HandleCustomRPC(TSharedPtr<unrealpb::RemoteFunctionMessage> Msg)
{
	// We should NEVER creates the actor via RPC
	AActor* Actor = Cast<AActor>(ChanneldUtils::GetObjectByRef(&Msg->targetobj(), GetWorld(), false));
	if (!Actor)
	{
		// Case 1: the client receives the RPC before the spawn message. Should wait until the actor is spawned.
		if (!IsServer())
		{
			UnprocessedRPCs.Add(Msg);
			UE_LOG(LogChanneld, Log, TEXT("Cannot find actor to call remote function '%s', NetGUID: %d. Pushed to the next tick."), UTF8_TO_TCHAR(Msg->functionname().c_str()), Msg->targetobj().netguid());
		}
		// Case 2: the server receives the client RPC, but the actor has just been handed over to another server (deleted).
		else
		{
			RedirectRPC(Msg);
		}
		return;
	}
	// Case 3: the server receives the client RPC, but the actor has just been handed over to another server (became non-authoritative).
	if (IsServer() && !Actor->HasAuthority())
	{
		RedirectRPC(Msg);
		return;
	}

	//TSet<FNetworkGUID> UnmappedGUID;
	bool bDelayRPC = false;
	FName FuncName = UTF8_TO_TCHAR(Msg->functionname().c_str());
	UObject* SubObject = nullptr;
	if (Msg->subobjectpath().length() > 0)
	{
		SubObject = Actor->GetDefaultSubobjectByName(Msg->subobjectpath().c_str());
		if(!SubObject)
		{
			UE_LOG(LogChanneld, Log, TEXT("Actor:%s, NetGuid:%d can not find Subobject:%s for Function:%s"),
				*Actor->GetName(),  Msg->targetobj().netguid(), *FString(Msg->subobjectpath().c_str()), *FuncName.ToString());
		}
	}
	ReceivedRPC(Actor, FuncName, Msg->paramspayload(), bDelayRPC, SubObject);
	if (bDelayRPC)
	{
		UE_LOG(LogChanneld, Log, TEXT("Deferred RPC '%s::%s' due to unmapped NetGUID: %d"), *Actor->GetName(), *FuncName.ToString(), Msg->targetobj().netguid());
		UnprocessedRPCs.Add(Msg);
	}
}

Channeld::ConnectionId UChanneldNetDriver::AddrToConnId(const FInternetAddr& Addr)
{
	uint32 ConnId;
	Addr.GetIp(ConnId);
	return ConnId;
}

TSharedRef<FInternetAddr> UChanneldNetDriver::ConnIdToAddr(Channeld::ConnectionId ConnId)
{
	auto AddrPtr = CachedAddr.Find(ConnId);
	if (AddrPtr == nullptr)
	{
		auto Addr = GetSocketSubsystem()->CreateInternetAddr();
		Addr->SetIp(ConnId);
		CachedAddr.Add(ConnId, Addr);
		AddrPtr = &Addr;
	}
	return *AddrPtr;
}

Channeld::ChannelId UChanneldNetDriver::GetSendToChannelId(UChanneldNetConnection* NetConn) const
{
	// Server view can implement the mapping of connId -> channelId.
	if (IsServer())
	{
		if (ChannelDataView.IsValid())
		{
			Channeld::ChannelId Result;
			if (ChannelDataView->GetSendToChannelId(NetConn, Result))
			{
				return Result;
			}
		}
	}
	
	return LowLevelSendToChannelId.Get();
}

UChanneldGameInstanceSubsystem* UChanneldNetDriver::GetSubsystem() const
{
	UWorld* TheWorld = GetWorld();
	if (!TheWorld)
	{
		TheWorld = GEngine->GetWorldContextFromPendingNetGameNetDriver(this)->World();
	}
	auto GameInstance = TheWorld->GetGameInstance();
	if (GameInstance)
	{
		return GameInstance->GetSubsystem<UChanneldGameInstanceSubsystem>();
	}
	return nullptr;
}

void UChanneldNetDriver::PostInitProperties()
{
	Super::PostInitProperties();
}

bool UChanneldNetDriver::IsAvailable() const
{
	return true;
}

bool UChanneldNetDriver::InitConnectionClass()
{
	NetConnectionClass = UChanneldNetConnection::StaticClass();
	return true;
}

bool UChanneldNetDriver::InitBase(bool bInitAsClient, FNetworkNotify* InNotify, const FURL& URL,
	bool bReuseAddressAndPort, FString& Error)
{
	auto Subsystem = GetSubsystem();
	if (Subsystem)
	{
		LowLevelSendToChannelId = Subsystem->LowLevelSendToChannelId;
		/*
		// Share the same ChanneldConnection with the subsystem if it exists.
		// In the standalone net game, NetDriver::InitBase is called when starting client travel, so the connection in the subsystem should be already created.
		ConnToChanneld = Subsystem->GetConnection();
		*/

		// Set the GuidCache in the view so it can map UObject(NetGUID) <-> ChannelId
		if (Subsystem->GetChannelDataView())
		{
			ChannelDataView = Subsystem->GetChannelDataView();
			// ChannelDataView->NetDriver = TSharedPtr<UChanneldNetDriver>(this);
		}
		else
		{
			Subsystem->OnViewInitialized.AddWeakLambda(this, [&](UChannelDataView* InView)
			{
				ChannelDataView = InView;
				// InView->NetDriver = TSharedPtr<UChanneldNetDriver>(this);
			});
		}

		if (bInitAsClient)
		{
			Subsystem->OnSetLowLevelSendChannelId.AddWeakLambda(this, [&]()
			{
				GetServerConnection()->FlushUnauthData();
			});
		}
	}

	ConnToChanneld = GEngine->GetEngineSubsystem<UChanneldConnection>();

	ConnToChanneld->OnUserSpaceMessageReceived.AddUObject(this, &UChanneldNetDriver::OnUserSpaceMessageReceived);

	InitBaseURL = URL;

	if (!ConnToChanneld->IsConnected())
	{
		FString Host;
		int Port;
		auto Settings = GetMutableDefault<UChanneldSettings>();
		if (bInitAsClient)
		{
			Host = Settings->ChanneldIpForClient;
			Port = Settings->ChanneldPortForClient;
		}
		else
		{
			Host = Settings->ChanneldIpForServer;
			Port = Settings->ChanneldPortForServer;
		}

		if (!ConnToChanneld->Connect(bInitAsClient, Host, Port, Error))
		{
			Error = TEXT("Failed to connect to channeld");
			return false;
		}
	}

	if (!ConnToChanneld->IsAuthenticated())
	{
		ConnToChanneld->OnAuthenticated.AddUObject(this, &UChanneldNetDriver::OnChanneldAuthenticated);

		ConnToChanneld->Auth(TEXT("test_pit"), TEXT("test_lt"));
	}

	return UNetDriver::InitBase(bInitAsClient, InNotify, URL, bReuseAddressAndPort, Error);
}

bool UChanneldNetDriver::InitConnect(FNetworkNotify* InNotify, const FURL& ConnectURL, FString& Error)
{
	ISocketSubsystem* SocketSubsystem = GetSocketSubsystem();
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogNet, Warning, TEXT("Unable to find socket subsystem"));
		return false;
	}

	if (!InitBase(true, InNotify, ConnectURL, false, Error))
	{
		UE_LOG(LogNet, Warning, TEXT("Failed to init net driver ConnectURL: %s: %s"), *ConnectURL.ToString(), *Error);
		return false;
	}

	/* Driver->ConnectionlessHandler is only used in server
	if (!bDisableHandshaking)
	{
		InitConnectionlessHandler();
	}
	*/

	// Create new connection.
	ServerConnection = NewObject<UNetConnection>(GetTransientPackage(), NetConnectionClass);
	UChanneldNetConnection* NetConnection = CastChecked<UChanneldNetConnection>(ServerConnection);

	if (NetConnection == nullptr)
	{
		Error = TEXT("Could not cast the ServerConnection into the base connection class for this netdriver!");
		return false;
	}

	NetConnection->bDisableHandshaking = GetMutableDefault<UChanneldSettings>()->bDisableHandshaking;
	ServerConnection->InitLocalConnection(this, GetSocket(), ConnectURL, USOCK_Open);
	//NetConnection->bInConnectionlessHandshake = true;

	UE_LOG(LogNet, Log, TEXT("Game client on port %i, rate %i"), ConnectURL.Port, ServerConnection->CurrentNetSpeed);
	CreateInitialClientChannels();

	if (ConnToChanneld->IsAuthenticated())
	{
		// Connection is already authenticated via the subsystem
		OnChanneldAuthenticated(ConnToChanneld);
	}

	return true;
}

bool UChanneldNetDriver::InitListen(FNetworkNotify* InNotify, FURL& LocalURL, bool bReuseAddressAndPort, FString& Error)
{
	if (!InitBase(false, InNotify, LocalURL, bReuseAddressAndPort, Error))
	{
		UE_LOG(LogNet, Warning, TEXT("Failed to init net driver ListenURL: %s: %s"), *LocalURL.ToString(), *Error);
		return false;
	}

	if (!GetMutableDefault<UChanneldSettings>()->bDisableHandshaking)
	{
		InitConnectionlessHandler();
	}

	if (!GetMutableDefault<UChanneldSettings>()->bSkipCustomReplication)
	{
		FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &UChanneldNetDriver::OnClientPostLogin);
	}

	GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UChanneldNetDriver::OnServerSpawnedActor));

	return true;

	//return Super::InitListen(InNotify, LocalURL, bReuseAddressAndPort, Error);
}

ISocketSubsystem* UChanneldNetDriver::GetSocketSubsystem()
{
	return ISocketSubsystem::Get();
}

FSocket* UChanneldNetDriver::GetSocket()
{
	//return Super::GetSocket();
	// Return the Socket to channeld
	return ConnToChanneld->GetSocket();

	// SetSocket can't be overridden
}

void UChanneldNetDriver::LowLevelSend(TSharedPtr<const FInternetAddr> Address, void* Data, int32 CountBits,
	FOutPacketTraits& Traits)
{
	//Super::LowLevelSend(Address, Data, CountBits, Traits);
	
	if (ConnToChanneld->IsConnected() && Address.IsValid() && Address->IsValid())
	{
		// Copied from UIpNetDriver::LowLevelSend
		uint8* DataToSend = reinterpret_cast<uint8*>(Data);
		int32 DataSize = FMath::DivideAndRoundUp(CountBits, 8);

		FPacketAudit::NotifyLowLevelReceive(DataToSend, DataSize);

		if (!GetMutableDefault<UChanneldSettings>()->bDisableHandshaking && ConnectionlessHandler.IsValid())
		{
			const ProcessedPacket ProcessedData =
				ConnectionlessHandler->OutgoingConnectionless(Address, (uint8*)DataToSend, CountBits, Traits);

			if (!ProcessedData.bError)
			{
				DataToSend = ProcessedData.Data;
				DataSize = FMath::DivideAndRoundUp(ProcessedData.CountBits, 8);
			}
			else
			{
				return;
			}
		}

		if (ConnToChanneld->IsServer())
		{
			Channeld::ConnectionId ClientConnId = AddrToConnId(*Address);
			if (auto Conn = ClientConnectionMap.FindRef(ClientConnId))
			{
				Conn->SendData(unrealpb::LOW_LEVEL, DataToSend, DataSize);
			}
			else
			{
				UE_LOG(LogChanneld, Log, TEXT("[Server] Failed to LowLevelSend to client %d"), ClientConnId);
			}
		}
		else
		{
			GetServerConnection()->SendData(unrealpb::LOW_LEVEL, DataToSend, DataSize);
		}
	}
}

void UChanneldNetDriver::LowLevelDestroy()
{
	ChannelDataView = nullptr;
	
	FGameModeEvents::GameModePostLoginEvent.RemoveAll(this);
	
	ClientConnectionMap.Reset();

	if (ConnToChanneld)
	{
		ConnToChanneld->OnAuthenticated.RemoveAll(this);
		ConnToChanneld->OnUserSpaceMessageReceived.RemoveAll(this);
		// ConnToChanneld->RemoveMessageHandler(channeldpb::UNSUB_FROM_CHANNEL, this);
		//ConnToChanneld->RemoveMessageHandler(unrealpb::LOW_LEVEL, this);
		//ConnToChanneld->RemoveMessageHandler(MessageType_RPC, this);

		/*
		// Only disconnect when the connection is owned by the net driver.
		// Otherwise the connection is owned by the subsystem, and it will be used across sessions, 
		// while the net driver will be created and destroyed for every client travel.
		if (ConnToChanneld->GetOuter() == this)
		{
			ConnToChanneld->Disconnect(true);
		}
		*/
	}
	ConnToChanneld = nullptr;

	Super::LowLevelDestroy();
}

bool UChanneldNetDriver::IsNetResourceValid()
{
	if ((ConnToChanneld->IsServer() && !ServerConnection)//  Server
		|| (ConnToChanneld->IsClient() && ServerConnection) // client
		)
	{
		return true;
	}

	return false;
}

void UChanneldNetDriver::OnServerSpawnedActor(AActor* Actor)
{
	if (GetMutableDefault<UChanneldSettings>()->bSkipCustomReplication)
	{
		return;
	}
	
	/* Newly spawned actor always has LocalRole = Authority
	*/
	if (!Actor->HasAuthority())
	{
		return;
	}

	if (Actor->HasDeferredComponentRegistration())
	{
		UE_LOG(LogChanneld, VeryVerbose, TEXT("[Server] Actor %s has deferred component registration. The spawn logic will be deferred to BeginPlay()."), *Actor->GetName());
		ServerDeferredSpawns.Add(Actor);
		return;
	}

	// Make sure the NetGUID exists.
	FNetworkGUID NetId = GuidCache->GetOrAssignNetGUID(Actor);

	/* Moved to UChanneldGameInstanceSubsystem::OnActorSpawned
	UChanneldGameInstanceSubsystem* ChanneldSubsystem = GetSubsystem();
	UChannelDataView* View = ChanneldSubsystem->GetChannelDataView();
	FNetworkGUID NetId = GuidCache->GetNetGUID(Actor);
 	Channeld::ChannelIdChId = LowLevelSendToChannelId.Get();
	if (View)
	{
		View->OnSpawnedObject(Actor, NetId, ChId);
	}
	else
	{
		ChanneldSubsystem->OnViewInitialized.AddWeakLambda(Actor, [Actor, NetId, ChId](UChannelDataView* InView)
		{
			InView->OnSpawnedObject(Actor, NetId, ChId);
		});
	}
	*/
	if (ChannelDataView.IsValid())
	{
		if (!ChannelDataView->OnServerSpawnedObject(Actor, NetId))
		{
			return;
		}
	}
	else
	{
		UE_LOG(LogChanneld, Log, TEXT("ChannelDataView is not initialized yet. If the actor '%s' is a DataProvider, it will not be registered."), *Actor->GetName());
	}

	// Send the spawn of PlayerController and PlayerState in OnClientPostLogin instead, because
	// 1) we only want to send PC to owning client, but at this moment, Actor doesn't have NetConnection set yet;
	// 2) NetConnection is not set up for the PlayerState yet, but we need it for setting the actor location in the spawn message.
	if (Actor->IsA<APlayerController>() || Actor->IsA<APlayerState>())
	{
		return;
	}

	if (!Actor->GetIsReplicated())
	{
		return;
	}

	/* At this moment, PlayerController doesn't have RemoteRole or NetConnection set yet.
	 * We should wait and send the spawn message later, but a couple of RPC on the PC will be called in GameMode::PostLogin immediately.
	if (Actor->IsA<APlayerController>())
	{
		return;
	}
	*/

	/*
	if (Actor->GetComponentsByInterface(UChannelDataProvider::StaticClass()).Num() == 0)
	{
		UE_LOG(LogChanneld, Warning, TEXT("[Server] Replicating actor %s doesn't implement IChannelDataProvider, will not be spawn to client."), *Actor->GetName());
		return;
	}
	*/

	/*
	// Already sent
	const FNetworkGUID NetId = GuidCache->GetNetGUID(Actor);
	if (NetId.IsValid() && SentSpawnedNetGUIDs.Contains(NetId))
	{
		return;
	}
	*/

	uint32 OwningConnId = 0;
	if (auto NetConn = Cast<UChanneldNetConnection>(Actor->GetNetConnection()))
	{
		OwningConnId = NetConn->GetConnId();
	}

	if (ChannelDataView.IsValid())
	{
		ChannelDataView->SendSpawnToClients(Actor, OwningConnId);
	}
	else
	{
		UE_LOG(LogChanneld, Warning, TEXT("Failed to send Spawn message to client as the view doesn't exist. Actor: %s"), *GetNameSafe(Actor));
	}
}

void UChanneldNetDriver::OnServerBeginPlay(UChanneldReplicationComponent* RepComp)
{
	AActor* Actor = RepComp->GetOwner();
	// Actor has deferred component registration, so we need to wait for BeginPlay to perform the spawn logic. E.g. BP_TestCube.
	if (ServerDeferredSpawns.Contains(Actor))
	{
		OnServerSpawnedActor(Actor);
		ServerDeferredSpawns.Remove(Actor);
	}
	// Actor's ChanneldReplicationComponent is not registered when spawned, so we need to wait for BeginPlay to perform the spawn logic. E.g. BP_TestNPC.
	else if (RepComp->AddedToChannelIds.Num() == 0)
	{
		if (ChannelDataView.IsValid())
		{
			UE_LOG(LogChanneld, VeryVerbose, TEXT("[Server] Actor %s has deferred ChanneldReplicationComponent registration. Adding it to default channel."), *Actor->GetName());
			ChannelDataView->AddProviderToDefaultChannel(RepComp);
		}
	}
}

void UChanneldNetDriver::SetAllSentSpawn(const FNetworkGUID NetId)
{
	for (auto& Pair : ClientConnectionMap)
	{
		Pair.Value->SetSentSpawned(NetId);
	}
}

void UChanneldNetDriver::RedirectRPC(TSharedPtr<unrealpb::RemoteFunctionMessage> Msg)
{
	if (Msg->redirectioncounter() < GetMutableDefault<UChanneldSettings>()->RpcRedirectionMaxRetries)
	{
		if (ChannelDataView.IsValid())
		{
			Channeld::ChannelId TargetChId = ChannelDataView->GetOwningChannelId(FNetworkGUID(Msg->targetobj().netguid()));
			ensureMsgf(!ConnToChanneld->OwnedChannels.Contains(TargetChId), TEXT("Attempt to redirect RPC to the same server, netId: %d, func: %s"), Msg->targetobj().netguid(), UTF8_TO_TCHAR(Msg->functionname().c_str()));
		
			if (TargetChId != Channeld::InvalidChannelId)
			{
				Msg->set_redirectioncounter(Msg->redirectioncounter() + 1);
				ConnToChanneld->Broadcast(TargetChId, unrealpb::RPC, *Msg, channeldpb::SINGLE_CONNECTION);
				UE_LOG(LogChanneld, Verbose, TEXT("Redirect RPC to channel %d, netId: %d, func: %s"), TargetChId, Msg->targetobj().netguid(), UTF8_TO_TCHAR(Msg->functionname().c_str()));
				
				UChanneldMetrics* Metrics = GEngine->GetEngineSubsystem<UChanneldMetrics>();
				Metrics->RedirectedRPCs_Counter->Increment();
#if !UE_BUILD_SHIPPING
				Metrics->RedirectedRPCs->Add({{"funcName", Msg->functionname()}}).Increment();
#endif

				OnSentRPC(*Msg);
				return;
			}
			
			UE_LOG(LogChanneld, Warning, TEXT("Unable to redirect RPC as the mapping to the target channel doesn't exists, netId: %d"), Msg->targetobj().netguid());
		}
		else
		{
			UE_LOG(LogChanneld, Warning, TEXT("Unable to redirect RPC as the view doesn't exist."));
		}
	}

	GEngine->GetEngineSubsystem<UChanneldMetrics>()->OnDroppedRPC(Msg->functionname());
}

// Called only on client or the destination server of a handover.
void UChanneldNetDriver::NotifyActorChannelOpen(UActorChannel* Channel, AActor* Actor)
{
	UE_LOG(LogChanneld, Verbose, TEXT("[Client] ActorChannelOpen: %s"), *GetNameSafe(Actor));
	// Actor's RPC can be invoked before BeginPlay(), so we need to make sure the replicators have been created at this moment.
	if (auto Comp = Actor->GetComponentByClass(UChanneldReplicationComponent::StaticClass()))
	{
		auto RepComp = Cast<UChanneldReplicationComponent>(Comp);
		RepComp->InitOnce();
	}
}

// Runs only on server
void UChanneldNetDriver::OnClientPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	auto NewPlayerConn = Cast<UChanneldNetConnection>(NewPlayer->GetNetConnection());
	if (NewPlayerConn == nullptr)
	{
		UE_LOG(LogChanneld, Error, TEXT("PlayerController doesn't have the UChanneldNetConnection. Failed to spawn the objects in the clients."));
		return;
	}
	
	ChannelDataView->OnClientPostLogin(GameMode, NewPlayer, NewPlayerConn);
}

void UChanneldNetDriver::TickDispatch(float DeltaTime)
{
	//Super::TickDispatch(DeltaTime);
	UNetDriver::TickDispatch(DeltaTime);

	if (IsValid(ConnToChanneld) && ConnToChanneld->IsConnected())
		ConnToChanneld->TickIncoming();

	int NumToProcess = UnprocessedRPCs.Num();
	for (int i = 0; i < NumToProcess; i++)
	{
		TSharedPtr<unrealpb::RemoteFunctionMessage> RpcMsg = *UnprocessedRPCs.GetData();
		UnprocessedRPCs.RemoveAt(0);
		HandleCustomRPC(RpcMsg);
	}
}

// Won't trigger until ClientConnections.Num() > 0
int32 UChanneldNetDriver::ServerReplicateActors(float DeltaSeconds)
{
	int32 Result = 0;

	if (GetMutableDefault<UChanneldSettings>()->bSkipCustomReplication)
	{
		Result = Super::ServerReplicateActors(DeltaSeconds);
	}
	else
	{
		for (int32 i = 0; i < ClientConnections.Num(); i++)
		{
			UChanneldNetConnection* Connection = CastChecked<UChanneldNetConnection>(ClientConnections[i]);
			if (Connection->PlayerController && Connection->PlayerController->GetViewTarget())
			{
				// Trigger ClientMoveResponse RPC
				Connection->PlayerController->SendClientAdjustment();
			}
		}

		if (ChannelDataView.IsValid())
		{
			Result = ChannelDataView->SendAllChannelUpdates();
		}
	}
	
	return Result;
}

void UChanneldNetDriver::ProcessRemoteFunction(class AActor* Actor, class UFunction* Function, void* Parameters, struct FOutParmRec* OutParms, struct FFrame* Stack, class UObject* SubObject /*= nullptr*/)
{
	const FName FuncFName = Function->GetFName();
	const FString FuncName = FuncFName.ToString();
	const bool bShouldLog = FuncFName != ServerMovePackedFuncName && FuncFName != ClientMoveResponsePackedFuncName && FuncFName != ServerUpdateCameraFuncName;
	UE_CLOG(bShouldLog, LogChanneld, Verbose, TEXT("Sending RPC %s::%s, SubObject: %s"), *Actor->GetName(), *FuncName, *GetNameSafe(SubObject));
	/*
	if (Function->GetFName() == FName("ServerToggleRotation"))
	{
		//UE_DEBUG_BREAK();
		UE_LOG(LogChanneld, VeryVerbose, TEXT(""));
	}
	*/
	
	if (Actor->IsActorBeingDestroyed())
	{
		UE_LOG(LogNet, Warning, TEXT("UNetDriver::ProcessRemoteFunction: Remote function %s called from actor %s while actor is being destroyed. Function will not be processed."), *FuncName, *Actor->GetName());
		return;
	}

	if (!ChannelDataView.IsValid())
	{
		UE_LOG(LogChanneld, Warning, TEXT("Failed to send RPC %s::%s as the view doesn't exist"), *Actor->GetName(), *FuncName);
		return;
	}
	
	if (!GetMutableDefault<UChanneldSettings>()->bSkipCustomReplication)
	{
		auto RepComp = Cast<UChanneldReplicationComponent>(Actor->FindComponentByClass(UChanneldReplicationComponent::StaticClass()));
		if (RepComp)
		{
			UObject* TargetObject = Actor;
			FString SubObjectPathName = "";
			if (SubObject)
			{
				TargetObject = SubObject;
				SubObjectPathName = SubObject->GetName();
			}
			
			bool bSuccess = true;
			auto ParamsMsg = RepComp->SerializeFunctionParams(TargetObject, Function, Parameters, OutParms, bSuccess);
			if (bSuccess)
			{
				UE_CLOG(bShouldLog && ParamsMsg.IsValid(), LogChanneld, VeryVerbose, TEXT("Serialized RPC parameters: %s"), UTF8_TO_TCHAR(ParamsMsg->DebugString().c_str()));
				
				Channeld::ChannelId OwningChId = ChannelDataView->GetOwningChannelId(Actor);

				// Server -> Client multicast RPC
				if (ConnToChanneld->IsServer() && (Function->FunctionFlags & FUNC_NetMulticast))
				{
					if (ChannelDataView->SendMulticastRPC(Actor, FuncName, ParamsMsg, SubObjectPathName))
					{
						return;
					}
				}
				// Client or authoritative server sends the RPC directly
				else if (ConnToChanneld->IsClient() || Actor->HasAuthority() || ConnToChanneld->OwnedChannels.Contains(OwningChId))
				{
					UChanneldNetConnection* NetConn = ConnToChanneld->IsClient() ? GetServerConnection() : Cast<UChanneldNetConnection>(Actor->GetNetConnection());
					if (NetConn)
					{
						NetConn->SendRPCMessage(Actor, FuncName, ParamsMsg, OwningChId, SubObjectPathName);
						return;
					}
					UE_LOG(LogChanneld, Warning, TEXT("Failed to send RPC %s::%s as the actor doesn't have any NetConn"), *Actor->GetName(), *FuncName);
				}
				// Non-authoritative server forwards the RPC to the server that has authority over the actor (channel owner)
				else
				{
					unrealpb::RemoteFunctionMessage RpcMsg;
					RpcMsg.mutable_targetobj()->set_netguid(GuidCache->GetNetGUID(Actor).Value);
					RpcMsg.set_functionname(TCHAR_TO_UTF8(*FuncName), FuncName.Len());
					if (!SubObjectPathName.IsEmpty())
					{
						RpcMsg.set_subobjectpath(TCHAR_TO_UTF8(*SubObjectPathName), SubObjectPathName.Len());
					}
					if (ParamsMsg)
					{
						RpcMsg.set_paramspayload(ParamsMsg->SerializeAsString());
						UE_LOG(LogChanneld, VeryVerbose, TEXT("Serialized RPC parameters to %llu bytes"), RpcMsg.paramspayload().size());
					}
					ConnToChanneld->Broadcast(OwningChId, unrealpb::RPC, RpcMsg, channeldpb::SINGLE_CONNECTION);
					UE_LOG(LogChanneld, Log, TEXT("Forwarded RPC %s::%s to the owner of channel %d"), *Actor->GetName(), *FuncName, OwningChId);
					OnSentRPC(RpcMsg);
					return;
				}
			}
			else
			{
				UE_LOG(LogChanneld, Warning, TEXT("Failed to serialize RPC function params: %s::%s"), *Actor->GetName(), *FuncName);
			}
		}
	}
	else
	{
		UE_LOG(LogChanneld, Warning, TEXT("Can't find the ReplicationComponent to serialize RPC: %s::%s"), *Actor->GetName(), *FuncName);
	}

	GEngine->GetEngineSubsystem<UChanneldMetrics>()->OnDroppedRPC(std::string(TCHAR_TO_UTF8(*FuncName)));
	
	// Fallback to native RPC
	Super::ProcessRemoteFunction(Actor, Function, Parameters, OutParms, Stack, SubObject);
}

void UChanneldNetDriver::OnSentRPC(const unrealpb::RemoteFunctionMessage& RpcMsg)
{
	UChanneldMetrics* Metrics = GEngine->GetEngineSubsystem<UChanneldMetrics>();
	Metrics->SentRPCs_Counter->Increment();
#if !UE_BUILD_SHIPPING
	Metrics->SentRPCs->Add({{"funcName", RpcMsg.functionname()}}).Increment();
#endif
}

void UChanneldNetDriver::ReceivedRPC(AActor* Actor, const FName& FunctionName, const std::string& ParamsPayload, bool& bDeferredRPC, UObject* SubObject)
{
	const bool bShouldLog = FunctionName != ServerMovePackedFuncName && FunctionName != ClientMoveResponsePackedFuncName && FunctionName != ServerUpdateCameraFuncName;
	UE_CLOG(bShouldLog,	LogChanneld, Verbose, TEXT("Received RPC %s::%s"), *Actor->GetName(), *FunctionName.ToString());

	
	UObject* Obj = SubObject != nullptr ? SubObject : Actor;
	UFunction* Function = Obj->FindFunction(FunctionName);
	if (!Function)
	{
		UE_LOG(LogChanneld, Error, TEXT("RPC function %s doesn't exist on Obj %s"), *FunctionName.ToString(), *Actor->GetName());
		return;
	}

	if (Actor->GetLocalRole() <= ENetRole::ROLE_SimulatedProxy)
	{
		// Simulated proxies can't process server or client RPCs
		if ((Function->FunctionFlags & FUNC_NetClient) || (Function->FunctionFlags & FUNC_NetServer))
		{
			UE_LOG(LogChanneld, Warning, TEXT("Local role has no authroity to process server or client RPC %s::%s"), *Actor->GetName(), *FunctionName.ToString());
			return;
		}
	}

	auto RepComp = Cast<UChanneldReplicationComponent>(Actor->FindComponentByClass(UChanneldReplicationComponent::StaticClass()));
	if (RepComp)
	{
		bool bSuccess = true;
		TSharedPtr<void> Params = RepComp->DeserializeFunctionParams(Obj, Function, ParamsPayload, bSuccess, bDeferredRPC);
		if (bDeferredRPC)
		{
			return;
		}

		if (bSuccess)
		{
			Obj->ProcessEvent(Function, Params.Get());
		}
		else
		{
			UE_LOG(LogChanneld, Warning, TEXT("Failed to deserialize function parameters of RPC %s::%s"), *Actor->GetName(), *FunctionName.ToString());
		}
	}
}

void UChanneldNetDriver::TickFlush(float DeltaSeconds)
{
	// Trigger the callings of ServerReplicateActors() and LowLevelSend()
	UNetDriver::TickFlush(DeltaSeconds);

	// Send ChannelDataUpdate to channeld even if there's no client connection yet, and ServerReplicateActors() is skipped.
	if (IsServer() && ClientConnections.Num() == 0 && !bSkipServerReplicateActors)
	{
		if (!GetMutableDefault<UChanneldSettings>()->bSkipCustomReplication && ChannelDataView.IsValid())
		{
			ChannelDataView->SendAllChannelUpdates();
		}
	}

	if (ConnToChanneld && ConnToChanneld->IsConnected())
	{
		CLOCK_CYCLES(SendCycles);
		ConnToChanneld->TickOutgoing();
		UNCLOCK_CYCLES(SendCycles);
	}
}

void UChanneldNetDriver::OnChanneldAuthenticated(UChanneldConnection* _)
{
	// IMPORTANT: offset with the ConnId to avoid NetworkGUID conflicts
	const uint32 UniqueNetIdOffset = ConnToChanneld->GetConnId() << Channeld::ConnectionIdBitOffset;
	GuidCache->UniqueNetIDs[0] = UniqueNetIdOffset;
	// Static NetIDs may conflict on the spatial servers, so we need to offset them as well.
	GuidCache->UniqueNetIDs[1] = UniqueNetIdOffset;

	if (ConnToChanneld->IsClient())
	{
		auto MyServerConnection = GetServerConnection();
		MyServerConnection->RemoteAddr = ConnIdToAddr(ConnToChanneld->GetConnId());
		MyServerConnection->bChanneldAuthenticated = true;

		if (*LowLevelSendToChannelId != Channeld::InvalidChannelId)
		{
			MyServerConnection->FlushUnauthData();
		}
		// Otherwise, wait OnSetLowLevelSendChannelId event to flush the data.
	}
}

// Triggered by UWorld::DestroyActor, for replicated actors only.
void UChanneldNetDriver::NotifyActorDestroyed(AActor* Actor, bool IsSeamlessTravel)
{
	if (GetMutableDefault<UChanneldSettings>()->bSkipCustomReplication)
	{
		Super::NotifyActorDestroyed(Actor, IsSeamlessTravel);
		return;
	}

	FNetworkGUID NetId = GuidCache->GetNetGUID(Actor);
	if (NetId.IsValid() && ChannelDataView.IsValid())
	{
		// Only authoritative server should send destroy to clients
		if (IsServer() && Actor->HasAuthority() && Actor->GetIsReplicated())
		{
			ChannelDataView->SendDestroyToClients(Actor, NetId);
		}
		
		ChannelDataView->OnDestroyedActor(Actor, NetId);
	}
	else
	{
		// UE_LOG(LogChanneld, Warning, TEXT("ChannelDataView failed to handle the destroy of %s, netId: %d"), *GetNameSafe(Actor), NetId.Value);
	}
	
	//~ Begin copy of UNetDriver::NotifyActorDestroyed
	if (ServerConnection)
	{
		ServerConnection->NotifyActorDestroyed(Actor);
	}
	else
	{
		for( int32 i=ClientConnections.Num()-1; i>=0; i-- )
		{
			UNetConnection* Connection = ClientConnections[i];
			UActorChannel* Channel = Connection->FindActorChannelRef(Actor);
			if (Channel && Channel->OpenedLocally)
			{
				// check(Channel->OpenedLocally);
				Channel->bClearRecentActorRefs = false;
				Channel->Close(EChannelCloseReason::Destroyed);
			}
			else
			{
				Connection->RemoveActorChannel(Actor);
			}
			Connection->NotifyActorDestroyed(Actor);
		}
	}
	
	RemoveNetworkActor(Actor);
	//~ End copy of UNetDriver::NotifyActorDestroyed
}
