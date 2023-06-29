#pragma once

#include "CoreMinimal.h"
#include "ChanneldTypes.h"
#include "ChanneldConnection.h"
#include "ChannelDataInterfaces.h"
#include "ChanneldNetConnection.h"
#include "Engine/NetDriver.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "google/protobuf/message.h"
#include "Replication/ChanneldReplicationComponent.h"
#include "View/ChannelDataView.h"
#include "ChanneldNetDriver.generated.h"

/**
 * 
 */
UCLASS(transient, config=Engine)
class CHANNELDUE_API UChanneldNetDriver : public UNetDriver
{
	GENERATED_BODY()

public:

	// Constructors.
	UChanneldNetDriver(const FObjectInitializer& ObjectInitializer);

	//~ Begin UObject Interface
	virtual void PostInitProperties() override;
	//~ End UObject Interface

	//~ Begin UNetDriver Interface.
	virtual bool IsAvailable() const override;
	// Always use UChanneldNetConnection
	virtual bool InitConnectionClass() override;
	// Connect to channeld
	virtual bool InitBase(bool bInitAsClient, FNetworkNotify* InNotify, const FURL& URL, bool bReuseAddressAndPort, FString& Error) override;
	// Create NetConnection for client (ServerConnection)
	virtual bool InitConnect(FNetworkNotify* InNotify, const FURL& ConnectURL, FString& Error) override;
	// Create message handlers for server
	virtual bool InitListen(FNetworkNotify* InNotify, FURL& LocalURL, bool bReuseAddressAndPort, FString& Error) override;
	// Receive packets from channeld by calling ChanneldConnection.TickIncoming()
	virtual void TickDispatch(float DeltaTime) override;
	// Send packets to channeld by calling ChanneldConnection.TickOutgoing()
	virtual void TickFlush(float DeltaSeconds) override;

	virtual class ISocketSubsystem* GetSocketSubsystem() override;
	virtual FSocket* GetSocket();
	// Client: send raw packet to server via channeld
	// Server: send or broadcast ServerForwardMessage to client via channeld
	virtual void LowLevelSend(TSharedPtr<const FInternetAddr> Address, void* Data, int32 CountBits, FOutPacketTraits& Traits) override;
	virtual void LowLevelDestroy() override;
	virtual bool IsNetResourceValid(void) override;

	virtual void NotifyActorChannelOpen(UActorChannel* Channel, AActor* Actor) override;
	virtual int32 ServerReplicateActors(float DeltaSeconds) override;
	virtual void ProcessRemoteFunction(class AActor* Actor, class UFunction* Function, void* Parameters, struct FOutParmRec* OutParms, struct FFrame* Stack, class UObject* SubObject = nullptr) override;
	virtual void NotifyActorDestroyed(AActor* Actor, bool IsSeamlessTravel) override;
	//~ End UNetDriver Interface
	
	UChanneldNetConnection* AddChanneldClientConnection(Channeld::ConnectionId ClientConnId, Channeld::ChannelId ChId);
	void RemoveChanneldClientConnection(Channeld::ConnectionId ClientConnId);

	void ReceivedRPC(AActor* Actor, const FName& FunctionName, const std::string& ParamsPayload, bool& bDeferredRPC, UObject* SubObject = nullptr);

	UChanneldConnection* GetConnToChanneld() const { return ConnToChanneld; }

	Channeld::ConnectionId AddrToConnId(const FInternetAddr& Addr);
	TSharedRef<FInternetAddr> ConnIdToAddr(Channeld::ConnectionId ConnId);

	UChanneldNetConnection* GetServerConnection() const
	{
		return CastChecked<UChanneldNetConnection>(ServerConnection);
	}

	UChanneldNetConnection* GetClientConnection(Channeld::ConnectionId ConnId) const
	{
		return ClientConnectionMap.FindRef(ConnId);
	}

	FORCEINLINE TMap<uint32, UChanneldNetConnection*>& GetClientConnectionMap() {return ClientConnectionMap;}

	virtual Channeld::ChannelId GetSendToChannelId(UChanneldNetConnection* NetConn) const;

	// Update the PackageMap of all connections that the specified NetId has been sent, so it's safe to send the actor's RPC message.
	void SetAllSentSpawn(const FNetworkGUID NetId);
	
	void RedirectRPC(TSharedPtr<unrealpb::RemoteFunctionMessage> Msg);

	void OnSentRPC(const unrealpb::RemoteFunctionMessage& RpcMsg);
	
	void OnServerBeginPlay(UChanneldReplicationComponent* RepComp);

	TWeakObjectPtr<UChannelDataView> ChannelDataView;
	void OnServerSpawnedActor(AActor* Actor);

protected:
	TSharedRef<Channeld::ChannelId> LowLevelSendToChannelId = MakeShared<Channeld::ChannelId>(Channeld::InvalidChannelId);

private:
	
	const FName ServerMovePackedFuncName = FName("ServerMovePacked");
	const FName ClientMoveResponsePackedFuncName = FName("ClientMoveResponsePacked");
	const FName ServerUpdateCameraFuncName = FName("ServerUpdateCamera");

	// Prevent the engine from GC the connection
	UPROPERTY()
	UChanneldConnection* ConnToChanneld;

	FURL InitBaseURL;

	// Cache the FInternetAddr so it won't be created again every time when mapping from ConnectionId.
	TMap<Channeld::ConnectionId, TSharedRef<FInternetAddr>> CachedAddr;

	UPROPERTY()
	TMap<uint32, UChanneldNetConnection*> ClientConnectionMap;

	// RPCs queued on the callee's side that dont' have the actor resolved yet.
	TArray<TSharedPtr<unrealpb::RemoteFunctionMessage>> UnprocessedRPCs;

	TSet<FNetworkGUID> SentSpawnedNetGUIDs;

	// Actors that spawned in server using SpawnActorDeferred (mainly in Blueprints), which don't have ActorComponent registered.
	// We need to skip these actors in OnServerSpawnedActor(), and actually handle them in their BeginPlay().
	TSet<TWeakObjectPtr<AActor>> ServerDeferredSpawns;

	void OnChanneldAuthenticated(UChanneldConnection* Conn);
	void OnUserSpaceMessageReceived(uint32 MsgType, Channeld::ChannelId ChId, Channeld::ConnectionId ClientConnId, const std::string& Payload);
	void OnReceivedRPC(const unrealpb::RemoteFunctionMessage& RpcMsg);
	void HandleSpawnObject(TSharedRef<unrealpb::SpawnObjectMessage> SpawnMsg);
	void HandleCustomRPC(TSharedPtr<unrealpb::RemoteFunctionMessage> Msg);
	void OnClientPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);

	UChanneldGameInstanceSubsystem* GetSubsystem() const;
};
