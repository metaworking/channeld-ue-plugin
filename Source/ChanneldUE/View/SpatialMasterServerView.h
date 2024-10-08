#pragma once

#include "CoreMinimal.h"
#include "PlayerStartLocator.h"
#include "View/ChannelDataView.h"
#include "SpatialMasterServerView.generated.h"

/**
 * 
 */
UCLASS()
class CHANNELDUE_API USpatialMasterServerView : public UChannelDataView
{
	GENERATED_BODY()

public:
	USpatialMasterServerView(const FObjectInitializer& ObjectInitializer);

	virtual void InitServer(bool bShouldRecover) override;
	virtual void AddProvider(Channeld::ChannelId ChId, IChannelDataProvider* Provider) override;
	virtual Channeld::ChannelId GetOwningChannelId(const FNetworkGUID NetId) const override;
	virtual void OnClientPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer, UChanneldNetConnection* NewPlayerConn) override;

	UPROPERTY(EditAnywhere)
	uint32 ClientFanOutIntervalMs = 20;
	UPROPERTY(EditAnywhere)
	uint32 ClientFanOutDelayMs = 3000;
	
private:

	// Maintains all the channels Master server has created and which spatial server they belong to.
	TMap<Channeld::ChannelId, Channeld::ConnectionId> AllSpatialChannelIds;

	// Use by the server to locate the player start position. In order to spawn the player's pawn in the right spatial channel,
	// the Master server and spatial servers should have the EXACTLY SAME position for a player.
	UPROPERTY()
	UPlayerStartLocatorBase* PlayerStartLocator;

	void HandleSpatialChannelsReady(UChanneldConnection* _, Channeld::ChannelId ChId, const google::protobuf::Message* Msg);
};
