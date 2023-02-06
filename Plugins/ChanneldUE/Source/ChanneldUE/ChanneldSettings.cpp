// Fill out your copyright notice in the Description page of Project Settings.


#include "ChanneldSettings.h"
#include "ChanneldTypes.h"
#include "SocketSubsystem.h"
#include "View/ChannelDataView.h"

//DEFINE_LOG_CATEGORY(LogChanneld)

UChanneldSettings::UChanneldSettings(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer),
	RegionBoxMinSize(10, 10, 20),
	RegionBoxMaxSize(100000, 100000, 1000)
{
}


void UChanneldSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Command line arguments can override the INI settings
	const TCHAR* CmdLine = FCommandLine::Get();
	UE_LOG(LogChanneld, Log, TEXT("Parsing ChanneldSettings from command line args: %s"), CmdLine);

	FString ViewClassName;
	if (FParse::Value(CmdLine, TEXT("ViewClass="), ViewClassName))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed View class name from CLI: %s"), *ViewClassName);
		ChannelDataViewClass = LoadClass<UChannelDataView>(NULL, *ViewClassName);
	}
	if (FParse::Value(CmdLine, TEXT("channeldClientIp="), ChanneldIpForClient))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed Client IP from CLI: %s"), *ChanneldIpForClient);
	}
	if (FParse::Value(CmdLine, TEXT("channeldClientPort="), ChanneldPortForClient))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed Client Port from CLI: %d"), ChanneldPortForClient);
	}
	if (FParse::Value(CmdLine, TEXT("channeldServerIp="), ChanneldIpForServer))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed Server IP from CLI: %s"), *ChanneldIpForServer);
	}
	if (FParse::Value(CmdLine, TEXT("channeldServerPort="), ChanneldPortForServer))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed Server Port from CLI: %d"), ChanneldPortForServer);
	}

	FString Addr;
	if (FParse::Value(CmdLine, TEXT("channeldClientAddr="), Addr))
	{
		if (ParseNetAddr(Addr, ChanneldIpForClient, ChanneldPortForClient))
		{
			UE_LOG(LogChanneld, Log, TEXT("Parsed Client Address from CLI: %s -> %s:%d"), *Addr, *ChanneldIpForClient, ChanneldPortForClient);
		}
	}
	if (FParse::Value(CmdLine, TEXT("channeldServerAddr="), Addr))
	{
		if (ParseNetAddr(Addr, ChanneldIpForServer, ChanneldPortForServer))
		{
			UE_LOG(LogChanneld, Log, TEXT("Parsed Server Address from CLI: %s -> %s:%d"), *Addr, *ChanneldIpForServer, ChanneldPortForServer);
		}
	}

	if (FParse::Bool(CmdLine, TEXT("UseReceiveThread="), bUseReceiveThread))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed bUseReceiveThread from CLI: %d"), bUseReceiveThread);
	}

	if (FParse::Bool(CmdLine, TEXT("DisableHandshaking="), bDisableHandshaking))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed bDisableHandshaking from CLI: %d"), bDisableHandshaking);
	}

	if (FParse::Bool(CmdLine, TEXT("SkipCustomReplication="), bSkipCustomReplication))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed bSkipCustomReplication from CLI: %d"), bSkipCustomReplication);
	}

	if (FParse::Bool(CmdLine, TEXT("SkipCustomRPC="), bSkipCustomRPC))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed bSkipCustomRPC from CLI: %d"), bSkipCustomRPC);
	}
	
	FString PlayerStartLocatorClassName;
	if (FParse::Value(CmdLine, TEXT("PlayerStartLocatorClass="), PlayerStartLocatorClassName))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed PlayerStartLocator class name from CLI: %s"), *PlayerStartLocatorClassName);
		PlayerStartLocatorClass = LoadClass<UChannelDataView>(NULL, *PlayerStartLocatorClassName);
	}
	if (FParse::Bool(CmdLine, TEXT("EnableSpatialVisualizer="), bEnableSpatialVisualizer))
	{
		UE_LOG(LogChanneld, Log, TEXT("Parsed bEnableSpatialVisualizer from CLI: %d"), bEnableSpatialVisualizer);
	}
}

bool UChanneldSettings::ParseNetAddr(const FString& Addr, FString& OutIp, int32& OutPort)
{
	/*
	int32 PortIndex;
	if (Addr.FindChar(':', PortIndex))
	{
		const FString& PortStr = Addr.Right(Addr.Len() - 1 - PortIndex);
		OutPort = FCString::Atoi(*PortStr);
	}
	*/
	FAddressInfoResult Result = ISocketSubsystem::Get()->GetAddressInfo(*Addr, nullptr, EAddressInfoFlags::Default, NAME_None);
	if (Result.Results.Num() > 0)
	{
		auto NetAddr = Result.Results[0].Address;
		OutIp = NetAddr->ToString(false);
		int32 Port = NetAddr->GetPort();
		if (Port > 0)
			OutPort = Port;
		return true;
	}
	return false;
}