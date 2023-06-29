#pragma once

#include "CoreMinimal.h"
#include "Engine/NetConnection.h"
#include "ChanneldTypes.h"
#include "unreal_common.pb.h"
#include "ChanneldNetConnection.generated.h"

DECLARE_EVENT_TwoParams(UChanneldNetConnection, FPlayerEnterSpatialChannelEvent, UChanneldNetConnection*, Channeld::ChannelId);

UCLASS(transient, config=ChanneldUE)
class CHANNELDUE_API UChanneldNetConnection : public UNetConnection
{
	GENERATED_BODY()

public:

	// Constructors.
	UChanneldNetConnection(const FObjectInitializer& ObjectInitializer);

	virtual void InitBase(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;
	virtual void InitLocalConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;
	virtual void InitRemoteConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, const class FInternetAddr& InRemoteAddr, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;
	virtual void LowLevelSend(void* Data, int32 CountBits, FOutPacketTraits& Traits) override;
	virtual FString LowLevelGetRemoteAddress(bool bAppendPort = false) override;
	virtual FString LowLevelDescribe() override;
	virtual void CleanUp() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void ReceivedRawPacket(void* Data, int32 Count) override;

	FORCEINLINE Channeld::ConnectionId GetConnId() const
	{
		if (RemoteAddr.IsValid())
		{
			uint32 ConnId;
			RemoteAddr->GetIp(ConnId);
			return ConnId;
		}
		return 0;
	}

	Channeld::ChannelId GetSendToChannelId();
	
	// Send data between UE client and sever via channeld. MsgType should be in user space (>= 100).
	void SendData(uint32 MsgType, const uint8* DataToSend, int32 DataSize, Channeld::ChannelId ChId = Channeld::InvalidChannelId);
	// Send message between UE client and sever via channeld. MsgType should be in user space (>= 100).
	void SendMessage(uint32 MsgType, const google::protobuf::Message& Msg, Channeld::ChannelId ChId = Channeld::InvalidChannelId);
	bool HasSentSpawn(UObject* Object) const;
	void SetSentSpawned(const FNetworkGUID NetId);
	/**
	 * @brief Send SpawnObjectMessage to the connection.
	 * @param Object The object has been spawned on the server.
	 * @param Role The LocalRole to set when the actor is spawn remotely. If not specified, the role will not be set.
	 * @param OwningChannelId The channel that the object belongs to. If not set, the NetId-ChannelId mapping in the view will be use. If still not found, the message will be queued until the mapping has been set.
	 * @param OwningConnId The connection that owns the object. It will be used remotely to amend the role (see #ChanneldUtils::SetActorRoleByOwningConnId). If not set, the amendment will not happen.
	 * @param Location The actor's location on the server. Only used when the actor is in spatial channel. channeld uses the location to amend the OwningChannelId. If not set or in spatial channel, the amendment will not happen.
	 */
	void SendSpawnMessage(UObject* Object, ENetRole Role = ENetRole::ROLE_None, uint32 OwningChannelId = Channeld::InvalidChannelId, uint32 OwningConnId = 0, FVector* Location = nullptr);
	void SendDestroyMessage(UObject* Object, EChannelCloseReason Reason = EChannelCloseReason::Destroyed);
	void SendRPCMessage(AActor* Actor, const FString& FuncName, TSharedPtr<google::protobuf::Message> ParamsMsg = nullptr, Channeld::ChannelId ChId = Channeld::InvalidChannelId, const FString& SubObjectPath = "");
	// Flush the handshake packets that are queued before received AuthResultMessage to the server.
	void FlushUnauthData();

	bool bDisableHandshaking = false;
	bool bInConnectionlessHandshake = false;
	bool bChanneldAuthenticated = false;

	UPROPERTY()
	class UClientInterestManager* ClientInterestManager;
	
	FPlayerEnterSpatialChannelEvent PlayerEnterSpatialChannelEvent;

private:

	//uint32 ConnId = 0;
	
	// Queue the data from LowLevelSend() when the connection and authentication to channeld is not finished yet,
	// and send them after the authentication is done.
	TQueue<TTuple<std::string*, FOutPacketTraits*>> LowLevelSendDataBeforeAuth;

	// Queued Spawn messages that don't have the object's NetId-ChannelId mapping set yet.
	TArray<TTuple<TWeakObjectPtr<UObject>, ENetRole, uint32, uint32, FVector*>> QueuedSpawnMessageTargets;

	struct FOutgoingRPC
	{
		AActor* Actor;
		FString FuncName;
		TSharedPtr<google::protobuf::Message> ParamsMsg;
		Channeld::ChannelId ChId;
	};
	// RPCs queued on the caller's side that don't have the NetId exported yet.
	TArray<FOutgoingRPC> UnexportedRPCs;
};