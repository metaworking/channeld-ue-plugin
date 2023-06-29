#include "DefaultSpatialChannelDataProcessor.h"

bool FDefaultSpatialChannelDataProcessor::Merge(const google::protobuf::Message* SrcMsg, google::protobuf::Message* DstMsg)
{
	// DstMsg->MergeFrom(*SrcMsg);
	
	auto Src = static_cast<const unrealpb::SpatialChannelData*>(SrcMsg);
	auto Dst = static_cast<unrealpb::SpatialChannelData*>(DstMsg);

	for (auto& Pair : Src->entities())
	{
		if (Pair.second.removed())
		{
			Dst->mutable_entities()->erase(Pair.first);
		}
		else
		{
			if (Dst->entities().contains(Pair.first))
			{
				Dst->mutable_entities()->at(Pair.first).MergeFrom(Pair.second);
			}
			else
			{
				Dst->mutable_entities()->emplace(Pair.first, Pair.second);
			}
		}
	}
	
	return true;
}

bool FDefaultSpatialChannelDataProcessor::UpdateChannelData(UObject* TargetObj, google::protobuf::Message* ChannelData)
{
	// Don't send Spatial channel data update to channeld, as the channel data is maintained via Spawn and Destroy messages.
	return false;
}

const google::protobuf::Message* FDefaultSpatialChannelDataProcessor::GetStateFromChannelData(
	google::protobuf::Message* ChannelData, UClass* TargetClass, UObject* TargetObject, uint32 NetGUID, bool& bIsRemoved)
{
	if (TargetClass != UObject::StaticClass())
	{
		return nullptr;
	}
	
	auto SpatialChannelData = static_cast<unrealpb::SpatialChannelData*>(ChannelData);
	auto Entry = SpatialChannelData->mutable_entities()->find(NetGUID);
	if (Entry != SpatialChannelData->mutable_entities()->end())
	{
		bIsRemoved = Entry->second.removed();
		return &Entry->second;
	}
	else
	{
		bIsRemoved = true;
		return nullptr;
	}
}

void FDefaultSpatialChannelDataProcessor::SetStateToChannelData(const google::protobuf::Message* State,
	google::protobuf::Message* ChannelData, UClass* TargetClass, UObject* TargetObject, uint32 NetGUID)
{
	if (TargetClass != UObject::StaticClass())
	{
		return;
	}

	static unrealpb::SpatialEntityState RemovedEntityState;
	RemovedEntityState.set_removed(true);
	auto EntityState = State ? static_cast<const unrealpb::SpatialEntityState*>(State) : &RemovedEntityState;
	auto SpatialChannelData = static_cast<unrealpb::SpatialChannelData*>(ChannelData);
	auto Entry = SpatialChannelData->mutable_entities()->find(NetGUID);
	if (Entry != SpatialChannelData->mutable_entities()->end())
	{
		Entry->second.MergeFrom(*EntityState);
	}
	else
	{
		SpatialChannelData->mutable_entities()->insert({ NetGUID, *EntityState });
	}
}

