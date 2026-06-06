/*
 * DroneApp.cc
 */
#include "DroneApp.h"
#include "MainNodeApp.h"

namespace uavswarmta {

Define_Module(DroneApp);

DroneApp::~DroneApp() {
    cancelAndDelete(arrivalTimer);
    cancelAndDelete(taskCompletionTimer);
    cancelAndDelete(connectionCheckTimer);
    cancelAndDelete(connectionTimeoutTimer);
    cancelAndDelete(rechargeTimer);
}

void DroneApp::initialize(int stage) {
    if (stage == 0) {
        droneId = getParentModule()->getIndex();
        currentState = IDLE;
        currentTaskId = -1;
        mbsMobility = nullptr;
        mainApp = nullptr;

        currentEnergy = par("initialEnergy").doubleValue();
        idlePower = par("idlePower").doubleValue();
        travelPower = par("travelPower").doubleValue();
        workingPower = par("workingPower").doubleValue();
        lastEnergyUpdateTime = simTime();

        mobility = check_and_cast<DroneMobility *>(
            getParentModule()->getSubmodule("mobility"));

        commRange = par("drone_comm_range").doubleValue();
        commTimeoutDuration = par("commTimeoutDuration");

        // Draw the comm range as a circle around the drone icon (auto-follows).
        cDisplayString& ds = getParentModule()->getDisplayString();
        ds.setTagArg("r", 0, (long)commRange);
        ds.setTagArg("r", 1, "");          // no fill
        ds.setTagArg("r", 2, "cyan");

        energyThreshold = par("energyThreshold").doubleValue();
        rechargeDuration = par("rechargeDuration").doubleValue();
        rechargeTimer = new cMessage("rechargeTimer");

        arrivalTimer = new cMessage("arrivalTimer");
        taskCompletionTimer = new cMessage("taskCompletionTimer");
        connectionCheckTimer = new cMessage("connectionCheckTimer");
        connectionTimeoutTimer = new cMessage("connectionTimeoutTimer");
    }
    else if (stage == 1) {
        // Resolve sibling modules now that the network is fully built.
        cModule *mainNodeMod = getModuleByPath("^.^.mainNode");
        if (mainNodeMod) {
            auto mainMobility = check_and_cast<inet::IMobility *>(
                mainNodeMod->getSubmodule("mobility"));
            homePosition = mainMobility->getCurrentPosition();
            mainApp = check_and_cast<MainNodeApp *>(
                mainNodeMod->getSubmodule("app", 0));
        } else {
            homePosition = mobility->getCurrentPosition();
            EV_WARN << "Drone " << droneId
                    << ": mainNode not found, setting home to local start.\n";
        }
    }
}

// =======================================================================
// PUBLIC: called by MainNodeApp::dispatchUnits()
// =======================================================================
void DroneApp::assignTask(int taskId, double syncedTravelTime, double duration,
                          double targetX, double targetY, int mbsId) {
    Enter_Method("assignTask");
    updateEnergy();

    currentState = TRAVELLING;
    currentTaskId = taskId;
    taskDuration = duration;
    taskTargetPos = inet::Coord(targetX, targetY, mobility->getCurrentPosition().z);

    // Remember which MBS will cover this task for the hasConnection() check.
    cModule *mbsMod = getModuleByPath(("^.^.mbs[" + std::to_string(mbsId) + "]").c_str());
    if (mbsMod) {
        mbsMobility = check_and_cast<inet::IMobility *>(mbsMod->getSubmodule("mobility"));
    }

    EV << "Drone " << droneId << " assigned Task #" << taskId
       << ". Synced arrival in: " << syncedTravelTime << "s.\n";

    // Command the mobility to actually fly to the task site at our speed.
    mobility->setTarget(taskTargetPos, par("speed").doubleValue());

    // Use the synced arrival window so every drone arrives together.
    cancelEvent(arrivalTimer);
    scheduleAt(simTime() + syncedTravelTime, arrivalTimer);
}

// =======================================================================
// MAIN MESSAGE ROUTER
// =======================================================================
void DroneApp::handleMessage(cMessage *msg) {
    if (currentState == DEAD) {
        if (msg->isSelfMessage()) return;  // owned by us, do not delete
        delete msg;
        return;
    }

    if (msg == arrivalTimer)              handleArrival();
    else if (msg == connectionCheckTimer) handleConnectionCheck();
    else if (msg == connectionTimeoutTimer) handleConnectionTimeout();
    else if (msg == taskCompletionTimer)  handleTaskCompletion();
    else if (msg == rechargeTimer)        handleRechargeCompletion();
    else if (msg->isSelfMessage())        return;
    else                                  delete msg;
}

// =======================================================================
// CONNECTION / ENERGY / DEATH
// =======================================================================
bool DroneApp::hasConnection() {
    if (mbsMobility == nullptr) return false;
    double distance = mobility->getCurrentPosition().distance(mbsMobility->getCurrentPosition());
    return distance <= commRange;
}

void DroneApp::handleConnectionLoss() {
    EV << "Drone " << droneId << " lost connection! Pausing Task #" << currentTaskId
       << " and waiting for " << commTimeoutDuration << "s.\n";

    if (taskCompletionTimer->isScheduled()) {
        savedRemainingDuration = (taskCompletionTimer->getArrivalTime() - simTime()).dbl();
        cancelEvent(taskCompletionTimer);
    } else {
        savedRemainingDuration = taskDuration;
    }

    currentState = WAITING_FOR_CONNECTION;
    scheduleAt(simTime() + commTimeoutDuration, connectionTimeoutTimer);
}

void DroneApp::updateEnergy() {
    if (currentState == DEAD) return;

    simtime_t timePassed = simTime() - lastEnergyUpdateTime;
    double powerDraw = 0.0;

    if (currentState == IDLE || currentState == WAITING_FOR_CONNECTION) powerDraw = idlePower;
    else if (currentState == TRAVELLING || currentState == RETURNING_HOME || currentState == HEADING_TO_RECHARGE) powerDraw = travelPower;
    else if (currentState == PERFORMING_TASK) powerDraw = workingPower;
    else if (currentState == RECHARGING) powerDraw = 0.0;

    currentEnergy -= powerDraw * timePassed.dbl();
    lastEnergyUpdateTime = simTime();

    if (currentEnergy <= 0.0) {
        currentEnergy = 0.0;
        die();
    }
    else if (currentEnergy <= energyThreshold &&
             currentState != HEADING_TO_RECHARGE &&
             currentState != RECHARGING) {
        initiateRecharge();
    }
}

void DroneApp::die() {
    EV << "Drone " << droneId << " HAS RUN OUT OF ENERGY! Shutting down completely.\n";
    currentState = DEAD;

    cancelEvent(arrivalTimer);
    cancelEvent(taskCompletionTimer);
    cancelEvent(connectionCheckTimer);
    cancelEvent(connectionTimeoutTimer);
    cancelEvent(rechargeTimer);

    // Freeze mobility too so the dead drone stops moving.
    mobility->setTarget(mobility->getCurrentPosition(), 0);

    getParentModule()->getDisplayString().setTagArg("i", 1, "red");
}

inet::Coord DroneApp::findClosestRechargeStation() {
    inet::Coord myPos = mobility->getCurrentPosition();
    inet::Coord closestPos = homePosition;
    double minDistance = myPos.distance(homePosition);

    int numMbs = getParentModule()->getParentModule()->par("numMbs").intValue();
    for (int i = 0; i < numMbs; i++) {
        cModule *mbsModule = getModuleByPath(("^.^.mbs[" + std::to_string(i) + "]").c_str());
        if (mbsModule) {
            auto m = check_and_cast<inet::IMobility *>(mbsModule->getSubmodule("mobility"));
            double dist = myPos.distance(m->getCurrentPosition());
            if (dist < minDistance) {
                minDistance = dist;
                closestPos = m->getCurrentPosition();
            }
        }
    }
    return closestPos;
}

void DroneApp::initiateRecharge() {
    EV << "Drone " << droneId << " energy critical! Aborting and heading to recharge.\n";

    if (currentTaskId != -1) {
        if (taskCompletionTimer->isScheduled()) {
            savedRemainingDuration = (taskCompletionTimer->getArrivalTime() - simTime()).dbl();
        } else {
            savedRemainingDuration = taskDuration;
        }
        cancelEvent(taskCompletionTimer);
        cancelEvent(connectionCheckTimer);
        cancelEvent(connectionTimeoutTimer);
    }

    currentState = HEADING_TO_RECHARGE;
    getParentModule()->getDisplayString().setTagArg("i", 1, "yellow");

    inet::Coord targetStation = findClosestRechargeStation();
    flyTo(targetStation);
}

// =======================================================================
// FLY HELPER
// =======================================================================
void DroneApp::flyTo(const inet::Coord& target) {
    double speed = par("speed").doubleValue();
    double dist = mobility->getCurrentPosition().distance(target);
    double travelTime = (speed > 0) ? dist / speed : 0;

    mobility->setTarget(target, speed);

    cancelEvent(arrivalTimer);
    scheduleAt(simTime() + travelTime, arrivalTimer);
}

// =======================================================================
// EVENT HANDLERS
// =======================================================================
void DroneApp::handleArrival() {
    updateEnergy();
    if (currentState == DEAD) return;

    if (currentState == TRAVELLING) {
        if (hasConnection()) {
            currentState = PERFORMING_TASK;
            EV << "Drone " << droneId << " arrived and has connection. Starting Task #"
               << currentTaskId << ".\n";
            scheduleAt(simTime() + taskDuration, taskCompletionTimer);
        } else {
            EV << "Drone " << droneId << " arrived but MBS is missing! Waiting for connection...\n";
            handleConnectionLoss();
        }
        scheduleAt(simTime() + 1.0, connectionCheckTimer);

    } else if (currentState == RETURNING_HOME) {
        currentState = IDLE;
        EV << "Drone " << droneId << " arrived home.\n";
        sendTaskDropNotification();

    } else if (currentState == HEADING_TO_RECHARGE) {
        currentState = RECHARGING;
        EV << "Drone " << droneId << " arrived at station. Recharging for "
           << rechargeDuration << "s.\n";

        if (currentTaskId != -1) {
            sendTaskDropNotification();
        }
        scheduleAt(simTime() + rechargeDuration, rechargeTimer);
    }
}

void DroneApp::handleConnectionCheck() {
    if (currentState == PERFORMING_TASK) {
        if (!hasConnection()) {
            handleConnectionLoss();
        }
    } else if (currentState == WAITING_FOR_CONNECTION) {
        if (hasConnection()) {
            EV << "Drone " << droneId << " regained connection! Resuming Task #"
               << currentTaskId << ".\n";
            cancelEvent(connectionTimeoutTimer);
            currentState = PERFORMING_TASK;
            scheduleAt(simTime() + savedRemainingDuration, taskCompletionTimer);
        }
    }

    if (currentState == PERFORMING_TASK || currentState == WAITING_FOR_CONNECTION) {
        scheduleAt(simTime() + 1.0, connectionCheckTimer);
    }
}

void DroneApp::handleConnectionTimeout() {
    updateEnergy();
    if (currentState == DEAD) return;

    EV << "Drone " << droneId << " connection timeout! Returning home.\n";
    cancelEvent(connectionCheckTimer);
    currentState = RETURNING_HOME;
    flyTo(homePosition);
}

void DroneApp::handleTaskCompletion() {
    updateEnergy();
    if (currentState == DEAD) return;

    currentState = IDLE;
    cancelEvent(connectionCheckTimer);

    EV << "Drone " << droneId << " finished Task #" << currentTaskId << ".\n";
    sendTaskCompletion();
}

void DroneApp::handleRechargeCompletion() {
    currentEnergy = par("initialEnergy").doubleValue();
    lastEnergyUpdateTime = simTime();
    currentState = IDLE;

    EV << "Drone " << droneId << " fully recharged and ready.\n";
    getParentModule()->getDisplayString().setTagArg("i", 1, "");
}

// =======================================================================
// DIRECT-CALL OUTPUTS (replaces UDP socket sends)
// =======================================================================
void DroneApp::sendTaskCompletion() {
    if (currentTaskId == -1) return;
    if (mainApp != nullptr) {
        mainApp->onTaskCompletion(currentTaskId, droneId);
    } else {
        EV_WARN << "Drone " << droneId << ": mainApp pointer null, cannot report completion.\n";
    }
    currentTaskId = -1;
    mbsMobility = nullptr;
}

void DroneApp::sendTaskDropNotification() {
    if (currentTaskId == -1) return;
    EV << "Reporting TaskDropNotification for Task #" << currentTaskId << ".\n";
    if (mainApp != nullptr) {
        mainApp->onTaskDropNotification(currentTaskId, droneId, savedRemainingDuration);
    }
    currentTaskId = -1;
    mbsMobility = nullptr;
}

void DroneApp::dropTaskAndWait() {
    EV << "Drone " << droneId << " is dropping Task #" << currentTaskId
       << " due to connection loss/low battery!\n";
    if (currentTaskId != -1) {
        savedRemainingDuration = 0.0;
        sendTaskDropNotification();
    }
    currentState = IDLE;
}

} // namespace uavswarmta
