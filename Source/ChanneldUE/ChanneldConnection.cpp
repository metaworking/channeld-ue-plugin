#include "ChanneldConnection.h"
#include "ChanneldNetDriver.h"
#include "ChanneldSettings.h"
#include "ChanneldMetrics.h"
#include "SocketSubsystem.h"

//DEFINE_LOG_CATEGORY(LogChanneld);

void UChanneldConnection::Initialize(FSubsystemCollectionBase& Collection)
{
	// Command line arguments can override the INI settings
	const TCHAR* CmdLine = FCommandLine::Get();
	if (FParse::Value(CmdLine, TEXT("ReceiveBufferSize="), ReceiveBufferSize))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed ReceiveBufferSize from CLI: %d"), ReceiveBufferSize);
	}
	if (FParse::Value(CmdLine, TEXT("SendBufferSize="), SendBufferSize))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed SendBufferSize from CLI: %d"), SendBufferSize);
	}
	if (FParse::Bool(CmdLine, TEXT("ShowUserSpaceMessageLog="), bShowUserSpaceMessageLog))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed bShowUserSpaceMessageLog from CLI: %d"), bShowUserSpaceMessageLog);
	}
	
	if (ReceiveBufferSize < Channeld::MaxPacketSize)
	{
		ReceiveBufferSize = Channeld::MaxPacketSize;
	}
	if (SendBufferSize < Channeld::MaxPacketSize)
	{
		SendBufferSize = Channeld::MaxPacketSize;
	}
	ReceiveBuffer = new uint8[ReceiveBufferSize];

	// StubId=0 is reserved.
	RpcCallbacks.Add(0, nullptr);

	UserSpaceMessageHandlerEntry = MessageHandlerEntry();
	UserSpaceMessageHandlerEntry.Msg = new channeldpb::ServerForwardMessage;
	//UserSpaceMessageHandlerEntry.Handlers.Add([&](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
	//	{
	//		HandleServerForwardMessage(Conn, ChId, Msg);
	//	});

	// The connection's internal handlers should always be called first, so we should not use the delegate as the order of its broadcast is not guaranteed.
	RegisterMessageHandler(channeldpb::AUTH, new channeldpb::AuthResultMessage(), [&](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
		{
			HandleAuth(Conn, ChId, Msg);
		});
	RegisterMessageHandler(channeldpb::CREATE_CHANNEL, new channeldpb::CreateChannelResultMessage(), [&](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
		{
			HandleCreateChannel(Conn, ChId, Msg);
		});
	RegisterMessageHandler(channeldpb::REMOVE_CHANNEL, new channeldpb::RemoveChannelMessage(), [&](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
		{
			HandleRemoveChannel(Conn, ChId, Msg);
		});
	RegisterMessageHandler(channeldpb::LIST_CHANNEL, new channeldpb::ListChannelResultMessage(), [&](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
		{
			HandleListChannel(Conn, ChId, Msg);
		});
	RegisterMessageHandler(channeldpb::SUB_TO_CHANNEL, new channeldpb::SubscribedToChannelResultMessage(), [&](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
		{
			HandleSubToChannel(Conn, ChId, Msg);
		});
	RegisterMessageHandler(channeldpb::UNSUB_FROM_CHANNEL, new channeldpb::UnsubscribedFromChannelResultMessage(), [&](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
		{
			HandleUnsubFromChannel(Conn, ChId, Msg);
		});
	RegisterMessageHandler(channeldpb::CHANNEL_DATA_UPDATE, new channeldpb::ChannelDataUpdateMessage(), [&](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
		{
			HandleChannelDataUpdate(Conn, ChId, Msg);
		});
	RegisterMessageHandler(channeldpb::CREATE_SPATIAL_CHANNEL, new channeldpb::CreateSpatialChannelsResultMessage(), [&](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
		{
			HandleCreateSpatialChannel(Conn, ChId, Msg);
		});
	RegisterMessageHandler(channeldpb::CREATE_ENTITY_CHANNEL, new channeldpb::CreateChannelResultMessage(), [&](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
		{
			HandleCreateChannel(Conn, ChId, Msg);
		});
	
	RegisterMessageHandler(channeldpb::QUERY_SPATIAL_CHANNEL, new channeldpb::QuerySpatialChannelResultMessage());
	RegisterMessageHandler(channeldpb::CHANNEL_DATA_HANDOVER, new channeldpb::ChannelDataHandoverMessage());
	RegisterMessageHandler(channeldpb::SPATIAL_REGIONS_UPDATE, new channeldpb::SpatialRegionsUpdateMessage());
}

void UChanneldConnection::Deinitialize()
{
	UserSpaceMessageHandlerEntry.Handlers.Reset();
	MessageHandlers.Reset();

//}
//
//void UChanneldConnection::BeginDestroy()
//{
//	Super::BeginDestroy();
	Disconnect(false);
}

bool UChanneldConnection::Connect(bool bInitAsClient, const FString& Host, int32 Port, FString& Error)
{
	if (IsConnected())
		return false;

	auto SocketSubsystem = ISocketSubsystem::Get();
	if (SocketSubsystem == NULL)
	{
		Error = TEXT("Unable to find socket subsystem");
		return false;
	}

	RemoteAddr = SocketSubsystem->CreateInternetAddr();
	bool IsIpValid;
	if (bInitAsClient)
	{
		ConnectionType = channeldpb::CLIENT;
	}
	else
	{
		ConnectionType = channeldpb::SERVER;
	}
	RemoteAddr->SetIp(*Host, IsIpValid);
	if (!IsIpValid)
	{
		Error = FString::Printf(TEXT("Invalid IP for client: %s"), *Host);
		return false;
	}
	RemoteAddr->SetPort(Port);

	// Create TCP socket to channeld
	Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("Connection to channeld"), RemoteAddr->GetProtocolType());

	int32 NewSize = 0;
	if (Socket->SetReceiveBufferSize(0x0fffff, NewSize))
	{
		UE_LOG(LogChanneld, Log, TEXT("Set Socket's receive buffer size to %d"), NewSize);
	}
	else
	{
		UE_LOG(LogChanneld, Error, TEXT("Failed to set Socket's receive buffer size"));
	}
	if (Socket->SetSendBufferSize(0x0fffff, NewSize))
	{
		UE_LOG(LogChanneld, Log, TEXT("Set Socket's send buffer size to %d"), NewSize);
	}
	else
	{
		UE_LOG(LogChanneld, Error, TEXT("Failed to set Socket's send buffer size"));
	}
	if(!Socket->SetNoDelay(true))
	{
		UE_LOG(LogChanneld, Error, TEXT("Failed to set Socket to NoDelay"));
	}
	if(!Socket->SetNonBlocking(true))
	{
		UE_LOG(LogChanneld, Error, TEXT("Failed to set Socket to NonBlocking"));
	}
	
	UE_LOG(LogChanneld, Log, TEXT("Connecting to channeld with addr: %s"), *RemoteAddr->ToString(true));
	bool bSocketConnected = Socket->Connect(*RemoteAddr);
	if (!ensure(bSocketConnected))
	{
		Error = FString::Printf(TEXT("SocketConnected failed"));
		return false;
	}

	if (GetMutableDefault<UChanneldSettings>()->bUseReceiveThread)
	{
		if (!ensure(StartReceiveThread()))
		{
			Error = TEXT("Start receive thread failed");
			return false;
		}
	}
	return true;
}

void UChanneldConnection::OnDisconnected()
{
	ConnId = 0;
	ConnectionType = channeldpb::NO_CONNECTION;
	RemoteAddr = nullptr;
		
	ReceiveBufferOffset = 0;
	IncomingQueue.Empty();
	OutgoingQueue.Empty();
	OutgoingQueueSize = 0;
	RpcCallbacks.Empty();
	// StubId=0 is reserved.
	RpcCallbacks.Add(0, nullptr);

	OnAuthenticated.Clear();
	OnUserSpaceMessageReceived.Clear();

	SubscribedChannels.Empty();
	OwnedChannels.Empty();
	ListedChannels.Empty();
}

void UChanneldConnection::SendDisconnectMessage(Channeld::ConnectionId InConnId)
{
	channeldpb::Packet Packet;
	channeldpb::MessagePack MsgPack;

	channeldpb::DisconnectMessage DisconnectMsg;
	DisconnectMsg.set_connid(InConnId);
	
	MsgPack.set_channelid(Channeld::GlobalChannelId);
	MsgPack.set_broadcast(channeldpb::NO_BROADCAST);
	MsgPack.set_stubid(0);
	MsgPack.set_msgtype(channeldpb::DISCONNECT);
	MsgPack.set_msgbody(DisconnectMsg.SerializeAsString());
	
	Packet.add_messages()->CopyFrom(MsgPack);
	SendDirect(Packet);
}

void UChanneldConnection::Disconnect(bool bFlushAll/* = true*/)
{
	if (!IsConnected())
		return;

	if (bFlushAll)
	{
		TickOutgoing();
	}

	OnDisconnected();

	Socket->Close();
	StopReceiveThread();

	auto SocketSubsystem = ISocketSubsystem::Get();
	if (SocketSubsystem)
	{
		SocketSubsystem->DestroySocket(Socket);
	}
	Socket = NULL;
}


void UChanneldConnection::Receive()
{
	uint32 PendingDataSize;
	if (!Socket->HasPendingData(PendingDataSize))
		return;

	int32 BytesRead;
	if (Socket->Recv(ReceiveBuffer + ReceiveBufferOffset, ReceiveBufferSize, BytesRead, ESocketReceiveFlags::None))
	{
		ReceiveBufferOffset += BytesRead;
		// if (ReceiveBufferOffset < HeaderSize)
		// {
		// 	// Unfinished packet
		// 	UE_LOG(LogChanneld, Verbose, TEXT("UChanneldConnection::Receive: unfinished packet header: %d"), BytesRead);
		// 	UChanneldMetrics* Metrics = GEngine->GetEngineSubsystem<UChanneldMetrics>();
		// 	Metrics->FragmentedPacket_Counter->Increment();
		// 	return;
		// }
		
		while (ReceiveBufferOffset > HeaderSize)
		{
			if (ReceiveBuffer[0] != 67 || ReceiveBuffer[1] != 72)
			{
				ReceiveBufferOffset = 0;
				UE_LOG(LogChanneld, Error, TEXT("Invalid tag: %d, the packet will be dropped"), ReceiveBuffer[0]);
				UChanneldMetrics* Metrics = GEngine->GetEngineSubsystem<UChanneldMetrics>();
				Metrics->DroppedPacket_Counter->Increment();
				return;
			}
			
			uint32 PacketSize = ReceiveBuffer[3] | (ReceiveBuffer[2] << 8);

			UE_LOG(LogChanneld, Warning, TEXT("ReceiveBufferOffset: %d, PacketSize:%d"), ReceiveBufferOffset,
			       PacketSize);

			if (ReceiveBufferOffset < HeaderSize + PacketSize)
			{
				// Unfinished packet
				UE_LOG(LogChanneld, Verbose,
				       TEXT("UChanneldConnection::Receive: unfinished packet body, read: %d, pos: %d/%d"), BytesRead,
				       ReceiveBufferOffset, HeaderSize + PacketSize);
				UChanneldMetrics* Metrics = GEngine->GetEngineSubsystem<UChanneldMetrics>();
				Metrics->FragmentedPacket_Counter->Increment();
				return;
			}

			// TODO: support Snappy compression

			channeldpb::Packet Packet;
			if (!Packet.ParseFromArray(ReceiveBuffer + HeaderSize, PacketSize))
			{
				ReceiveBufferOffset = 0;
				UE_LOG(LogChanneld, Error, TEXT("UChanneldConnection::Receive: Failed to parse packet, size: %d"),
				       PacketSize);
				UChanneldMetrics* Metrics = GEngine->GetEngineSubsystem<UChanneldMetrics>();
				Metrics->DroppedPacket_Counter->Increment();
				return;
			}

			// move buff
			uint32 moveLen = PacketSize + HeaderSize;
			for (int i = 0; i < ReceiveBufferOffset - moveLen; ++i)
			{
				ReceiveBuffer[i] = ReceiveBuffer[moveLen + i];
			}
			//ReceiveBuffer += moveLen;
			
			ReceiveBufferOffset -= moveLen;



			for (auto const& MessagePackData : Packet.messages())
			{
				uint32 MsgType = MessagePackData.msgtype();

				MessageHandlerEntry Entry;
				if (!MessageHandlers.Contains(MsgType))
				{
					if (MsgType >= channeldpb::USER_SPACE_START)
					{
						Entry = UserSpaceMessageHandlerEntry;
					}
					else
					{
						UE_LOG(LogChanneld, Warning, TEXT("No message handler registered for type: %d"),
						       MessagePackData.msgtype());
						continue;
					}
				}
				else
				{
					Entry = MessageHandlers[MsgType];
				}

				if (Entry.Msg == nullptr)
				{
					UE_LOG(LogChanneld, Error, TEXT("No message template registered for type: %d"),
					       MessagePackData.msgtype());
					continue;
				}

				// Always make a clone!
				google::protobuf::Message* Msg = Entry.Msg->New();
				Msg->CopyFrom(*Entry.Msg);
				if (!Msg->ParseFromString(MessagePackData.msgbody()))
				{
					UE_LOG(LogChanneld, Error, TEXT("Failed to parse message %s"),
					       UTF8_TO_TCHAR(Msg->GetTypeName().c_str()));
					continue;
				}

				MessageQueueEntry QueueEntry = {
					MsgType, Msg, MessagePackData.channelid(), MessagePackData.stubid(), Entry.Handlers, Entry.Delegate
				};
				IncomingQueue.Enqueue(QueueEntry);
			}
		}
	}
	else
	{
		OnDisconnected();
		// Handle disconnection or exception
		UE_LOG(LogChanneld, Warning, TEXT("Failed to receive data "));
	}

	// Reset read position
	// ReceiveBufferOffset = 0;
}



bool UChanneldConnection::StartReceiveThread()
{
	if (bReceiveThreadRunning)
	{
		return false;
	}
	if (ReceiveThread == nullptr)
	{
		ReceiveThread = FRunnableThread::Create(this, TEXT("Tpri_Channeld_Connection_Receive"));
	}
	return ReceiveThread != nullptr;
}

void UChanneldConnection::StopReceiveThread()
{
	Stop();
	if (ReceiveThread)
	{
		delete ReceiveThread;
		ReceiveThread = nullptr;
	}
}

bool UChanneldConnection::Init()
{
	bReceiveThreadRunning = true;
	return true;
}

uint32 UChanneldConnection::Run()
{
	while (bReceiveThreadRunning)
	{
		Receive();
		// FPlatformProcess::Sleep(0.001f);
		Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(10));
	}
	return 0;
}

void UChanneldConnection::Stop()
{
	bReceiveThreadRunning = false;
}

void UChanneldConnection::Exit()
{
	if (bReceiveThreadRunning != false)
		bReceiveThreadRunning = true;
}

uint32 UChanneldConnection::AddRpcCallback(const FChanneldMessageHandlerFunc& HandlerFunc)
{
	uint32 StubId = 0;
	while (RpcCallbacks.Contains(StubId))
	{
		StubId++;
	}

	RpcCallbacks.Add(StubId, HandlerFunc);
	return StubId;
}

void UChanneldConnection::TickIncoming()
{
	if (!bReceiveThreadRunning)
	{
		Receive();
	}
	
	MessageQueueEntry Entry;
	while (IncomingQueue.Dequeue(Entry))
	{
		if (Entry.MsgType >= channeldpb::USER_SPACE_START && !MessageHandlers.Contains(Entry.MsgType))
		{
			HandleServerForwardMessage(this, Entry.ChId, Entry.Msg, Entry.MsgType);
		}
		else
		{
			// Handler functions are called before the delegate.Broadcast()
			for (const auto& Func : Entry.Handlers)
			{
				Func(this, Entry.ChId, Entry.Msg);
			}
			Entry.Delegate.Broadcast(this, Entry.ChId, Entry.Msg);
		}

		if (Entry.StubId > 0)
		{
			auto CallbackFunc = RpcCallbacks.Find(Entry.StubId);
			if (CallbackFunc != nullptr)
			{
				UE_LOG(LogChanneld, VeryVerbose, TEXT("Handling RPC callback of %s, stubId: %d"), UTF8_TO_TCHAR(Entry.Msg->GetTypeName().c_str()), Entry.StubId);
				(*CallbackFunc)(this, Entry.ChId, Entry.Msg);
				RpcCallbacks.Remove(Entry.StubId);
			}
		}
		delete Entry.Msg;
		Entry.Msg = nullptr;
	}
}

void UChanneldConnection::TickOutgoing()
{
	if (!IsConnected())
		return;

	if (OutgoingQueue.IsEmpty())
		return;

	channeldpb::Packet Packet;
	TSharedPtr<channeldpb::MessagePack> MessagePack;
	while (OutgoingQueue.Peek(MessagePack))
	{
		uint32 MsgSize = MessagePack->ByteSizeLong();
		if (MsgSize >= Channeld::MaxPacketSize)
		{
			OutgoingQueue.Pop();
			OutgoingQueueSize--;
			UE_LOG(LogChanneld, Error, TEXT("Dropped oversized message pack: %d, type: %d, remaining in queue: %d"), MsgSize, MessagePack->msgtype(), OutgoingQueueSize);
			return;
		}

		Packet.add_messages()->CopyFrom(*MessagePack);
		if (Packet.ByteSizeLong() > Channeld::MaxPacketSize)
		{
			// Revert adding the message that causes oversize
			Packet.mutable_messages()->RemoveLast();
			UE_LOG(LogChanneld, Log, TEXT("Packet is going to be oversized: %d, message type: %d, size: %d, num in packet: %d, remaining in queue: %d"),
				(uint32)Packet.ByteSizeLong(), MessagePack->msgtype(), MsgSize, Packet.messages_size(), OutgoingQueueSize);

			if (Packet.messages_size() > 0)
			{
				SendDirect(Packet);
				Packet.Clear();
			}
			// Keep sending packets until the queue is empty.
			continue;
		}

		// Actually remove the message from the queue
		OutgoingQueue.Pop();
		OutgoingQueueSize--;
	}

	if (Packet.messages_size() > 0)
	{
		SendDirect(Packet);
	}
}

void UChanneldConnection::SendDirect(const channeldpb::Packet& Packet)
{
	uint32 PacketSize = Packet.ByteSizeLong();
	uint32 Size = HeaderSize + PacketSize;
	// TODO: Use a send buffer for all transmissions instead of temp buffer for each transmission
	uint8* PacketData = new uint8[Size];
	if (!Packet.SerializeToArray(PacketData + HeaderSize, Size))
	{
		delete[] PacketData;
		UE_LOG(LogChanneld, Error, TEXT("Failed to serialize Packet, size: %d"), Size);
		return;
	}

	// Set the header
	PacketData[0] = 67;
	PacketData[1] = 72;
	PacketData[2] = (PacketSize >> 8) & 0xff;
	PacketData[3] = (PacketSize & 0xff);
	// TODO: support Snappy compression
	PacketData[4] = 0;

	int32 BytesSent;
	bool IsSent = Socket->Send(PacketData, Size, BytesSent);
	// Free send buffer
	delete[] PacketData;
	if (!IsSent || BytesSent != Size)
	{
		FString MsgTypes;
		for (int i = 0; i < Packet.messages_size(); i++)
		{
			MsgTypes.Appendf(TEXT("%d, "), Packet.messages(i).msgtype());
		}
		UE_LOG(LogChanneld, Error, TEXT("Failed to send packet to channeld, msgTypes: %s, sent/full size: %d/%d, last packet size: %d"), *MsgTypes, BytesSent, Size, LastPacketSize);
	}
	else
	{
		LastPacketSize = PacketSize;
	}
}

void UChanneldConnection::Send(Channeld::ChannelId ChId, uint32 MsgType, google::protobuf::Message& Msg, channeldpb::BroadcastType Broadcast/* = channeldpb::NO_BROADCAST*/, const FChanneldMessageHandlerFunc& HandlerFunc/* = nullptr*/)
{
	if (ChId == Channeld::InvalidChannelId)
	{
		UE_LOG(LogChanneld, Error, TEXT("Illegal attempt to send message to invalid channel"));
		return;
	}
	
	SendRaw(ChId, MsgType, Msg.SerializeAsString(), Broadcast, HandlerFunc);

	if (MsgType < channeldpb::USER_SPACE_START)
		UE_LOG(LogChanneld, Verbose, TEXT("Send message %s to channel %d"), UTF8_TO_TCHAR(channeldpb::MessageType_Name((channeldpb::MessageType)MsgType).c_str()), ChId);
}

void UChanneldConnection::SendRaw(Channeld::ChannelId ChId, uint32 MsgType, const std::string& MsgBody, channeldpb::BroadcastType Broadcast /*= channeldpb::NO_BROADCAST*/, const FChanneldMessageHandlerFunc& HandlerFunc /*= nullptr*/)
{
	if (ChId == Channeld::InvalidChannelId)
	{
		UE_LOG(LogChanneld, Error, TEXT("Illegal attempt to send message to invalid channel"));
		return;
	}
	
	uint32 StubId = HandlerFunc != nullptr ? AddRpcCallback(HandlerFunc) : 0;

	TSharedPtr<channeldpb::MessagePack> MsgPack(new channeldpb::MessagePack);
	MsgPack->set_channelid(ChId);
	MsgPack->set_broadcast(Broadcast);
	MsgPack->set_stubid(StubId);
	MsgPack->set_msgtype(MsgType);
	MsgPack->set_msgbody(MsgBody);
	OutgoingQueue.Enqueue(MsgPack);
	OutgoingQueueSize++;

	/*
	channeldpb::MessagePack MsgPack;
	MsgPack.set_channelid(ChId);
	MsgPack.set_broadcast(Broadcast);
	MsgPack.set_stubid(StubId);
	MsgPack.set_msgtype(MsgType);
	MsgPack.set_msgbody(MsgBody);
	OutgoingQueue.Enqueue(MsgPack);
	*/

	if (MsgType >= channeldpb::USER_SPACE_START && bShowUserSpaceMessageLog)
		UE_LOG(LogChanneld, Verbose, TEXT("Send user-space message to channel %d, stubId=%d, type=%d, bodySize=%d)"), ChId, StubId, MsgType, MsgBody.size());
}

void UChanneldConnection::Forward(Channeld::ChannelId ChId, uint32 MsgType, const google::protobuf::Message& Msg, Channeld::ConnectionId ClientConnId)
{
	channeldpb::ServerForwardMessage ServerForwardMessage;
	ServerForwardMessage.set_clientconnid(ClientConnId);
	ServerForwardMessage.set_payload(Msg.SerializeAsString());
	Send(ChId, MsgType, ServerForwardMessage, channeldpb::BroadcastType::SINGLE_CONNECTION);
}

void UChanneldConnection::Broadcast(Channeld::ChannelId ChId, uint32 MsgType, const google::protobuf::Message& Msg, int BroadcastType)
{
	channeldpb::ServerForwardMessage ServerForwardMessage;
	ServerForwardMessage.set_payload(Msg.SerializeAsString());
	Send(ChId, MsgType, ServerForwardMessage, static_cast<channeldpb::BroadcastType>(BroadcastType));
}

void UChanneldConnection::HandleServerForwardMessage(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg, uint32 MsgType)
{
	auto UserSpaceMsg = static_cast<const channeldpb::ServerForwardMessage*>(Msg);
	if (!OnUserSpaceMessageReceived.IsBound())
	{
		UE_LOG(LogChanneld, Warning, TEXT("No handler for user-space message %d, channelId=%d, client connId=%d"), MsgType, ChId, UserSpaceMsg->clientconnid());
		return;
	}
	OnUserSpaceMessageReceived.Broadcast(MsgType, ChId, UserSpaceMsg->clientconnid(), UserSpaceMsg->payload());
}

template <typename MsgClass>
FChanneldMessageHandlerFunc WrapMessageHandler(const TFunction<void(const MsgClass*)>& Callback)
{
	if (Callback == nullptr)
		return nullptr;
	return [Callback](UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
	{
		Callback(static_cast<const MsgClass*>(Msg));
	};
}

void UChanneldConnection::Auth(const FString& PIT, const FString& LT, const TFunction<void(const channeldpb::AuthResultMessage*)>& Callback /*= nullptr*/)
{
	channeldpb::AuthMessage Msg;
	Msg.set_playeridentifiertoken(std::string(TCHAR_TO_UTF8(*PIT)));
	Msg.set_logintoken(std::string(TCHAR_TO_UTF8(*LT)));

	Send(Channeld::GlobalChannelId, channeldpb::AUTH, Msg, channeldpb::NO_BROADCAST, WrapMessageHandler(Callback));
}

void UChanneldConnection::CreateChannel(channeldpb::ChannelType ChannelType, const FString& Metadata, const channeldpb::ChannelSubscriptionOptions* SubOptions /*= nullptr*/, const google::protobuf::Message* Data /*= nullptr*/, const channeldpb::ChannelDataMergeOptions* MergeOptions /*= nullptr*/, const TFunction<void(const channeldpb::CreateChannelResultMessage*)>& Callback /*= nullptr*/)
{
	channeldpb::CreateChannelMessage Msg;
	Msg.set_channeltype(ChannelType);
	Msg.set_metadata(TCHAR_TO_UTF8(*Metadata));
	if (SubOptions != nullptr)
		Msg.mutable_suboptions()->MergeFrom(*SubOptions);
	if (Data != nullptr)
		Msg.mutable_data()->PackFrom(*Data);
	if (MergeOptions != nullptr)
		Msg.mutable_mergeoptions()->MergeFrom(*MergeOptions);

	Send(Channeld::GlobalChannelId, channeldpb::CREATE_CHANNEL, Msg, channeldpb::NO_BROADCAST, WrapMessageHandler(Callback));
}

void UChanneldConnection::CreateSpatialChannel(const FString& Metadata, const channeldpb::ChannelSubscriptionOptions* SubOptions /*= nullptr*/, const google::protobuf::Message* Data /*= nullptr*/, const channeldpb::ChannelDataMergeOptions* MergeOptions /*= nullptr*/, const TFunction<void(const channeldpb::CreateSpatialChannelsResultMessage*)>& Callback /*= nullptr*/)
{
	channeldpb::CreateChannelMessage Msg;
	Msg.set_channeltype(channeldpb::SPATIAL);
	Msg.set_metadata(TCHAR_TO_UTF8(*Metadata));
	if (SubOptions != nullptr)
		Msg.mutable_suboptions()->MergeFrom(*SubOptions);
	if (Data != nullptr)
		Msg.mutable_data()->PackFrom(*Data);
	if (MergeOptions != nullptr)
		Msg.mutable_mergeoptions()->MergeFrom(*MergeOptions);

	Send(Channeld::GlobalChannelId, channeldpb::CREATE_CHANNEL, Msg, channeldpb::NO_BROADCAST, WrapMessageHandler(Callback));
}

void UChanneldConnection::CreateEntityChannel(Channeld::ChannelId ChId, UObject* Entity, uint32 EntityId,
	const FString& Metadata, const channeldpb::ChannelSubscriptionOptions* SubOptions, 
	const google::protobuf::Message* Data, const channeldpb::ChannelDataMergeOptions* MergeOptions,
	const TFunction<void(const channeldpb::CreateChannelResultMessage*)>& Callback)
{
	channeldpb::CreateEntityChannelMessage CreateEntityMsg;
	CreateEntityMsg.set_entityid(EntityId);
	if (!Metadata.IsEmpty())
	{
		CreateEntityMsg.set_metadata(TCHAR_TO_UTF8(*Metadata));
	}
	if (SubOptions != nullptr)
	{
		CreateEntityMsg.mutable_suboptions()->MergeFrom(*SubOptions);
	}
	if (Data != nullptr)
	{
		CreateEntityMsg.mutable_data()->PackFrom(*Data);
	}
	if (MergeOptions != nullptr)
	{
		CreateEntityMsg.mutable_mergeoptions()->MergeFrom(*MergeOptions);
	}
	
	if (const AActor* Actor = Cast<AActor>(Entity))
	{
		CreateEntityMsg.set_iswellknown(Actor->bAlwaysRelevant);
	}
	
	Send(ChId, channeldpb::CREATE_ENTITY_CHANNEL, CreateEntityMsg, channeldpb::NO_BROADCAST, WrapMessageHandler(Callback));
}

void UChanneldConnection::RemoveChannel(uint32 ChannelToRemove, const TFunction<void(const channeldpb::RemoveChannelMessage*)>& Callback)
{
	channeldpb::RemoveChannelMessage Msg;
	Msg.set_channelid(ChannelToRemove);
	Send(0, channeldpb::REMOVE_CHANNEL, Msg, channeldpb::NO_BROADCAST, WrapMessageHandler(Callback));
}

void UChanneldConnection::ListChannel(channeldpb::ChannelType TypeFilter /*= channeldpb::UNKNOWN*/, const TArray<FString>* MetadataFilters /*= null*/, const TFunction<void(const channeldpb::ListChannelResultMessage*)>& Callback /*= null*/)
{
	channeldpb::ListChannelMessage ListMsg;
	ListMsg.set_typefilter(TypeFilter);
	if (MetadataFilters != nullptr)
	{
		for (const FString& MetadataFilter : *MetadataFilters)
		{
			ListMsg.add_metadatafilters(TCHAR_TO_ANSI(*MetadataFilter));
		}
	}
	Send(Channeld::GlobalChannelId, channeldpb::LIST_CHANNEL, ListMsg, channeldpb::NO_BROADCAST, WrapMessageHandler(Callback));
}

void UChanneldConnection::SubToChannel(Channeld::ChannelId ChId, const channeldpb::ChannelSubscriptionOptions* SubOptions /*= nullptr*/, const TFunction<void(const channeldpb::SubscribedToChannelResultMessage*)>& Callback /*= nullptr*/)
{
	SubConnectionToChannel(GetConnId(), ChId, SubOptions, Callback);
}

void UChanneldConnection::SubConnectionToChannel(Channeld::ConnectionId TargetConnId, Channeld::ChannelId ChId, const channeldpb::ChannelSubscriptionOptions* SubOptions /*= nullptr*/, const TFunction<void(const channeldpb::SubscribedToChannelResultMessage*)>& Callback /*= nullptr*/)
{
	channeldpb::SubscribedToChannelMessage Msg;
	Msg.set_connid(TargetConnId);
	if (SubOptions != nullptr)
		Msg.mutable_suboptions()->MergeFrom(*SubOptions);

	Send(ChId, channeldpb::SUB_TO_CHANNEL, Msg, channeldpb::NO_BROADCAST, WrapMessageHandler(Callback));
}

void UChanneldConnection::UnsubFromChannel(Channeld::ChannelId ChId, const TFunction<void(const channeldpb::UnsubscribedFromChannelResultMessage*)>& Callback /*= nullptr*/)
{
	UnsubConnectionFromChannel(GetConnId(), ChId, Callback);
}

void UChanneldConnection::UnsubConnectionFromChannel(Channeld::ConnectionId TargetConnId, Channeld::ChannelId ChId, const TFunction<void(const channeldpb::UnsubscribedFromChannelResultMessage*)>& Callback /*= nullptr*/)
{
	channeldpb::UnsubscribedFromChannelMessage Msg;
	Msg.set_connid(TargetConnId);

	Send(ChId, channeldpb::UNSUB_FROM_CHANNEL, Msg, channeldpb::NO_BROADCAST, WrapMessageHandler(Callback));
}

void UChanneldConnection::QuerySpatialChannel(const TArray<FVector>& Positions, const TFunction<void(const channeldpb::QuerySpatialChannelResultMessage*)>& Callback)
{
	channeldpb::QuerySpatialChannelMessage Msg;
	for (auto& Pos : Positions)
	{
		channeldpb::SpatialInfo* SpatialInfo = Msg.add_spatialinfo();
		// Swap the Y and Z as UE uses the Z-Up rule but channeld uses the Y-up rule.
		SpatialInfo->set_x(Pos.X);
		SpatialInfo->set_y(Pos.Z);
		SpatialInfo->set_z(Pos.Y);
	}
	Send(Channeld::GlobalChannelId, channeldpb::QUERY_SPATIAL_CHANNEL, Msg, channeldpb::NO_BROADCAST, WrapMessageHandler(Callback));
}

void UChanneldConnection::AddToEntityGroup(Channeld::EntityId EntityChId, channeldpb::EntityGroupType GroupType, const TArray<Channeld::EntityId> EntitiesToAdd)
{
	channeldpb::AddEntityGroupMessage addMsg;
	addMsg.set_type(GroupType);
	for (auto& EntityId : EntitiesToAdd)
	{
		addMsg.add_entitiestoadd(EntityId);
	}
	
	Send(EntityChId, channeldpb::ENTITY_GROUP_ADD, addMsg);
}

void UChanneldConnection::RemoveFromEntityGroup(Channeld::EntityId EntityChId, channeldpb::EntityGroupType GroupType, const TArray<Channeld::EntityId> EntitiesToRemove)
{
	channeldpb::RemoveEntityGroupMessage removeMsg;
	removeMsg.set_type(GroupType);
	for (auto& EntityId : EntitiesToRemove)
	{
		removeMsg.add_entitiestoremove(EntityId);
	}

	Send(EntityChId, channeldpb::ENTITY_GROUP_REMOVE, removeMsg);
}

void UChanneldConnection::HandleAuth(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
{
	auto ResultMsg = static_cast<const channeldpb::AuthResultMessage*>(Msg);
	if (ResultMsg->result() == channeldpb::AuthResultMessage_AuthResult_SUCCESSFUL)
	{
		// Unauthorized yet
		if (ConnId == 0)
		{
			ConnId = ResultMsg->connid();
			CompressionType = ResultMsg->compressiontype();
			OnAuthenticated.Broadcast(this);
		}
		else
		{
			// The master server receives other connection's AuthResultMessage
		}
	}
	else
	{
		UE_LOG(LogChanneld, Error, TEXT("Failed to get authorized by channeld"));
	}
}

void UChanneldConnection::HandleCreateChannel(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
{
	auto ResultMsg = static_cast<const channeldpb::CreateChannelResultMessage*>(Msg);
	if (ResultMsg->ownerconnid() == GetConnId())
	{
		FOwnedChannelInfo ChannelInfo;
		ChannelInfo.ChannelType = static_cast<EChanneldChannelType>(ResultMsg->channeltype());
		ChannelInfo.ChannelId = ResultMsg->channelid();
		ChannelInfo.Metadata = FString(UTF8_TO_TCHAR(ResultMsg->metadata().c_str()));
		ChannelInfo.OwnerConnId = ResultMsg->ownerconnid();
		OwnedChannels.Add(ResultMsg->channelid(), ChannelInfo);
	}
}

void UChanneldConnection::HandleRemoveChannel(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
{
	auto RemoveMsg = static_cast<const channeldpb::RemoveChannelMessage*>(Msg);
	SubscribedChannels.Remove(RemoveMsg->channelid());
	OwnedChannels.Remove(RemoveMsg->channelid());
	ListedChannels.Remove(RemoveMsg->channelid());
}

void UChanneldConnection::HandleListChannel(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
{
	auto ResultMsg = static_cast<const channeldpb::ListChannelResultMessage*>(Msg);
	for (auto ChannelInfo : ResultMsg->channels())
	{
		FListedChannelInfo ListedChannelInfo;
		ListedChannelInfo.ChannelId = ChannelInfo.channelid();
		ListedChannelInfo.ChannelType = static_cast<EChanneldChannelType>(ChannelInfo.channeltype());
		ListedChannelInfo.Metadata = FString(UTF8_TO_TCHAR(ChannelInfo.metadata().c_str()));
		ListedChannels.Add(ChannelInfo.channelid(), ListedChannelInfo);
	}
}

void UChanneldConnection::HandleSubToChannel(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
{
	auto SubMsg = static_cast<const channeldpb::SubscribedToChannelResultMessage*>(Msg);
	if (SubMsg->connid() == Conn->GetConnId())
	{
		FSubscribedChannelInfo* ExistingSub = SubscribedChannels.Find(ChId);
		if (ExistingSub != nullptr)
		{
			if (SubMsg->has_suboptions())
			{
				UE_LOG(LogChanneld, Verbose, TEXT("Merged the SubOptions of the channel %d: %s"), ChId, UTF8_TO_TCHAR(SubMsg->suboptions().ShortDebugString().c_str()));
				// Merge the SubOptions if the subscription already exists
				ExistingSub->Merge(*SubMsg);
			}
		}
		else
		{
			FSubscribedChannelInfo SubscribedInfo;
			SubscribedInfo.Merge(*SubMsg);
			SubscribedChannels.Add(ChId, SubscribedInfo);
		}
	}
	else
	{
		// Other than the channel owner. Could be Master server.
		FOwnedChannelInfo* ExistingOwnedChannel = OwnedChannels.Find(ChId);
		if (ExistingOwnedChannel)
		{
			FSubscribedChannelInfo SubscribedInfo;
			SubscribedInfo.Merge(*SubMsg);
			ExistingOwnedChannel->Subscribeds.Add(SubMsg->connid(), SubscribedInfo);
		}
	}
}

void UChanneldConnection::HandleUnsubFromChannel(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
{
	auto UnsubMsg = static_cast<const channeldpb::UnsubscribedFromChannelResultMessage*>(Msg);
	if (UnsubMsg->connid() == Conn->GetConnId())
	{
		SubscribedChannels.Remove(ChId);
	}
	else
	{
		// Other than then channel owner. Could be Master server.
		FOwnedChannelInfo* ExistingOwnedChannel = OwnedChannels.Find(ChId);
		if (ExistingOwnedChannel)
		{
			ExistingOwnedChannel->Subscribeds.Remove(UnsubMsg->connid());
		}
	}
}

void UChanneldConnection::HandleChannelDataUpdate(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
{

}

void UChanneldConnection::HandleCreateSpatialChannel(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg)
{
	auto ResultMsg = static_cast<const channeldpb::CreateSpatialChannelsResultMessage*>(Msg);
	if (ResultMsg->ownerconnid() == GetConnId())
	{
		for (const Channeld::ChannelId& SpatialChId : ResultMsg->spatialchannelid())
		{
			FOwnedChannelInfo ChannelInfo;
			ChannelInfo.ChannelType = EChanneldChannelType::ECT_Spatial;
			ChannelInfo.ChannelId = SpatialChId;
			ChannelInfo.Metadata = FString(UTF8_TO_TCHAR(ResultMsg->metadata().c_str()));
			ChannelInfo.OwnerConnId = ResultMsg->ownerconnid();
			OwnedChannels.Add(SpatialChId, ChannelInfo);
		}
	}
}
