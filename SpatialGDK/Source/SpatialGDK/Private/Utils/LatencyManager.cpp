// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Utils/LatencyManager.h"

#include "Engine/World.h"

#include "EngineClasses/SpatialNetConnection.h"
#include "EngineClasses/SpatialNetDriver.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interop/Connection/SpatialWorkerConnection.h"
#include "Interop/SpatialReceiver.h"
#include "SpatialConstants.h"

DEFINE_LOG_CATEGORY(LogSpatialLatencyManager);

LatencyManager::LatencyManager(const USpatialNetConnection& InConnection, const USpatialNetDriver& InDriver)
	: PlayerControllerEntity(SpatialConstants::INVALID_ENTITY_ID)
	, NetConnection(InConnection)
	, NetDriver(InDriver)
{
}

void LatencyManager::Enable(Worker_EntityId InPlayerControllerEntity)
{
	checkf(PlayerControllerEntity == SpatialConstants::INVALID_ENTITY_ID, TEXT("LatencyManager::Enable : PlayerControllerEntity already set: %lld. New entity: %lld"), PlayerControllerEntity, InPlayerControllerEntity);
	PlayerControllerEntity = InPlayerControllerEntity;

	LastPingSent = NetConnection.GetWorld()->RealTimeSeconds;

	auto Delegate = TBaseDelegate<void, const Worker_ComponentUpdateOp &>::CreateLambda([this](const Worker_ComponentUpdateOp& Op)
	{
		float ReceivedTimestamp = NetConnection.GetWorld()->RealTimeSeconds;

		Schema_Object* EventsObject = Schema_GetComponentUpdateEvents(Op.update.schema_type);
		uint32 EventCount = Schema_GetObjectCount(EventsObject, SpatialConstants::PING_PONG_EVENT_ID);

		if (EventCount > 0)
		{
			check(NetConnection.PlayerController);

			if (NetConnection.PlayerController->PlayerState)
			{
				if (NetDriver.IsServer())
				{
					// The Server answers to the client's pong as fast as possible for the client to be able to measure latency
					SendPingOrPong(SpatialConstants::SERVER_PING_COMPONENT_ID);
				}
				else
				{
					UE_LOG(LogSpatialLatencyManager, Verbose, TEXT("UpdatePing(%f)"), (ReceivedTimestamp - LastPingSent));
					NetConnection.PlayerController->PlayerState->UpdatePing(ReceivedTimestamp - LastPingSent);

					// The client doesn't answer to the server's ping, it starts a new round-trip only a second later to lower the network load
					NetConnection.GetWorld()->GetTimerManager().SetTimer(PongTimerHandle, [this](){
							SendPingOrPong(SpatialConstants::CLIENT_PONG_COMPONENT_ID);
						}, 1.f, false);
				}
			}
			else
			{
				UE_LOG(LogSpatialLatencyManager, Warning, TEXT("[%s] PlayerState is nullptr"), *NetConnection.PlayerController->GetName());
			}
		}
	});


	if (NetDriver.IsServer())
	{
		NetDriver.Receiver->AddClientPongDelegate(PlayerControllerEntity, Delegate);
		SendPingOrPong(SpatialConstants::SERVER_PING_COMPONENT_ID);
	}
	else
	{
		NetDriver.Receiver->AddServerPingDelegate(PlayerControllerEntity, Delegate);
	}
}

void LatencyManager::Disable()
{
	PlayerControllerEntity = SpatialConstants::INVALID_ENTITY_ID;

	NetConnection.GetWorld()->GetTimerManager().ClearTimer(PongTimerHandle);
}

void LatencyManager::SendPingOrPong(Worker_ComponentId ComponentId)
{
	Worker_ComponentUpdate ComponentUpdate = {};

	ComponentUpdate.component_id = ComponentId;
	ComponentUpdate.schema_type = Schema_CreateComponentUpdate(ComponentId);
	Schema_Object* EventsObject = Schema_GetComponentUpdateEvents(ComponentUpdate.schema_type);
	Schema_AddObject(EventsObject, SpatialConstants::PING_PONG_EVENT_ID);

	if (NetDriver.IsValidLowLevel())
	{
		USpatialWorkerConnection* WorkerConnection = NetDriver.Connection;
		if (WorkerConnection && WorkerConnection->IsConnected())
		{
			UE_LOG(LogSpatialLatencyManager, Verbose, TEXT("SendPingOrPong(%s)"), NetDriver.IsServer() ? TEXT("server") : TEXT("client"));

			WorkerConnection->SendComponentUpdate(PlayerControllerEntity, &ComponentUpdate);
			LastPingSent = NetConnection.GetWorld()->RealTimeSeconds;
		}
	}
}
