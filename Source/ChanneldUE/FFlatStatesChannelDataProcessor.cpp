#include "FFlatStatesChannelDataProcessor.h"
#include "Replication/ChanneldReplication.h"
#include "google/protobuf/map_field.h"
#include "google/protobuf/reflection.h"

// using namespace google::protobuf;

bool FFlatStatesChannelDataProcessor::Merge(const google::protobuf::Message* SrcMsg, google::protobuf::Message* DstMsg)
{
	// TODO
	return false;
}

const google::protobuf::Message* FFlatStatesChannelDataProcessor::GetStateFromChannelData(google::protobuf::Message* ChannelData, UClass* TargetClass, UObject* TargetObject,  uint32 NetGUID, bool& bIsRemoved)
{
	bIsRemoved = false;
	auto StateInProto = ChanneldReplication::FindReplicatorStateInProto(TargetClass);
	if (!StateInProto)
	{
		UE_LOG(LogChanneld, Warning, TEXT("Can't find proto info of replicator state for class %s"), *TargetClass->GetName());
		return nullptr;
	}
	
	auto StatesField = ChannelData->GetDescriptor()->field(StateInProto->FieldIndex);
	if (!StatesField)
	{
		UE_LOG(LogChanneld, Warning, TEXT("Field index %d doesn't exist in proto message %s"), UTF8_TO_TCHAR(ChannelData->GetDescriptor()->full_name().c_str()));
		return nullptr;
	}

	const google::protobuf::Message* State = nullptr;
	if (StatesField->is_map())
	{
		auto KeysField = StatesField->message_type()->map_key();
		auto ValuesField = StatesField->message_type()->map_value();

		auto RepeatedStates = ChannelData->GetReflection()->GetRepeatedFieldRef<google::protobuf::Message>(*ChannelData, StatesField);
		int StatesNum = RepeatedStates.size();
		for (int i = 0; i < StatesNum; i++)
		{
			auto& KV = RepeatedStates.Get(i, ChannelData);
			if (KV.GetReflection()->GetUInt32(KV, KeysField) == NetGUID)
			{
				State = &KV.GetReflection()->GetMessage(KV, ValuesField);
				break;
			}
		}
	}
	else
	{
		State = &ChannelData->GetReflection()->GetMessage(*ChannelData, StatesField);
	}

	if (State)
	{
		auto RemovedField = State->GetDescriptor()->field(0);
		// The "removed" field should always be the first field in the proto message if it exists
		if (RemovedField && RemovedField->type() == google::protobuf::FieldDescriptor::TYPE_BOOL)
		{
			bIsRemoved = State->GetReflection()->GetBool(*State, RemovedField);
		}
	}

	return State;
}

void FFlatStatesChannelDataProcessor::SetStateToChannelData(const google::protobuf::Message* State,google::protobuf::Message* ChannelData, UClass* TargetClass, UObject* TargetObject, uint32 NetGUID)
{
	auto StateInProto = ChanneldReplication::FindReplicatorStateInProto(TargetClass);
	if (!StateInProto)
	{
		UE_LOG(LogChanneld, Warning, TEXT("Can't find proto info of replicator state for class %s"), *TargetClass->GetName());
		return;
	}
	
	auto StatesField = ChannelData->GetDescriptor()->field(StateInProto->FieldIndex);
	if (!StatesField)
	{
		UE_LOG(LogChanneld, Warning, TEXT("Field index %d doesn't exist in proto message %s"), UTF8_TO_TCHAR(ChannelData->GetDescriptor()->full_name().c_str()));
		return;
	}

	if (StatesField->is_map())
	{
		auto KV = ChannelData->GetReflection()->AddMessage(ChannelData, StatesField);
		KV->GetReflection()->SetUInt32(KV, StatesField->message_type()->map_key(), NetGUID);
		auto MutableState = KV->GetReflection()->MutableMessage(KV, StatesField->message_type()->map_value());
		
		if (State)
		{
			MutableState->MergeFrom(*State);
		}
		else
		{
			// The "removed" field should always be the first field in the proto message if it exists
			auto RemovedField = MutableState->GetDescriptor()->field(0);
			if (RemovedField && RemovedField->type() == google::protobuf::FieldDescriptor::TYPE_BOOL)
			{
				MutableState->GetReflection()->SetBool(MutableState, RemovedField, true);
			}
		}
	}
	else
	{
		if (State)
		{
			ChannelData->GetReflection()->MutableMessage(ChannelData, StatesField)->MergeFrom(*State);
		}
		else
		{
			auto RemovedState = ChannelData->GetReflection()->MutableMessage(ChannelData, StatesField);
			auto RemovedField = RemovedState->GetDescriptor()->field(0);
			if (RemovedField && RemovedField->type() == google::protobuf::FieldDescriptor::TYPE_BOOL)
			{
				RemovedState->GetReflection()->SetBool(RemovedState, RemovedField, true);
			}
		}
	}
}
