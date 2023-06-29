#pragma once
#include "ChannelDataInterfaces.h"

// Based on the assumption that the states in the channel data are organized in a flat structure,
// i.e. the channel data contains the maps of NetId to state.
//
// In this way, the get/set state logic can be implemented based on Protobuf's reflection API.
// Keep in mind that the reflection implementation is 10-20 times lower than the plain code implementation,
// so it's not recommended to use this processor.
class CHANNELDUE_API FFlatStatesChannelDataProcessor : IChannelDataProcessor
{
public:
	virtual bool Merge(const google::protobuf::Message* SrcMsg, google::protobuf::Message* DstMsg) override;
	virtual const google::protobuf::Message* GetStateFromChannelData(google::protobuf::Message* ChannelData,
		UClass* TargetClass, UObject* TargetObject, uint32 NetGUID, bool& bIsRemoved) override;
	virtual void SetStateToChannelData(const google::protobuf::Message* State, google::protobuf::Message* ChannelData,
		UClass* TargetClass, UObject* TargetObject, uint32 NetGUID) override;
};
