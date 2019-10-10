// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include <WorkerSDK/improbable/c_worker.h>

class USpatialNetConnection;
class USpatialNetDriver;

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialLatencyManager, Log, All);

class LatencyManager
{
public:
	LatencyManager(const USpatialNetConnection& InConnection, const USpatialNetDriver& InDriver);

	void Enable(Worker_EntityId InPlayerControllerEntity);
	void Disable();

private:
	void SendPingOrPong(Worker_ComponentId ComponentId);

	Worker_EntityId PlayerControllerEntity;
	float LastPingSent;
	FTimerHandle PongTimerHandle;

	const USpatialNetConnection& NetConnection;
	const USpatialNetDriver& NetDriver;
};
