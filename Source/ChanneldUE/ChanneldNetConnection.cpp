#include "ChanneldNetConnection.h"
#include "ChanneldNetDriver.h"
#include "ChanneldTypes.h"
#include "Net/DataChannel.h"
#include "PacketHandler.h"
#include "PacketHandlers/StatelessConnectHandlerComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "unreal_common.pb.h"
#include "ChanneldUtils.h"
#include "ChanneldSettings.h"
#include "Interest/ClientInterestManager.h"

UChanneldNetConnection::UChanneldNetConnection(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	//MaxPacket = MaxPacketSize;
	if (GetMutableDefault<UChanneldSettings>()->bSetInternalAck)
	{
		SetInternalAck(true);
		SetReplay(false);
		SetAutoFlush(true);
	}
}

void UChanneldNetConnection::InitBase(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket /*= 0*/, int32 InPacketOverhead /*= 0*/)
{
	// Pass the call up the chain
	Super::InitBase(InDriver, InSocket, InURL, InState,
		// Use the default packet size/overhead unless overridden by a child class
		InMaxPacket == 0 ? MAX_PACKET_SIZE : InMaxPacket,
		InPacketOverhead == 0 ? Channeld::MinPacketSize : InPacketOverhead);
	
	if (bDisableHandshaking)
	{
		//DisableAddressResolution();
		// Reset the PacketHandler to remove the StatelessConnectHandler and bypass the handshake process.
		Handler.Reset(NULL);
	}

	ClientInterestManager = NewObject<UClientInterestManager>(this, UClientInterestManager::StaticClass());
}

void UChanneldNetConnection::InitLocalConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket /*= 0*/, int32 InPacketOverhead /*= 0*/)
{
	InitBase(InDriver, InSocket, InURL, InState, InMaxPacket, InPacketOverhead);

	MaxPacket = Channeld::MaxPacketSize;
	PacketOverhead = 5;
	InitSendBuffer();
}

void UChanneldNetConnection::InitRemoteConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, const class FInternetAddr& InRemoteAddr, EConnectionState InState, int32 InMaxPacket /*= 0*/, int32 InPacketOverhead /*= 0*/)
{
	InitBase(InDriver, InSocket, InURL, InState, InMaxPacket, InPacketOverhead);

	RemoteAddr = InDriver->GetSocketSubsystem()->CreateInternetAddr();
	uint32 Ip;
	int32 Port;
	InRemoteAddr.GetIp(Ip);
	InRemoteAddr.GetPort(Port);
	RemoteAddr->SetIp(Ip);
	RemoteAddr->SetPort(Port);

	MaxPacket = Channeld::MaxPacketSize;
	PacketOverhead = 10;
	InitSendBuffer();

	ClientInterestManager->ServerSetup(this);

	// This is for a client that needs to log in, setup ClientLoginState and ExpectedClientLoginMsgType to reflect that
	SetClientLoginState(EClientLoginState::LoggingIn);
	SetExpectedClientLoginMsgType(NMT_Hello);
}

void UChanneldNetConnection::LowLevelSend(void* Data, int32 CountBits, FOutPacketTraits& Traits)
{
	//Super::LowLevelSend(Data, CountBits, Traits);

	int32 DataSize = FMath::DivideAndRoundUp(CountBits, 8);
	// The packet sent to channeld before the authentication is finished (e.g. Handshake, Join) should be queued
	if (!bChanneldAuthenticated)
	{
		LowLevelSendDataBeforeAuth.Enqueue(MakeTuple(new std::string(reinterpret_cast<const char*>(Data), DataSize), &Traits));
		UE_LOG(LogChanneld, Log, TEXT("NetConnection queued unauthenticated LowLevelSendData, size: %dB"), DataSize);
	}
	else
	{
		const uint8* DataToSend = reinterpret_cast<uint8*>(Data);
		if (!bDisableHandshaking && Handler.IsValid() && !Handler->GetRawSend())
		{
			const ProcessedPacket ProcessedData = Handler->Outgoing(reinterpret_cast<uint8*>(Data), CountBits, Traits);

			if (!ProcessedData.bError)
			{
				DataToSend = ProcessedData.Data;
				CountBits = ProcessedData.CountBits;
				DataSize = FMath::DivideAndRoundUp(CountBits, 8);
			}
			else
			{
				return;
			}
		}

		bool bBlockSend = false;
#if !UE_BUILD_SHIPPING
		LowLevelSendDel.ExecuteIfBound((void*)DataToSend, DataSize, bBlockSend);
#endif

		if (!bBlockSend && DataSize > 0)
		{
			SendData(unrealpb::LOW_LEVEL, DataToSend, DataSize);
		}
	}
}

