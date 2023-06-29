#pragma once

#include "CoreMinimal.h"
#include "ChanneldTypes.h"
#include "channeld.pb.h"
#include "google/protobuf/message.h"
#include "unreal_common.pb.h"
#include "ChannelDataInterfaces.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UChannelDataProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * @brief This interface connects the channel data view and a replicated object.
 * 
 * In order to replicate an UObject via channeld, you need to implement this interface; for AActors, use UChanneldReplicationComponent.
 * 
 * The data providers are aggregated in UChannelDataView.
 */
class CHANNELDUE_API IChannelDataProvider
{
	GENERATED_BODY()
public:
	virtual UObject* GetTargetObject() = 0;
	// virtual channeldpb::ChannelType GetChannelType() = 0;
	// virtual google::protobuf::Message* GetChannelDataTemplate() const = 0;
	//virtual Channeld::ChannelId GetChannelId() = 0;
	// virtual void SetChannelId(Channeld::ChannelId ChId) = 0;
	virtual void OnAddedToChannel(Channeld::ChannelId ChId) {}
	virtual void OnRemovedFromChannel(Channeld::ChannelId ChId) {}
	virtual bool IsRemoved() = 0;
	virtual void SetRemoved(bool bInRemoved) = 0;
	virtual bool UpdateChannelData(google::protobuf::Message* ChannelData) = 0;
	virtual void OnChannelDataUpdated(google::protobuf::Message* ChannelData) = 0;
	
	static FString GetName(const IChannelDataProvider* Provider)
	{
		return Provider->_getUObject()->GetPathName();// GetClass()->GetFullName();
		//return Cast<UObjectBase>(Provider)->GetClass()->GetName();
	}

};

/**
 * @brief Contains logic for merging channel data used in UChannelDataView::HandleChannelDataUpdate(), and
 * getting/setting specific state from/to the channel data used in UChanneldReplicationComponent.
 */
class CHANNELDUE_API IChannelDataProcessor
{
public:
	virtual bool Merge(const google::protobuf::Message* SrcMsg, google::protobuf::Message* DstMsg) = 0;

	virtual bool UpdateChannelData(UObject* TargetObj, google::protobuf::Message* ChannelData) {return true;}
	virtual bool OnChannelDataUpdated(UObject* TargetObj, google::protobuf::Message* ChannelData) {return true;}
	
	virtual const google::protobuf::Message* GetStateFromChannelData(google::protobuf::Message* ChannelData, UClass* TargetClass, UObject* TargetObject, uint32 NetGUID, bool& bIsRemoved) = 0;
	/**
	 * @brief Set a replicator's state to the channel data. UChanneldReplicationComponent doesn't know what states are defined in the channel data, or how are they organized. So the child class should implement this logic.
	 * @param State The delta state of a replicator, collected during Tick(). If null, removed = true will be set for the state.
	 * @param ChannelData The data field in the ChannelDataUpdate message which will be sent to channeld.
	 * @param TargetClass The class associated with the replicator. E.g. AActor for FChanneldActorReplicator, and ACharacter for FChanneldCharacterReplicator.
	 * @param NetGUID The NetworkGUID used for looking up the state in the channel data. Generally the key of the state map.
	 */
	virtual void SetStateToChannelData(const google::protobuf::Message* State, google::protobuf::Message* ChannelData, UClass* TargetClass, UObject* TargetObject, uint32 NetGUID) = 0;
	
	virtual ~IChannelDataProcessor() {}
};