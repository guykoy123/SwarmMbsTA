#ifndef __UAVSWARMTA_DRONEAPP_H_
#define __UAVSWARMTA_DRONEAPP_H_

#include <omnetpp.h>
#include "inet/transportlayer/contract/udp/UdpSocket.h"
#include "inet/mobility/contract/IMobility.h"
#include "TaskMessages_m.h"

using namespace omnetpp;

namespace uavswarmta {

enum DroneState {
    IDLE = 0,
    TRAVELLING = 1,
    PERFORMING_TASK = 2,
    WAITING_FOR_CONNECTION = 3, //  Waiting for MBS to arrive or reconnect
    RETURNING_HOME = 4,          //  Given up, flying back to start
    HEADING_TO_RECHARGE = 5,
    RECHARGING = 6,
    DEAD = 7                 // Out of energy
};

class DroneApp : public cSimpleModule, public inet::UdpSocket::ICallback {
  public:
    bool isIdle() const { return currentState == IDLE; }
    // Updated to include the assigned MBS ID so the drone can track it
    void assignTask(int taskId, double syncedTravelTime, double duration, double targetX, double targetY, int assignedMbsId);

  protected:
    inet::UdpSocket socket;
    inet::IMobility *mobility;
    inet::IMobility *mbsMobility; // Pointer to track the MBS position

    int droneId;
    DroneState currentState;
    int currentTaskId;
    double taskDuration;

    inet::Coord homePosition; // Where the drone started
    double commRange;         // Communication range limit
    double savedRemainingDuration; //if comms dropped during task, save the remaining duration to send to main node once comms returns
    double commTimeoutDuration; //how long to wait once comms drop

    double currentEnergy;
    simtime_t lastEnergyUpdateTime;
    double idlePower, travelPower, workingPower;

    // Timers
    cMessage *arrivalTimer;
    cMessage *taskCompletionTimer;
    cMessage *connectionCheckTimer;   // New: Fires every 1s while performing task
    cMessage *connectionTimeoutTimer; // New: Fires if connection isn't restored in time

    //energy
    double energyThreshold;
    double rechargeDuration;
    cMessage *rechargeTimer;

    virtual void die();
    virtual void initiateRecharge();
    virtual inet::Coord findClosestRechargeStation();

    virtual void initialize(int stage) override;
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void handleMessage(cMessage *msg) override;

    virtual void updateEnergy();
    virtual bool hasConnection(); // Helper to check distance to MBS
    virtual void dropTaskAndWait(); // Triggers the drop logic

    virtual void handleArrival();
    virtual void handleConnectionCheck();
    virtual void handleConnectionTimeout();
    virtual void handleConnectionLoss();

    virtual void handleTaskCompletion();
    virtual void handleRechargeCompletion();

    virtual void socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) override;
    virtual void socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) override;
    virtual void socketClosed(inet::UdpSocket *socket) override;

    // Helper to avoid duplicate code
    virtual void sendTaskDropNotification();
};

} // namespace
#endif