Channeld::ChannelId UChanneldNetConnection::GetSendToChannelId()
{
	return CastChecked<UChanneldNetDriver>(Driver)->GetSendToChannelId(this);
}

void UChanneldNetConnection::SendData(uint32 MsgType, const uint8* DataToSend, int32 DataSize, Channeld::ChannelId ChId)
{
	if (DataSize <= 0)
	{
		return;
	}

	if (!Driver)
	{
		UE_LOG(LogChanneld, Warning, TEXT("SendData failed as the NetConn %d has no NetDriver"), GetConnId());
		return;
	}
	
	auto ConnToChanneld = GEngine->GetEngineSubsystem<UChanneldConnection>();
	
	if (ChId == Channeld::InvalidChannelId)
	{
		ChId = GetSendToChannelId();
		if (ChId == Channeld::InvalidChannelId)
		{
			UE_LOG(LogChanneld, Warning, TEXT("UChanneldNetConnection::SendData failed as the NetConn %d has no channelId"), GetConnId());
		}
	}
	
	if (ConnToChanneld->IsServer())
	{
		channeldpb::ServerForwardMessage ServerForwardMessage;
		ServerForwardMessage.set_clientconnid(GetConnId());
		ServerForwardMessage.set_payload(DataToSend, DataSize);
		ConnToChanneld->Send(ChId, MsgType, ServerForwardMessage, channeldpb::SINGLE_CONNECTION);
	}
	else
	{
		ConnToChanneld->SendRaw(ChId, MsgType, std::string(reinterpret_cast<const char*>(DataToSend), DataSize));
	}
}

void UChanneldNetConnection::SendMessage(uint32 MsgType, const google::protobuf::Message& Msg, Channeld::ChannelId ChId)
{
	const std::string StrData = Msg.SerializeAsString();
	SendData(MsgType, reinterpret_cast<const uint8*>(StrData.data()), StrData.size(), ChId);
}

bool UChanneldNetConnection::HasSentSpawn(UObject* Object) const
{
	if (!Driver)
	{
		UE_LOG(LogChanneld, Warning, TEXT("Failed to check HasSentSpawn as the NetConn %d has no NetDriver"), GetConnId());
		return false;
	}
	
	// Already in the queue, don't send again
	for (auto& Tuple : QueuedSpawnMessageTargets)
	{
		if (Tuple.Get<0>().Get() == Object)
		{
			return true;
		}
	}
	
	const FNetworkGUID NetId = Driver->GuidCache->GetOrAssignNetGUID(Object);
	UPackageMapClient* PackageMapClient = CastChecked<UPackageMapClient>(PackageMap);
	int32* ExportCount = PackageMapClient->NetGUIDExportCountMap.Find(NetId);
	return (ExportCount != nullptr && *ExportCount > 0);
}

