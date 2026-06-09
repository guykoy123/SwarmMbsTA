/*
 * DroneApp.h
 */
#ifndef __UAVSWARMTA_DRONEAPP_H_
#define __UAVSWARMTA_DRONEAPP_H_

#include <omnetpp.h>
#include "inet/mobility/contract/IMobility.h"
#include "DroneMobility.h"

using namespace omnetpp;

namespace uavswarmta {

class MainNodeApp;  // forward decl

enum DroneState {
    IDLE = 0,
    TRAVELLING = 1,
    PERFORMING_TASK = 2,
    WAITING_FOR_CONNECTION = 3,
    RETURNING_HOME = 4,
    HEADING_TO_RECHARGE = 5,
    RECHARGING = 6,
    DEAD = 7
};

class DroneApp : public cSimpleModule {
  public:
    bool isIdle() const { return currentState == IDLE; }
    int  getCurrentTaskId() const { return currentTaskId; }
    bool isAlive() const { return currentState != DEAD; }
    void assignTask(int taskId, double syncedTravelTime, double duration,
                    double targetX, double targetY, int assignedMbsId);

    // Used by the COST allocator to forcibly free a busy drone for a higher
    // priority task. The caller is responsible for bookkeeping the old task
    // (e.g. decrementing activeTaskDroneCount); preempt() only cleans local
    // state so that a subsequent assignTask() lands in a fresh drone.
    void preempt();

    // Called by the main node when peers leave a shared task -- the survivors
    // shoulder more work, so their remaining duration is multiplied by
    // scaleFactor (typically oldCount/newCount > 1.0). Safe to call from any
    // state: TRAVELLING scales taskDuration, PERFORMING_TASK reschedules the
    // completion timer, WAITING_FOR_CONNECTION scales savedRemainingDuration.
    void extendRemainingDuration(double scaleFactor);

    virtual ~DroneApp();

  protected:
    DroneMobility *mobility;       // own mobility (movable)
    inet::IMobility *mbsMobility;  // assigned MBS's mobility (for hasConnection)
    MainNodeApp *mainApp;          // direct-call target for completion / drop

    int droneId;
    DroneState currentState;
    int currentTaskId;
    double taskDuration;
    inet::Coord taskTargetPos;     // remembered for re-launch after reconnect

    inet::Coord homePosition;
    double commRange;
    double savedRemainingDuration;
    double commTimeoutDuration;

    double currentEnergy;
    simtime_t lastEnergyUpdateTime;
    double idlePower, travelPower, workingPower;

    cMessage *arrivalTimer;
    cMessage *taskCompletionTimer;
    cMessage *connectionCheckTimer;
    cMessage *connectionTimeoutTimer;

    double energyThreshold;
    double rechargeDuration;
    cMessage *rechargeTimer;

    virtual void initialize(int stage) override;
    virtual int numInitStages() const override { return 4; }
    virtual void handleMessage(cMessage *msg) override;

    virtual void die();
    virtual void initiateRecharge();
    virtual inet::Coord findClosestRechargeStation();

    virtual void updateEnergy();
    virtual bool hasConnection();
    virtual void dropTaskAndWait();

    virtual void handleArrival();
    virtual void handleConnectionCheck();
    virtual void handleConnectionTimeout();
    virtual void handleConnectionLoss();
    virtual void handleTaskCompletion();
    virtual void handleRechargeCompletion();

    // Direct-call helpers
    virtual void sendTaskCompletion();
    virtual void sendTaskDropNotification();

    // Convenience: command our mobility to go somewhere and schedule arrivalTimer
    virtual void flyTo(const inet::Coord& target);
};

} // namespace uavswarmta

#endif
