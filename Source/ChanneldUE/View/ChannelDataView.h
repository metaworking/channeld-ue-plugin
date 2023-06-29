#pragma once

#include "CoreMinimal.h"
#include "ChanneldTypes.h"
#include "channeld.pb.h"
#include "ChanneldConnection.h"
#include "ChannelDataInterfaces.h"
#include "ChanneldNetConnection.h"
#include "google/protobuf/message.h"
#include "UObject/WeakInterfacePtr.h"
#include "ChannelDataView.generated.h"

// Owned by UChanneldGameInstanceSubsystem.
UCLASS(Blueprintable, Abstract, Config=ChanneldUE)
class CHANNELDUE_API UChannelDataView : public UObject
{
	GENERATED_BODY()

public:
	UChannelDataView(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	FORCEINLINE void RegisterChannelDataTemplate(int ChannelType, google::protobuf::Message* MsgTemplate)
	{
		if (ChannelDataTemplates.Contains(ChannelType))
		{
			UE_LOG(LogChanneld, Warning, TEXT("Channel data template for channel type %d already exists, will be overwritten."), ChannelType);
		}
		
		ChannelDataTemplates.Add(ChannelType, MsgTemplate);
		if (AnyForTypeUrl == nullptr)
			AnyForTypeUrl = new google::protobuf::Any;
		AnyForTypeUrl->PackFrom(*MsgTemplate);
		ChannelDataTemplatesByTypeUrl.Add(FString(UTF8_TO_TCHAR(AnyForTypeUrl->type_url().c_str())), MsgTemplate);
		UE_LOG(LogChanneld, Log, TEXT("Registered %s for channel type %d"), UTF8_TO_TCHAR(MsgTemplate->GetTypeName().c_str()), ChannelType);
	}

	UFUNCTION(BlueprintCallable)
	void RegisterChannelDataType(EChanneldChannelType ChannelType, const FString& MessageFullName);

	virtual void Initialize(UChanneldConnection* InConn);
	virtual void Unintialize();
	virtual void BeginDestroy() override;
	
	virtual bool GetSendToChannelId(UChanneldNetConnection* NetConn, uint32& OutChId) const {return false;}

	virtual void AddProvider(Channeld::ChannelId ChId, IChannelDataProvider* Provider);
	virtual void AddProviderToDefaultChannel(IChannelDataProvider* Provider);
	bool IsObjectProvider(UObject* Obj);
	void AddObjectProvider(Channeld::ChannelId ChId, UObject* Obj);
	void AddObjectProviderToDefaultChannel(UObject* Obj);
	void RemoveObjectProvider(Channeld::ChannelId ChId, UObject* Obj, bool bSendRemoved);
	void RemoveObjectProviderAll(UObject* Obj, bool bSendRemoved);
	virtual void RemoveProvider(Channeld::ChannelId ChId, IChannelDataProvider* Provider, bool bSendRemoved);
	virtual void RemoveProviderFromAllChannels(IChannelDataProvider* Provider, bool bSendRemoved);
	virtual void MoveProvider(Channeld::ChannelId OldChId, Channeld::ChannelId NewChId, IChannelDataProvider* Provider, bool bSendRemoved);
	void MoveObjectProvider(Channeld::ChannelId OldChId, Channeld::ChannelId NewChId, UObject* Provider, bool bSendRemoved);

	virtual void OnAddClientConnection(UChanneldNetConnection* ClientConnection, Channeld::ChannelId ChId){}
	virtual void OnRemoveClientConnection(UChanneldNetConnection* ClientConn){}
	virtual void OnClientPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer, UChanneldNetConnection* NewPlayerConn);
	virtual FNetworkGUID GetNetId(UObject* Obj, bool bAssignOnServer = true) const;
	virtual FNetworkGUID GetNetId(IChannelDataProvider* Provider) const;
	
	/**
	 * @brief Added the object to the NetId-ChannelId mapping. If the object is an IChannelDataProvider, also add it to the providers set.
	 * <br/>
	 * By default, the channelId used for adding is the LowLevelSendToChannelId in the UChanneldNetDriver / UChanneldGameInstanceSubsystem.
	 * <br/>
	 * Executed on the authoritative servers.
	 * @param Obj The object just spawned on the server, passed from UChanneldNetDriver::OnServerSpawnedActor.
	 * @param NetId The NetworkGUID assigned for the object.
	 * @return Should the NetDriver send the spawn message to the clients?
	 */
	virtual bool OnServerSpawnedObject(UObject* Obj, const FNetworkGUID NetId);
	// Send the Spawn message to all interested clients.
	virtual void SendSpawnToClients(UObject* Obj, uint32 OwningConnId);
	// Send the Destroy message to all interested clients.
	virtual void SendDestroyToClients(UObject* Obj, const FNetworkGUID NetId);
	// Send the Spawn message to a single client connection.
	// Gives the view a chance to override the NetRole, OwningChannelId, OwningConnId, or the Location parameter.
	virtual void SendSpawnToConn(UObject* Obj, UChanneldNetConnection* NetConn, uint32 OwningConnId);
	// The NetDriver spawned an object from the Spawn message. Executed on clients and interested servers.
	virtual void OnNetSpawnedObject(UObject* Obj, const Channeld::ChannelId ChId) {}
	virtual void OnDestroyedActor(AActor* Actor, const FNetworkGUID NetId);
	virtual void SetOwningChannelId(const FNetworkGUID NetId, Channeld::ChannelId ChId);
	virtual Channeld::ChannelId GetOwningChannelId(const FNetworkGUID NetId) const;
	virtual Channeld::ChannelId GetOwningChannelId(AActor* Actor) const;

