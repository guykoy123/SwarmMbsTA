/*
 * DroneApp.cc
 *
 * Per-drone application logic. One instance lives inside each drone[i]
 * host. The drone is purely reactive -- the central MainNodeApp decides
 * what to do and when via direct method calls (assignTask, preempt,
 * extendRemainingDuration); the drone reports back through the same
 * channel (MainNodeApp::onTaskCompletion / onTaskDropNotification).
 *
 * State machine (see enum DroneState):
 *   IDLE  --assignTask-->  TRAVELLING  --arrival-->  PERFORMING_TASK
 *                                                       |
 *                                                       v
 *                          completion timer  -->  IDLE
 *
 *   Side branches from any non-DEAD state:
 *     * connection lost mid-task  -> WAITING_FOR_CONNECTION (timer:
 *                                    commTimeoutDuration). Resume on
 *                                    reconnect, otherwise RETURNING_HOME.
 *     * low battery               -> HEADING_TO_RECHARGE -> RECHARGING -> IDLE
 *     * battery hits zero         -> die() -> DEAD (terminal)
 *
 * Energy accounting is a pure integration (power * dt) settled at every
 * state change and on the 1-second connectionCheckTimer tick so we notice
 * a low battery DURING a long task, not just at transitions.
 */
#include "DroneApp.h"
#include "MainNodeApp.h"

