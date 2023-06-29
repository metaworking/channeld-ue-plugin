#pragma once
#include "ChannelDataInterfaces.h"

class CHANNELDUE_API FDefaultSpatialChannelDataProcessor : public IChannelDataProcessor
{
public:
	virtual bool Merge(const google::protobuf::Message* SrcMsg, google::protobuf::Message* DstMsg) override;

	virtual bool UpdateChannelData(UObject* TargetObj, google::protobuf::Message* ChannelData) override;
	
	virtual const google::protobuf::Message* GetStateFromChannelData(google::protobuf::Message* ChannelData, UClass* TargetClass, UObject* TargetObject, uint32 NetGUID, bool& bIsRemoved) override;
	virtual void SetStateToChannelData(const google::protobuf::Message* State, google::protobuf::Message* ChannelData, UClass* TargetClass, UObject* TargetObject, uint32 NetGUID) override;
};