void UChanneldNetConnection::SendSpawnMessage(UObject* Object, ENetRole Role /*= ENetRole::None*/, uint32 OwningChannelId /*= Channeld::InvalidChannelId*/, uint32 OwningConnId /*= 0*/, FVector* Location /*= nullptr*/)
{
	if (!Driver)
	{
		UE_LOG(LogChanneld, Warning, TEXT("SendSpawnMessage failed as the NetConn %d has no NetDriver"), GetConnId());
		return;
	}
	
	const FNetworkGUID NetId = Driver->GuidCache->GetOrAssignNetGUID(Object);
	UPackageMapClient* PackageMapClient = CastChecked<UPackageMapClient>(PackageMap);
	int32* ExportCount = PackageMapClient->NetGUIDExportCountMap.Find(NetId);
	if (ExportCount != nullptr && *ExportCount > 0)
	{
		UE_LOG(LogChanneld, Verbose, TEXT("[Server] Skip sending spawn to conn %d, obj: %s"), GetConnId(), *GetNameSafe(Object));
		return;
	}

	// Check if the object has the owning ChannelId
	if (OwningChannelId == Channeld::InvalidChannelId)
	{
		auto NetDriver = CastChecked<UChanneldNetDriver>(Driver);
		if (NetDriver->ChannelDataView.IsValid())
		{
			OwningChannelId = NetDriver->ChannelDataView->GetOwningChannelId(NetId);
		}
	}
	if (OwningChannelId == Channeld::InvalidChannelId)
	{
		QueuedSpawnMessageTargets.Add(MakeTuple(Object, Role, OwningChannelId, OwningConnId, Location));
		UE_LOG(LogChanneld, Warning, TEXT("[Server] Unable to send Spawn message as there's no mapping of NetId %d -> ChannelId. Pushed to the next tick."), NetId.Value);
		return;
	}

	unrealpb::SpawnObjectMessage SpawnMsg;
	SpawnMsg.mutable_obj()->CopyFrom(*ChanneldUtils::GetRefOfObject(Object, this, true));
	SpawnMsg.set_channelid(OwningChannelId);
	if (Role > ENetRole::ROLE_None)
	{
		SpawnMsg.set_localrole(Role);
	}
	if (OwningConnId > 0)
	{
		SpawnMsg.mutable_obj()->set_owningconnid(OwningConnId);
	}
	if (Location)
	{
		SpawnMsg.mutable_location()->MergeFrom(ChanneldUtils::GetVectorPB(*Location));
	}
	SendMessage(unrealpb::SPAWN, SpawnMsg, OwningChannelId);
	UE_LOG(LogChanneld, Verbose, TEXT("[Server] Send Spawn message to conn: %d, obj: %s, netId: %d, role: %d, owning channel: %d, owning connId: %d, location: %s"),
		GetConnId(), *GetNameSafe(Object), SpawnMsg.obj().netguid(), SpawnMsg.localrole(), SpawnMsg.channelid(), SpawnMsg.obj().owningconnid(), Location ? *Location->ToCompactString() : TEXT("NULL"));

	SetSentSpawned(NetId);
}

void UChanneldNetConnection::SetSentSpawned(const FNetworkGUID NetId)
{
	if (UPackageMapClient* PackageMapClient = Cast<UPackageMapClient>(PackageMap))
	{
		int32* ExportCount = &PackageMapClient->NetGUIDExportCountMap.FindOrAdd(NetId, 0);
		(*ExportCount)++;
	}
	else
	{
		UE_LOG(LogChanneld, Error, TEXT("Failed to set sent spawned as the NetConn %d has no PackageMapClient"), GetConnId());
	}
}

void UChanneldNetConnection::SendDestroyMessage(UObject* Object, EChannelCloseReason Reason)
{
	if (!Driver)
	{
		UE_LOG(LogChanneld, Warning, TEXT("SendDestroyMessage failed as the NetConn %d has no NetDriver"), GetConnId());
		return;
	}

	if (!PackageMap)
	{
		UE_LOG(LogChanneld, Warning, TEXT("SendDestroyMessage failed as the NetConn %d has no PackageMap"), GetConnId());
		return;
	}
	
	const FNetworkGUID NetId = Driver->GuidCache->GetNetGUID(Object);
	if (!NetId.IsValid())
	{
		return;
	}

	UPackageMapClient* PackageMapClient = CastChecked<UPackageMapClient>(PackageMap);
	int32* ExportCount = PackageMapClient->NetGUIDExportCountMap.Find(NetId);
	if (ExportCount == nullptr || *ExportCount <= 0)
	{
		UE_LOG(LogChanneld, Verbose, TEXT("[Server] Skip sending destroy to conn %d, obj: %s"), GetConnId(), *GetNameSafe(Object));
		return;
	}

	unrealpb::DestroyObjectMessage DestroyMsg;
	DestroyMsg.set_netid(NetId.Value);
	DestroyMsg.set_reason(static_cast<uint8>(EChannelCloseReason::Destroyed));
	SendMessage(unrealpb::DESTROY, DestroyMsg);
	UE_LOG(LogChanneld, Verbose, TEXT("[Server] Send Destroy message to conn: %d, obj: %s, netId: %d"), GetConnId(), *GetNameSafe(Object), NetId.Value);

	if (ExportCount != nullptr)
	{
		(*ExportCount)--;
	}
}