namespace uavswarmta {

Define_Module(DroneApp);

// All five self-message timers are owned by us; cancelAndDelete handles
// both the cancel and the free safely whether or not the timer is scheduled.
DroneApp::~DroneApp() {
    cancelAndDelete(arrivalTimer);
    cancelAndDelete(taskCompletionTimer);
    cancelAndDelete(connectionCheckTimer);
    cancelAndDelete(connectionTimeoutTimer);
    cancelAndDelete(rechargeTimer);
}

// Two-stage init:
//   stage 0 -- cache NED params, set up the mobility + comm-range circle,
//              allocate the self-message timers we will reuse later.
//   stage 1 -- resolve sibling modules (mainNode + its home position). Done
//              in stage 1 because mainNode is built in stage 0 alongside us.
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
// Hand the drone a new task. Caller has already done the eligibility checks
// and computed `syncedTravelTime` so every drone on the same task starts
// work at the same instant (the longest of (each drone's travel time, the
// MBS travel time)). Side-effects:
//   * resolves the assigned MBS so hasConnection() can monitor link distance
//   * commands the mobility to actually fly to the target
//   * schedules arrivalTimer at the synced ETA
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

// Forcibly clear our current task without notifying the main node (the
// MainNodeApp's COST allocator already accounts for the drop on its side).
// After this returns the drone is in IDLE state and safe to assignTask() again.
void DroneApp::preempt() {
    Enter_Method("preempt");
    if (currentState == DEAD) return;

    // Settle whatever energy was burned up to this instant so the new task
    // starts with an accurate battery reading.
    updateEnergy();
    if (currentState == DEAD) return;

    EV << "Drone " << droneId << " PREEMPTED from Task #" << currentTaskId
       << " for a higher-priority assignment.\n";

    cancelEvent(arrivalTimer);
    cancelEvent(taskCompletionTimer);
    cancelEvent(connectionCheckTimer);
    cancelEvent(connectionTimeoutTimer);

    currentTaskId = -1;
    mbsMobility = nullptr;
    savedRemainingDuration = 0.0;
    currentState = IDLE;
}

// A peer just left the same task; pick up the slack by extending the time we
// still need to work. Called by MainNodeApp::scaleRemainingDurationForTask.
void DroneApp::extendRemainingDuration(double scaleFactor) {
    Enter_Method("extendRemainingDuration");
    if (currentState == DEAD || currentTaskId == -1 || scaleFactor <= 1.0) return;

    if (currentState == PERFORMING_TASK && taskCompletionTimer->isScheduled()) {
        double remaining = (taskCompletionTimer->getArrivalTime() - simTime()).dbl();
        double extended  = remaining * scaleFactor;
        cancelEvent(taskCompletionTimer);
        scheduleAt(simTime() + extended, taskCompletionTimer);
        EV << "Drone " << droneId << " picking up slack on Task #" << currentTaskId
           << ": remaining " << remaining << "s -> " << extended << "s (x"
           << scaleFactor << ").\n";
    } else if (currentState == WAITING_FOR_CONNECTION) {
        savedRemainingDuration *= scaleFactor;
        EV << "Drone " << droneId << " (waiting on Task #" << currentTaskId
           << ") will resume with " << savedRemainingDuration << "s remaining.\n";
    } else if (currentState == TRAVELLING) {
        // Haven't started work yet -- scale the nominal duration so when we
        // arrive and schedule the completion timer it reflects the new load.
        taskDuration *= scaleFactor;
        EV << "Drone " << droneId << " (en route to Task #" << currentTaskId
           << ") will now work for " << taskDuration << "s after arrival.\n";
    }
    // Other states (RETURNING_HOME, recharge, etc.) -- we've already left the
    // task semantically; nothing to extend.
}

// =======================================================================
// MAIN MESSAGE ROUTER
// =======================================================================
// Five self-message timers, plus a catch-all delete for anything else.
// A DEAD drone silently drops every incoming message (its own self-msgs
// have all been cancelled by die(), so this branch only protects against
// late stragglers from peer modules).
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
// Link-budget check: a drone is "connected" iff its Euclidean distance to
// the MBS currently covering its task is within commRange. The MBS may be
// moving (it's an unmanned base station), so this is rechecked on every
// connectionCheckTimer tick during PERFORMING_TASK / WAITING_FOR_CONNECTION.
bool DroneApp::hasConnection() {
    if (mbsMobility == nullptr) return false;
    double distance = mobility->getCurrentPosition().distance(mbsMobility->getCurrentPosition());
    return distance <= commRange;
}

// Connection just dropped mid-task. Snapshot how much work is left,
// suspend the completion timer, switch to WAITING_FOR_CONNECTION and start
// a countdown -- if the MBS doesn't return within commTimeoutDuration the
// drone gives up via handleConnectionTimeout() (-> returns home + drops).
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

// Integrate power draw over the time since the previous settle. State picks
// the power profile (idle / travel / work / recharge). Reaching zero kills
// the drone; falling under energyThreshold queues a recharge trip.
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

// Terminal state: battery hit zero. If a task was in flight we tell the
// main node so its bookkeeping can decrement the team count (and possibly
// finalize the task as DROPPED). All timers are cancelled, mobility is
// frozen, and the icon turns red. No transitions out of DEAD.
void DroneApp::die() {
    EV << "Drone " << droneId << " HAS RUN OUT OF ENERGY! Shutting down completely.\n";

    // If we were mid-task, tell the main node the task got dropped so the
    // activeTaskDroneCount can decrement and the task can be finalized.
    if (currentTaskId != -1) {
        if (taskCompletionTimer->isScheduled()) {
            savedRemainingDuration = (taskCompletionTimer->getArrivalTime() - simTime()).dbl();
        } else {
            savedRemainingDuration = taskDuration;
        }
        sendTaskDropNotification();
    }

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

// Recharge stations are co-located with MBSs (plus the mainNode home as a
// fallback). Pick whichever is currently closest to our position.
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

// Battery dipped under threshold while non-RECHARGING. Save how much work
// was left on the current task (so the main node can re-allocate it after
// we drop), cancel its timers, head to the closest recharge station.
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
// Issue a setTarget() to the mobility AND schedule arrivalTimer at the ETA,
// so the state machine knows when the drone has actually arrived. Replaces
// any in-flight goal (caller is responsible for state consistency).
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
// arrivalTimer fired: we just reached our flyTo() target. What happens next
// depends on WHY we were flying:
//   * TRAVELLING (en route to a task)   -> start work if MBS is in range,
//                                          otherwise enter WAITING_FOR_CONNECTION
//   * RETURNING_HOME (after timeout)    -> back to IDLE, report drop
//   * HEADING_TO_RECHARGE               -> begin RECHARGING countdown,
//                                          report drop if a task was held
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

// Periodic (1 Hz) tick scheduled while PERFORMING_TASK or
// WAITING_FOR_CONNECTION. Three jobs:
//   1. settle the energy account (catches a low battery DURING a long task)
//   2. detect new connection loss (PERFORMING_TASK -> WAITING_FOR_CONNECTION)
//   3. resume a paused task once the MBS is back in range (WAITING -> PERFORMING)
// Re-arms itself for another second while still in either of the two
// monitored states.
void DroneApp::handleConnectionCheck() {
    // Periodically settle energy so we notice a low battery DURING a long task
    // (otherwise updateEnergy only runs at state transitions).
    updateEnergy();
    if (currentState == DEAD) return;

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

// commTimeoutDuration elapsed while WAITING_FOR_CONNECTION. The MBS never
// came back -- give up on the task and fly home. The drop is reported when
// arrivalTimer fires at home (see handleArrival's RETURNING_HOME branch).
void DroneApp::handleConnectionTimeout() {
    // Decide what to do before charging energy (so even a death below still
    // notifies the main node about the dropped task).
    EV << "Drone " << droneId << " connection timeout! Returning home.\n";
    cancelEvent(connectionCheckTimer);
    currentState = RETURNING_HOME;
    flyTo(homePosition);

    updateEnergy();
}

// taskCompletionTimer fired: our share of the work is done. We notify the
// main node BEFORE settling energy so the completion is reported even if
// the energy update kills us or triggers a recharge run.
void DroneApp::handleTaskCompletion() {
    // The work is done as of *this* event -- report it before anything else,
    // so that even if updateEnergy() kills us or sends us off to recharge,
    // the main node has already been told the task finished successfully.
    EV << "Drone " << droneId << " finished Task #" << currentTaskId << ".\n";
    cancelEvent(connectionCheckTimer);
    sendTaskCompletion();
    currentState = IDLE;

    // Now it's safe to settle the energy account (may trigger recharge / die).
    updateEnergy();
}

// rechargeDuration elapsed at a station -- top up the battery, switch back
// to IDLE, clear the yellow status icon. The drone is now allocatable again.
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
// Tell the main node a task finished successfully. Cleared local task
// state so a subsequent assignTask() lands cleanly.
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

// Tell the main node we are abandoning the current task (death, recharge,
// connection timeout). savedRemainingDuration carries the work still left,
// so the allocator can decide whether peers can absorb it.
void DroneApp::sendTaskDropNotification() {
    if (currentTaskId == -1) return;
    EV << "Reporting TaskDropNotification for Task #" << currentTaskId << ".\n";
    if (mainApp != nullptr) {
        mainApp->onTaskDropNotification(currentTaskId, droneId, savedRemainingDuration);
    }
    currentTaskId = -1;
    mbsMobility = nullptr;
}

// Convenience wrapper used by other state transitions: report a zero-work
// drop and return to IDLE without going through the fly-home / recharge
// machinery. (Kept for callers that already settled state themselves.)
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
