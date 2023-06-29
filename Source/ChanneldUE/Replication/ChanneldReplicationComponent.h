#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChanneldTypes.h"
#include "ChannelDataInterfaces.h"
#include "Replication/ChanneldReplicatorBase.h"
#include "Components/ActorComponent.h"
#include "google/protobuf/message.h"
#include "ChanneldReplicationComponent.generated.h"

class UChanneldReplicationComponent;
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_OneParam(FComponentAddedToChannelSignature, UChanneldReplicationComponent, OnComponentAddedToChannel, int64, ChId);
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_OneParam(FComponentRemovedFromChannelSignature, UChanneldReplicationComponent, OnComponentRemovedFromChannel, int64, ChId);
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE(FCrossServerHandoverSignature, UChanneldReplicationComponent, OnCrossServerHandover);
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_OneParam(FEntityChannelCreatedSignature, UChanneldReplicationComponent, OnEntityChannelCreated, int64, ChId);

// Responsible for replicating the owning Actor and its replicated components via ChannelDataUpdate
UCLASS(BlueprintType, ClassGroup = "Channeld", meta = (DisplayName = "Channeld Replication Component", BlueprintSpawnableComponent))
class CHANNELDUE_API UChanneldReplicationComponent : public UActorComponent, public IChannelDataProvider
{
	GENERATED_BODY()

public:	
	UChanneldReplicationComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(BlueprintAssignable, Category = "Components|Channeld")
	FComponentAddedToChannelSignature OnComponentAddedToChannel;

	UPROPERTY(BlueprintAssignable, Category = "Components|Channeld")
	FComponentRemovedFromChannelSignature OnComponentRemovedFromChannel;

	UPROPERTY(BlueprintAssignable, Category = "Components|Channeld")
	FCrossServerHandoverSignature OnCrossServerHandover;

	UPROPERTY(BlueprintAssignable, Category = "Components|Channeld")
	FEntityChannelCreatedSignature OnEntityChannelCreated;
	
	TSet<Channeld::ChannelId> AddedToChannelIds;

protected:
	bool bInitialized = false;
	bool bUninitialized = false;
	// UPROPERTY(EditAnywhere, BlueprintReadWrite)
	// EChanneldChannelType ChannelType;
	// Channeld::ChannelId OwningChannelId;
	bool bRemoved = false;
	float LastUpdateTime = 0;

	TArray< TUniquePtr<FChanneldReplicatorBase> > Replicators;

public:

	virtual void BeginPlay() override;
	virtual void InitOnce();
	virtual void UninitOnce();
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	/* Only EndPlay() will be triggered on the server when the Actor is being destroy!
	virtual void DestroyComponent(bool bPromoteChildren = false) override;
	virtual void BeginDestroy() override;
	*/
	
	//~ Begin IChannelDataProvider Interface.
	virtual UObject* GetTargetObject() override {return GetOwner();}
	virtual void OnAddedToChannel(Channeld::ChannelId ChId) override;
	virtual void OnRemovedFromChannel(Channeld::ChannelId ChId) override;
	virtual bool IsRemoved() override;
	virtual void SetRemoved(bool bInRemoved) override;
	virtual bool UpdateChannelData(google::protobuf::Message* ChannelData) override;
	virtual void OnChannelDataUpdated(google::protobuf::Message* ChannelData) override;
	//~ End IChannelDataProvider Interface.

	TSharedPtr<google::protobuf::Message> SerializeFunctionParams(UObject* Object, UFunction* Func, void* Params, FOutParmRec* OutParams, bool& bSuccess);
	TSharedPtr<void> DeserializeFunctionParams(UObject* Object, UFunction* Func, const std::string& ParamsPayload, bool& bSuccess, bool& bDeferredRPC);
};