void UChanneldNetConnection::SendRPCMessage(AActor* Actor, const FString& FuncName, TSharedPtr<google::protobuf::Message> ParamsMsg, Channeld::ChannelId ChId, const FString& SubObjectPath)
{
	if (GetMutableDefault<UChanneldSettings>()->bQueueUnexportedActorRPC)
	{
		if (Actor->HasAuthority() && !HasSentSpawn(Actor))
		{
			/* On server, the actor should be spawned to client via OnServerSpawnedObject.
			// If the target object hasn't been spawned in the remote end yet, send the Spawn message before the RPC message.
			NetConn->SendSpawnMessage(Actor, Actor->GetRemoteRole());
			*/

			UnexportedRPCs.Add(FOutgoingRPC{Actor, FuncName, ParamsMsg, ChId});
			UE_LOG(LogChanneld, Log, TEXT("Calling RPC %s::%s while the NetConnection(%d) doesn't have the NetId exported yet. Pushed to the next tick."),
				*Actor->GetName(), *FuncName, GetConnId());
			return;
		}
	}
	
	unrealpb::RemoteFunctionMessage RpcMsg;
	// Don't send the whole UnrealObjectRef to the other side - the object spawning process goes its own way!
	// RpcMsg.mutable_targetobj()->MergeFrom(*ChanneldUtils::GetRefOfObject(Actor));
	RpcMsg.mutable_targetobj()->set_netguid(Driver->GuidCache->GetNetGUID(Actor).Value);
	RpcMsg.set_functionname(TCHAR_TO_UTF8(*FuncName), FuncName.Len());
	if (!SubObjectPath.IsEmpty())
	{
		RpcMsg.set_subobjectpath(TCHAR_TO_UTF8(*SubObjectPath), SubObjectPath.Len());
	}
	if (ParamsMsg)
	{
		RpcMsg.set_paramspayload(ParamsMsg->SerializeAsString());
		UE_LOG(LogChanneld, VeryVerbose, TEXT("Serialized RPC parameters to %d bytes"), RpcMsg.paramspayload().size());
	}
	SendMessage(unrealpb::RPC, RpcMsg, ChId);

	if (auto NetDriver = Cast<UChanneldNetDriver>(Driver))
	{
		NetDriver->OnSentRPC(RpcMsg);
	}
}

FString UChanneldNetConnection::LowLevelGetRemoteAddress(bool bAppendPort /*= false*/)
{
	if (!Driver)
	{
		UE_LOG(LogChanneld, Warning, TEXT("LowLevelGetRemoteAddress failed as the NetConn %d has no NetDriver"), GetConnId());
		return TEXT("");
	}
	
	if (RemoteAddr)
	{
		if (bAppendPort)
			RemoteAddr->SetPort(GetSendToChannelId());
		return RemoteAddr->ToString(bAppendPort);
	}
	else
	{
		return bAppendPort ? FString::Printf(TEXT("0.0.0.0:%d"), GetSendToChannelId()) : TEXT("0.0.0.0");
	}
}

FString UChanneldNetConnection::LowLevelDescribe()
{
	return FString::Printf
	(
		TEXT("connId: %d, state: %s"),
		GetConnId(),
		State == USOCK_Pending ? TEXT("Pending")
		: State == USOCK_Open ? TEXT("Open")
		: State == USOCK_Closed ? TEXT("Closed")
		: TEXT("Invalid")
	);
}

void UChanneldNetConnection::CleanUp()
{
	if (ClientInterestManager)
	{
		ClientInterestManager->CleanUp();
	}
	
	Super::CleanUp();
}