	virtual bool SendMulticastRPC(AActor* Actor, const FString& FuncName, TSharedPtr<google::protobuf::Message> ParamsMsg, const FString& SubObjectPathName);

	int32 SendChannelUpdate(Channeld::ChannelId ChId);
	int32 SendAllChannelUpdates();

	void OnDisconnect();

	// UPROPERTY(EditAnywhere)
	// EChanneldChannelType DefaultChannelType = EChanneldChannelType::ECT_Global;

	// DO NOT loop-reference, otherwise the destruction can cause exception.
	// TSharedPtr< class UChanneldNetDriver > NetDriver;
	
protected:

	// TSet doesn't support TWeakInterfacePtr, so we need to wrap it in a new type. 
	struct FProviderInternal : TWeakInterfacePtr<IChannelDataProvider>
	{
		FProviderInternal(IChannelDataProvider* ProviderInstance) : TWeakInterfacePtr(ProviderInstance) {}

		bool operator==(const FProviderInternal& s) const
		{
			return Get() == s.Get();
		}

		friend FORCEINLINE uint32 GetTypeHash(const FProviderInternal& s)
		{
			return PointerHash(s.Get());
		}
	};

	virtual void LoadCmdLineArgs() {}

	UFUNCTION(BlueprintCallable, BlueprintPure/*, meta=(CallableWithoutWorldContext)*/)
	class UChanneldGameInstanceSubsystem* GetChanneldSubsystem() const;

	UObject* GetObjectFromNetGUID(const FNetworkGUID& NetId);

	virtual void InitServer();
	virtual void InitClient();

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "BeginInitServer"))
	void ReceiveInitServer();
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "BeginInitClient"))
	void ReceiveInitClient();

	virtual void UninitServer();
	virtual void UninitClient();

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "BeginUninitServer"))
	void ReceiveUninitServer();
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "BeginUninitClient"))
	void ReceiveUninitClient();

	void HandleChannelDataUpdate(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg);
	virtual bool ConsumeChannelUpdateData(Channeld::ChannelId ChId, google::protobuf::Message* UpdateData);

	const google::protobuf::Message* GetEntityData(UObject* Obj);

	void HandleUnsub(UChanneldConnection* Conn, Channeld::ChannelId ChId, const google::protobuf::Message* Msg);
	virtual void ServerHandleClientUnsub(Channeld::ConnectionId ClientConnId, channeldpb::ChannelType ChannelType, Channeld::ChannelId ChId);
	
	// Give the subclass a chance to mess with the removed providers, e.g. add a provider back to a channel.
	virtual void OnRemovedProvidersFromChannel(Channeld::ChannelId ChId, channeldpb::ChannelType ChannelType, const TSet<FProviderInternal>& RemovedProviders) {}

	/**
	 * @brief Checks if the channel data contains any unsolved NetworkGUID.
	 * @param ChId The channel that the data belongs to.
	 * @param ChannelData The data field in the ChannelDataUpdateMessage.
	 * @return If true, the ChannelDataUpdate should not be applied until the objects are spawned.
	 */
	virtual bool CheckUnspawnedObject(Channeld::ChannelId ChId, const google::protobuf::Message* ChannelData) {return false;}

	UPROPERTY()
	UChanneldConnection* Connection;

	TMap<int, const google::protobuf::Message*> ChannelDataTemplates;

	// Reuse the message objects to 1) decrease the memory footprint; 2) save the data for the next update if no state is consumed.
	TMap<Channeld::ChannelId, google::protobuf::Message*> ReceivedUpdateDataInChannels;

	// Received ChannelUpdateData that don't have any provider to consume. Will be consumed as soon as a provider is added to the channel.
	TMap<Channeld::ChannelId, TArray<google::protobuf::Message*>> UnprocessedUpdateDataInChannels;

	// The spawned object's NetGUID mapping to the ID of the channel that owns the object.
	TMap<const FNetworkGUID, Channeld::ChannelId> NetIdOwningChannels;

	// Virtual NetConnection for sending Spawn message to channeld to broadcast.
	// Exporting the NetId of the spawned object requires a NetConnection, but we don't have a specific client when broadcasting.
	// So we use a virtual NetConnection that doesn't belong to any client, and clear the export map everytime to make sure the NetId is fully exported.
	UPROPERTY()
	UChanneldNetConnection* NetConnForSpawn;

private:
	
	// Use the Arena for faster allocation. See https://developers.google.com/protocol-buffers/docs/reference/arenas
	google::protobuf::Arena ArenaForSend;

	google::protobuf::Any* AnyForTypeUrl;
	TMap<FString, google::protobuf::Message*> ChannelDataTemplatesByTypeUrl;

	TMap<Channeld::ChannelId, TSet<FProviderInternal>> ChannelDataProviders;
	TMap<Channeld::ChannelId, google::protobuf::Message*> RemovedProvidersData;
};
