/*
 * DroneApp.cc
 *
 *  Created on: Mar 6, 2026
 *      Author: Guy Koyfman & Omer
 */

#include "DroneApp.h"
#include "inet/common/Protocol.h"
#include "inet/common/ProtocolTag_m.h"
Define_Module(uavswarmta::DroneApp);
namespace uavswarmta {

void DroneApp::initialize(int stage) {
    // --- שלב 0: אתחול משתנים מקומיים וחישובים לפני שהרשת מתעוררת ---
    if (stage == inet::INITSTAGE_LOCAL) {
        droneId = getParentModule()->getIndex();
        currentState = IDLE;
        currentTaskId = -1;
        mbsMobility = nullptr;

        currentEnergy = par("initialEnergy").doubleValue();
        idlePower = par("idlePower").doubleValue();
        travelPower = par("travelPower").doubleValue();
        workingPower = par("workingPower").doubleValue();
        lastEnergyUpdateTime = simTime();

        mobility = check_and_cast<inet::IMobility *>(getParentModule()->getSubmodule("mobility"));

        // We look "up" to the network level and then "down" to the mainNode
        cModule *mainNodeMod = getModuleByPath("mainNode");
        if (mainNodeMod) {
            auto mainMobility = check_and_cast<inet::IMobility *>(mainNodeMod->getSubmodule("mobility"));
            homePosition = mainMobility->getCurrentPosition();
        } else {
            // Fallback to local position if mainNode isn't found
            homePosition = mobility->getCurrentPosition();
            EV_WARN << "Drone " << droneId << ": mainNode not found, setting home to local start.\n";
        }

        // Define comm range
        commRange = par("drone_comm_range").doubleValue(); // Supports the 250m in your .ini
        commTimeoutDuration = par("commTimeoutDuration");

        energyThreshold = par("energyThreshold").doubleValue();
        rechargeDuration = par("rechargeDuration").doubleValue();
        rechargeTimer = new cMessage("rechargeTimer");

        arrivalTimer = new cMessage("arrivalTimer");
        taskCompletionTimer = new cMessage("taskCompletionTimer");
        connectionCheckTimer = new cMessage("connectionCheckTimer");
        connectionTimeoutTimer = new cMessage("connectionTimeoutTimer");
    }
    // --- שלב 3: הרשת באוויר, אפשר לפתוח פורטים (UDP) ---
    else if (stage == inet::INITSTAGE_APPLICATION_LAYER) {
        socket.setOutputGate(gate("socketOut"));
        // socket.setProtocol(&inet::Protocol::udp); // השורה הזו נשארת בהערה או נמחקת
        socket.bind(par("localPort"));
        socket.setCallback(this);
    }
}

bool DroneApp::hasConnection() {
    if (mbsMobility == nullptr) return false;

    double distance = mobility->getCurrentPosition().distance(mbsMobility->getCurrentPosition());
    return distance <= commRange;
}

void DroneApp::handleConnectionLoss() {
    EV << "Drone " << droneId << " lost connection! Pausing Task #" << currentTaskId
       << " and waiting for " << commTimeoutDuration << "s.\n";

    // Calculate remaining time and PAUSE the task timer
    if (taskCompletionTimer->isScheduled()) {
        savedRemainingDuration = (taskCompletionTimer->getArrivalTime() - simTime()).dbl();
        cancelEvent(taskCompletionTimer);
    } else {
        // Never started (lost connection exactly upon arrival)
        savedRemainingDuration = taskDuration;
    }

    currentState = WAITING_FOR_CONNECTION;

    // Schedule the timeout using your new variable
    scheduleAt(simTime() + commTimeoutDuration, connectionTimeoutTimer);
}

void DroneApp::updateEnergy() {
    if (currentState == DEAD) return; // Dead drones don't draw power

    simtime_t timePassed = simTime() - lastEnergyUpdateTime;
    double powerDraw = 0.0;

    if (currentState == IDLE || currentState == WAITING_FOR_CONNECTION) powerDraw = idlePower;
    else if (currentState == TRAVELLING || currentState == RETURNING_HOME || currentState == HEADING_TO_RECHARGE) powerDraw = travelPower;
    else if (currentState == PERFORMING_TASK) powerDraw = workingPower;
    else if (currentState == RECHARGING) powerDraw = 0.0; // Plugged in

    currentEnergy -= powerDraw * timePassed.dbl();
    lastEnergyUpdateTime = simTime();

    // Check for death first
    if (currentEnergy <= 0.0) {
        currentEnergy = 0.0;
        die();
    }
    // Check for threshold (only trigger if not already recharging/heading there)
    else if (currentEnergy <= energyThreshold &&
             currentState != HEADING_TO_RECHARGE &&
             currentState != RECHARGING) {
        initiateRecharge();
    }
}

void DroneApp::die() {
    EV << "Drone " << droneId << " HAS RUN OUT OF ENERGY! Shutting down completely.\n";
    currentState = DEAD;

    // Cancel all operations
    cancelEvent(arrivalTimer);
    cancelEvent(taskCompletionTimer);
    cancelEvent(connectionCheckTimer);
    cancelEvent(connectionTimeoutTimer);
    cancelEvent(rechargeTimer);

    // Change GUI icon tint to RED so you can visually see it died
    getParentModule()->getDisplayString().setTagArg("i", 1, "red");
}

inet::Coord DroneApp::findClosestRechargeStation() {
    inet::Coord myPos = mobility->getCurrentPosition();
    inet::Coord closestPos = homePosition; // Default to Main Node (Home)
    double minDistance = myPos.distance(homePosition);

    // God mode: iterate through MBSs to see if one is closer
    int numMbs = getParentModule()->getParentModule()->par("numMbs").intValue();
    for (int i = 0; i < numMbs; i++) {
        cModule *mbsModule = getModuleByPath(("^.^.mbs[" + std::to_string(i) + "]").c_str());
        if (mbsModule) {
            auto mbsMobility = check_and_cast<inet::IMobility *>(mbsModule->getSubmodule("mobility"));
            double dist = myPos.distance(mbsMobility->getCurrentPosition());
            if (dist < minDistance) {
                minDistance = dist;
                closestPos = mbsMobility->getCurrentPosition();
            }
        }
    }
    return closestPos;
}

void DroneApp::initiateRecharge() {
    EV << "Drone " << droneId << " energy critical! Aborting and heading to recharge.\n";

    // If working on a task, prep the drop notification
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

    // Change GUI to YELLOW while retreating
    getParentModule()->getDisplayString().setTagArg("i", 1, "yellow");

    inet::Coord targetStation = findClosestRechargeStation();

    double travelTime = mobility->getCurrentPosition().distance(targetStation) / par("speed").doubleValue();

    cancelEvent(arrivalTimer); // Cancel current travel if we were mid-flight
    scheduleAt(simTime() + travelTime, arrivalTimer);
}

// -----------------------------------------------------------------------
// GOD MODE ASSIGNMENT (Called directly by Main Node C++ code)
// -----------------------------------------------------------------------
void DroneApp::assignTask(int taskId, double syncedTravelTime, double duration, double targetX, double targetY, int mbsId) {
    //syncedTravelTime - the max travel time over all drones assigned to task
    updateEnergy();

    currentState = TRAVELLING;
    currentTaskId = taskId;
    taskDuration = duration;

    EV << "Drone " << droneId << " assigned Task #" << taskId
       << ". Synced arrival in: " << syncedTravelTime << "s.\n";

    // 2. Schedule the perfectly synced start time
    scheduleAt(simTime() + syncedTravelTime, arrivalTimer);
}

// =======================================================================
// MAIN MESSAGE ROUTER
// =======================================================================
void DroneApp::handleMessage(cMessage *msg) {
    // If the drone is dead, it ignores all timers and incoming packets
    if (currentState == DEAD) {
        delete msg;
        return;
    }

    if (msg == arrivalTimer) {
        handleArrival();
    } else if (msg == connectionCheckTimer) {
        handleConnectionCheck();
    } else if (msg == connectionTimeoutTimer) {
        handleConnectionTimeout();
    } else if (msg == taskCompletionTimer) {
        handleTaskCompletion();
    } else if (msg == rechargeTimer) {
        handleRechargeCompletion();
    } else if (msg->getKind() == inet::UDP_I_DATA) {
        socket.processMessage(msg);
    } else {
        delete msg; // Prevent memory leaks for unknown messages
    }
}

// =======================================================================
// SPECIFIC EVENT HANDLERS
// =======================================================================

void DroneApp::handleArrival() {
    updateEnergy();
    if (currentState == DEAD) return;

    if (currentState == TRAVELLING) {
        // SCENARIO 1: Arrived at Task
        if (hasConnection()) {
            currentState = PERFORMING_TASK;
            EV << "Drone " << droneId << " arrived and has connection. Starting Task #" << currentTaskId << ".\n";
            scheduleAt(simTime() + taskDuration, taskCompletionTimer);
        } else {
            EV << "Drone " << droneId << " arrived but MBS is missing! Waiting for connection...\n";
            handleConnectionLoss();
        }
        scheduleAt(simTime() + 1.0, connectionCheckTimer); // Always start checking connection

    } else if (currentState == RETURNING_HOME) {
        // SCENARIO 2: Arrived Home after failure
        currentState = IDLE;
        EV << "Drone " << droneId << " arrived home.\n";
        sendTaskDropNotification();

    } else if (currentState == HEADING_TO_RECHARGE) {
        // SCENARIO 3: Arrived at Recharge Station
        currentState = RECHARGING;
        EV << "Drone " << droneId << " arrived at station. Recharging for " << rechargeDuration << "s.\n";

        // If we abandoned a task to recharge, tell the Main Node now
        if (currentTaskId != -1) {
            sendTaskDropNotification();
        }
        scheduleAt(simTime() + rechargeDuration, rechargeTimer);
    }
}

void DroneApp::handleConnectionCheck() {
    if (currentState == PERFORMING_TASK) {
        if (!hasConnection()) {
            handleConnectionLoss(); // Lost it! Pause and wait.
        }
    } else if (currentState == WAITING_FOR_CONNECTION) {
        if (hasConnection()) {
            // CONNECTION RESTORED!
            EV << "Drone " << droneId << " regained connection! Resuming Task #" << currentTaskId << ".\n";
            cancelEvent(connectionTimeoutTimer);
            currentState = PERFORMING_TASK;
            scheduleAt(simTime() + savedRemainingDuration, taskCompletionTimer); // Resume work
        }
    }

    // Keep checking as long as we are at the task site
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

    double travelTime = mobility->getCurrentPosition().distance(homePosition) / par("speed").doubleValue();
    scheduleAt(simTime() + travelTime, arrivalTimer);
}

void DroneApp::handleTaskCompletion() {
    updateEnergy();
    if (currentState == DEAD) return;

    currentState = IDLE;
    cancelEvent(connectionCheckTimer);

    EV << "Drone " << droneId << " finished Task #" << currentTaskId << ".\n";

    inet::Packet *packet = new inet::Packet("TaskCompletion");
    auto chunk = inet::makeShared<TaskCompletion>();
    chunk->setTaskId(currentTaskId);
    chunk->setDroneId(droneId);
    chunk->setChunkLength(inet::B(16)); // התיקון הקריטי כאן
    packet->insertAtBack(chunk);

    // Add tags so the MessageDispatcher knows this is UDP
    packet->addTagIfAbsent<inet::PacketProtocolTag>()->setProtocol(&inet::Protocol::udp);

    inet::L3Address destAddr("10.0.0.1"); // Main Node IP
    socket.sendTo(packet, destAddr, par("destPort").intValue());

    currentTaskId = -1;
    mbsMobility = nullptr;
}

void DroneApp::handleRechargeCompletion() {
    // Your correct logic goes right here!
    currentEnergy = par("initialEnergy").doubleValue();
    lastEnergyUpdateTime = simTime();
    currentState = IDLE;

    EV << "Drone " << droneId << " fully recharged and ready.\n";
    getParentModule()->getDisplayString().setTagArg("i", 1, ""); // Clear visual warning
}

// Helper function to avoid writing the UDP send logic twice
void DroneApp::sendTaskDropNotification() {
    if (currentTaskId == -1) return; // Safety check

    EV << "Broadcasting TaskDropNotification for Task #" << currentTaskId << ".\n";
    inet::Packet *packet = new inet::Packet("TaskDropNotification");
    auto chunk = inet::makeShared<TaskDropNotification>();
    chunk->setTaskId(currentTaskId);
    chunk->setDroppingDroneId(droneId);
    chunk->setRemainingDuration(savedRemainingDuration);
    chunk->setChunkLength(inet::B(16)); // התיקון הקריטי כאן
    packet->insertAtBack(chunk);

    inet::L3Address destAddr("10.0.0.1"); // Main Node IP
    socket.sendTo(packet, destAddr, par("destPort").intValue());

    currentTaskId = -1;
    mbsMobility = nullptr;
}

void DroneApp::socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) {
    delete packet; // We'll add real logic to this later if the drone needs to receive packets
}

void DroneApp::socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) {
    delete indication;
}

void DroneApp::socketClosed(inet::UdpSocket *socket) {}

void DroneApp::dropTaskAndWait() {
    EV << "Drone " << droneId << " is dropping Task #" << currentTaskId << " due to connection loss/low battery!\n";

    if (currentTaskId != -1) {
        // 1. Create the Drop Notification Packet
        inet::Packet *packet = new inet::Packet("TaskDropNotification");
        auto chunk = inet::makeShared<TaskDropNotification>();

        chunk->setTaskId(currentTaskId);
        chunk->setDroppingDroneId(droneId);
        // For now, we'll send a dummy value of 0.0 or the total duration.
        chunk->setRemainingDuration(0.0);
        chunk->setChunkLength(inet::B(16)); // התיקון הקריטי גם כאן

        packet->insertAtBack(chunk);

        // 2. Send to Main Node (Assuming standard IPv4 setup)
        inet::L3Address destAddr("10.0.0.1"); // Replace with your Main Node's actual IP if different
        socket.sendTo(packet, destAddr, par("destPort").intValue());

        // 3. Clear the task
        currentTaskId = -1;
    }

    // 4. Update state and cancel timers
    currentState = IDLE;
}

} // namespace