void UChanneldNetConnection::Tick(float DeltaSeconds)
{
	UNetConnection::Tick(DeltaSeconds);
	
	const int RpcNum = UnexportedRPCs.Num();
	for (int i = 0; i < RpcNum; i++)
	{
		FOutgoingRPC& RPC = UnexportedRPCs[i];
		if (IsValid(RPC.Actor))
		{
			SendRPCMessage(RPC.Actor, RPC.FuncName, RPC.ParamsMsg, RPC.ChId);
		}
	}
	UnexportedRPCs.RemoveAt(0, RpcNum);

	const int SpawnMsgNum = QueuedSpawnMessageTargets.Num();
	for (int i = 0; i < SpawnMsgNum; i++)
	{
		auto& Params = QueuedSpawnMessageTargets[i];
		if (Params.Get<0>().IsValid())
		{
			SendSpawnMessage(Params.Get<0>().Get(), Params.Get<1>(), Params.Get<2>(), Params.Get<3>(), Params.Get<4>());
		}
	}
	QueuedSpawnMessageTargets.RemoveAt(0, SpawnMsgNum);

}

void UChanneldNetConnection::FlushUnauthData()
{
	while (!LowLevelSendDataBeforeAuth.IsEmpty())
	{
		TTuple<std::string*, FOutPacketTraits*> Params;
		LowLevelSendDataBeforeAuth.Dequeue(Params);
		std::string* data = Params.Get<0>();
		LowLevelSend((uint8*)data->data(), data->size() * 8, *Params.Get<1>());
		UE_LOG(LogChanneld, Log, TEXT("NetConnection %d flushed unauthenticated LowLevelSendData to channeld, size: %dB"), GetConnId(), data->size());
		delete data;
	}
}

void UChanneldNetConnection::ReceivedRawPacket(void* Data, int32 Count)
{
	if (Count == 0 || Driver == NULL)
	{
		return;
	}

	uint8* DataRef = reinterpret_cast<uint8*>(Data);
	if (bInConnectionlessHandshake)
	{
		// Process all incoming packets.
		if (Driver->ConnectionlessHandler.IsValid() && Driver->StatelessConnectComponent.IsValid() && Driver->GetSocketSubsystem() != nullptr)
		{
			TSharedPtr<FInternetAddr> IncomingAddress = RemoteAddr->Clone();
			//UE_LOG(LogNet, Log, TEXT("%s received raw packet from: %s"), Driver->IsServer() ? "Server" : "Client", *(IncomingAddress->ToString(true)));

			const ProcessedPacket UnProcessedPacket =
				Driver->ConnectionlessHandler->IncomingConnectionless(IncomingAddress, DataRef, Count);

			TSharedPtr<StatelessConnectHandlerComponent> StatelessConnect = Driver->StatelessConnectComponent.Pin();

			bool bRestartedHandshake = false;

			if (!UnProcessedPacket.bError && StatelessConnect->HasPassedChallenge(IncomingAddress, bRestartedHandshake) &&
				!bRestartedHandshake)
			{
				UE_LOG(LogNet, Log, TEXT("Server accepting post-challenge connection from: %s"), *(IncomingAddress->ToString(false)));
				// Set the initial packet sequence from the handshake data
				if (StatelessConnectComponent.IsValid())
				{
					int32 ServerSequence = 0;
					int32 ClientSequence = 0;
					StatelessConnect->GetChallengeSequence(ServerSequence, ClientSequence);
					InitSequence(ClientSequence, ServerSequence);
				}

				if (Handler.IsValid())
				{
					Handler->BeginHandshaking();
				}

				bInConnectionlessHandshake = false; // i.e. bPassedChallenge
				//UE_LOG(LogNet, Warning, TEXT("UChanneldNetConnection::bChallengeHandshake: %s"), *LowLevelDescribe());
				Count = FMath::DivideAndRoundUp(UnProcessedPacket.CountBits, 8);
				if (Count > 0)
				{
					DataRef = UnProcessedPacket.Data;
				}
				else
				{
					return; // NO FURTHER DATA TO PROCESS
				}
			}
			else
			{
				// WARNING: if here, it might be during (bInitialConnect) - which needs to be processed (ReceivedRawPacket)
				//return;
			}
		}
	}

	UNetConnection::ReceivedRawPacket(DataRef, Count);
}
