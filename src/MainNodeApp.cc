/*
 * MainNodeApp.cc
 *
 *  Created on: Mar 5, 2026
 *      Author: Guy Koyfman
 */
#include "MainNodeApp.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/mobility/contract/IMobility.h"
#include "inet/common/Protocol.h"
#include "inet/common/ProtocolTag_m.h"

#include "DroneApp.h"
#include "MbsApp.h"

Define_Module(uavswarmta::MainNodeApp);
namespace uavswarmta {



// -----------------------------------------------------------------------
// QUEUE SORTING LOGIC
// -----------------------------------------------------------------------
// Tells the cPacketQueue how to sort tasks by priority (Highest first)
int MainNodeApp::compareTaskPriority(cObject *a, cObject *b) {
    TaskNotification *taskA = check_and_cast<TaskNotification*>(a);
    TaskNotification *taskB = check_and_cast<TaskNotification*>(b);
    return taskB->getPriority() - taskA->getPriority();
}
// -----------------------------------------------------------------------
// INITIALIZATION
// -----------------------------------------------------------------------
void MainNodeApp::initialize(int stage) {
    if (stage == inet::INITSTAGE_LOCAL) {
        taskCounter = 0;
        maxQueueSize = 50;

        std::string algo = par("algorithmType").stringValue();
        if (algo == "AUCTION") {
            taskQueue.setup(compareTaskPriority);
        }

        // --- FIXED SOCKET SETUP ---
        int localPort = par("localPort");
        socket.setOutputGate(gate("socketOut"));

        // This line is crucial for the MessageDispatcher!
//        socket.setProtocol(&inet::Protocol::udp);

        socket.bind(localPort);
        socket.setCallback(this);

        generateTaskTimer = new cMessage("generateTaskTimer");
        scheduleAt(simTime() + par("taskGenerationInterval"), generateTaskTimer);
    }
}

// -----------------------------------------------------------------------
// TIMER & MESSAGE HANDLING
// -----------------------------------------------------------------------
void MainNodeApp::handleMessage(cMessage *msg) {
    if (msg->isSelfMessage()) {
            if (msg == generateTaskTimer) {
                if (msg == generateTaskTimer) {
                    if (taskQueue.getLength() < maxQueueSize){
                        generateNewTask();
                    }
                    if (taskCounter < par("taskLimit").intValue()) {
                        scheduleAt(simTime() + par("taskGenerationInterval"), generateTaskTimer);
                    } else {
                        EV << "Task limit of " << par("taskLimit").intValue() << " reached! No more tasks.\n";
                    }
                }
            }
    }
    else  {
        // INET 4.x recommends letting the socket handle all incoming gate messages
        socket.processMessage(msg);
    }
}

// -----------------------------------------------------------------------
// TASK GENERATION & QUEUE MANAGEMENT
// -----------------------------------------------------------------------
void MainNodeApp::generateNewTask() {
    taskCounter++;
    TaskNotification *newTask = new TaskNotification();
    newTask->setTaskId(taskCounter);
    newTask->setTargetX(uniform(0, 2000));
    newTask->setTargetY(uniform(0, 2000));
    newTask->setPriority(intuniform(1, 3));
    newTask->setRequiredDrones(intuniform(1, 3));
    newTask->setDuration(uniform(10, 60));

    // Ensure the task packet has protocol tags for the dispatcher
    newTask->addTagIfAbsent<inet::PacketProtocolTag>()->setProtocol(&inet::Protocol::udp);

    EV << "Generated Task #" << taskCounter << "\n";
    taskQueue.insert(newTask);
    tryAssignTask();
}
// -----------------------------------------------------------------------
// ASSIGNMENT LOGIC
// -----------------------------------------------------------------------
void MainNodeApp::tryAssignTask() {
    if (taskQueue.isEmpty()) return;

    std::string algo = par("algorithmType").stringValue();

    if (algo == "FIFO") {
        assignTaskFifo();
    } else if (algo == "AUCTION") {
        // assignTaskAuction(); // We will build this later!
    }
}

// -----------------------------------------------------------------------
// INCOMING MESSAGES (From Drones)
// -----------------------------------------------------------------------
void MainNodeApp::socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) {
    // Pop the custom data chunk out of the UDP packet
    auto chunk = packet->popAtFront<inet::Chunk>();

    // Check if it is a TaskCompletion message
    if (auto completion = inet::dynamicPtrCast<const TaskCompletion>(chunk)) {
        int taskId = completion->getTaskId();
        int droneId = completion->getDroneId();

        EV << "Main Node received TaskCompletion for Task #" << taskId << " from Drone " << droneId << ".\n";
        handleTaskUpdate(taskId, droneId, true);
    }
    // Check if it is a TaskDropNotification
    else if (auto drop = inet::dynamicPtrCast<const TaskDropNotification>(chunk)) {
        int taskId = drop->getTaskId();
        int droneId = drop->getDroppingDroneId();
        double remaining = drop->getRemainingDuration();

        EV << "Main Node received TaskDropNotification for Task #" << taskId << " from Drone " << droneId
           << ". Time remaining: " << remaining << "s.\n";

        handleTaskUpdate(taskId, droneId, false, remaining);
    }

    delete packet; // Prevent memory leaks
}
void MainNodeApp::handleTaskUpdate(int taskId, int droneId, bool isCompletion, double remainingDuration) {
    // If it was dropped, you might want to create a new task with the remainingDuration
    // and push it back into the queue here. For now, we will just log it.
    if (!isCompletion) {
        EV << "WARNING: Task #" << taskId << " was dropped by Drone " << droneId << "!\n";
        // TODO: Re-queue logic could go here
    }

    // Decrement the active drone counter for this task
    if (activeTaskDroneCount.find(taskId) != activeTaskDroneCount.end()) {
        activeTaskDroneCount[taskId]--;

        // Have all drones reported back?
        if (activeTaskDroneCount[taskId] <= 0) {
            EV << "All assigned drones have reported back for Task #" << taskId << ". Releasing MBS coverage.\n";

            // 1. Clean up tracking maps
            activeTaskDroneCount.erase(taskId);
            activeTaskLocations.erase(taskId);

            // 2. Remove the task from whichever MBS is covering it
            removeTaskFromMbs(taskId);

            // 3. Since resources (drones and an MBS slot) just freed up, immediately check the queue!
            tryAssignTask();
        }
    }
}

void MainNodeApp::removeTaskFromMbs(int taskId) {
    int totalMbs = getParentModule()->par("numMbs").intValue();

    // Search all MBSs to find which one is covering this task
    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);

        std::vector<int> currentTasks = mbsApp->getAssignedTasks();
        if (std::find(currentTasks.begin(), currentTasks.end(), taskId) != currentTasks.end()) {

            // Found it! Tell the MBS to drop the task.
            // If it has other tasks, it will stay active. If this was its last task, it becomes IDLE.
            mbsApp->removeTask(taskId);
            break;
        }
    }
}

void MainNodeApp::assignTaskFifo() {

    // Look at the first task in the queue (FIFO)
    TaskNotification *task = check_and_cast<TaskNotification *>(taskQueue.front());
    double targetX = task->getTargetX();
    double targetY = task->getTargetY();
    int reqDrones = task->getRequiredDrones();
    EV << "Trying to assign " << reqDrones << " drones to task " << task->getTaskId() << " at position (" <<targetX<<","<<targetY<<")\n";

    // 1. Find Closest Drones
    std::vector<cModule*> assignedDrones = findClosestIdleDrones(targetX, targetY, reqDrones);
    if (assignedDrones.size() < reqDrones){
        EV << "Couldn't find enough drones to assign to task: " << task->getTaskId() << "\n";
        return; // Not enough idle drones, stop.
    }

    // 2. Find Suitable MBS and calculate its target centroid
    double centroidX = 0, centroidY = 0;
    cModule* assignedMbs = findSuitableMbs(targetX, targetY, centroidX, centroidY);
    if (assignedMbs == nullptr) {
        EV << "Couldn't find MBS to cover task: " << task->getTaskId() << "\n";
        return; // No MBS can cover this right now, stop.
    }

    // 3. We have everything! Remove from queue and dispatch.
    taskQueue.pop(); // Remove it from the front
    dispatchUnits(task, assignedDrones, assignedMbs, centroidX, centroidY);

    delete task;

    // 4. Ripple Effect: Check if we can immediately assign the next task
    tryAssignTask();
}

std::vector<cModule*> MainNodeApp::findClosestIdleDrones(double targetX, double targetY, int reqDrones) {
    std::vector<std::pair<double, cModule*>> idleDrones;
    int totalDrones = getParentModule()->par("numDrones").intValue();

    // Gather all idle drones and their distances
    for (int i = 0; i < totalDrones; i++) {
        cModule *droneMod = getModuleByPath(("^.drone[" + std::to_string(i) + "].app[0]").c_str());
        auto droneApp = check_and_cast<uavswarmta::DroneApp *>(droneMod);

        if (droneApp->isIdle()) {
            auto dMob = check_and_cast<inet::IMobility *>(droneMod->getParentModule()->getSubmodule("mobility"));
            double dist = dMob->getCurrentPosition().distance(inet::Coord(targetX, targetY, 0));
            idleDrones.push_back({dist, droneMod});
        }
    }

    // Sort by distance (closest first)
    std::sort(idleDrones.begin(), idleDrones.end());

    // Pick the required number
    std::vector<cModule*> selectedDrones;
    for (int i = 0; i < reqDrones && i < idleDrones.size(); i++) {
        selectedDrones.push_back(idleDrones[i].second);
    }

    return selectedDrones;
}

cModule* MainNodeApp::findSuitableMbs(double targetX, double targetY, double& outCentroidX, double& outCentroidY) {
    int totalMbs = getParentModule()->par("numMbs").intValue();
    inet::Coord newTaskPos(targetX, targetY, 0);

    // Attempt A: Try to stretch an ACTIVE MBS
    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);

        if (!mbsApp->isIdle()) {
            std::vector<int> currentTasks = mbsApp->getAssignedTasks();
            double sumX = targetX, sumY = targetY;
            int count = 1;

            // Calculate proposed center
            for (int tid : currentTasks) {
                sumX += activeTaskLocations[tid].x;
                sumY += activeTaskLocations[tid].y;
                count++;
            }
            double cX = sumX / count;
            double cY = sumY / count;
            inet::Coord proposedCentroid(cX, cY, 0);

            // Verify if the new centroid keeps ALL tasks within comm range
            bool isValid = true;
            if (newTaskPos.distance(proposedCentroid) > commRange) isValid = false;

            for (int tid : currentTasks) {
                if (activeTaskLocations[tid].distance(proposedCentroid) > commRange) {
                    isValid = false;
                    break;
                }
            }

            if (isValid) {
                outCentroidX = cX;
                outCentroidY = cY;
                return mbsMod;
            }
        }
    }

    // Attempt B: If no active MBS can cover it, find an IDLE one
    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);

        if (mbsApp->isIdle()) {
            outCentroidX = targetX;
            outCentroidY = targetY;
            return mbsMod;
        }
    }

    return nullptr; // No MBS available
}

void MainNodeApp::dispatchUnits(TaskNotification* task, std::vector<cModule*>& drones, cModule* mbs, double centroidX, double centroidY) {
    int taskId = task->getTaskId();
    double targetX = task->getTargetX();
    double targetY = task->getTargetY();

    // Save to our active map for future MBS math
    activeTaskLocations[taskId] = inet::Coord(targetX, targetY, 0);

    // Track how many drones are assigned to this task
    activeTaskDroneCount[taskId] = drones.size();

    double maxTravelTime = 0.0;

    // Calculate MBS travel time
    auto mbsMob = check_and_cast<inet::IMobility *>(mbs->getParentModule()->getSubmodule("mobility"));
    double mbsDist = mbsMob->getCurrentPosition().distance(inet::Coord(centroidX, centroidY, 0));
    double mbsTravelTime = mbsDist / mbs->getParentModule()->par("speed").doubleValue();
    if (mbsTravelTime > maxTravelTime) maxTravelTime = mbsTravelTime;

    // Calculate Drones travel time
    for (cModule* dMod : drones) {
        auto dMob = check_and_cast<inet::IMobility *>(dMod->getParentModule()->getSubmodule("mobility"));
        double dDist = dMob->getCurrentPosition().distance(inet::Coord(targetX, targetY, 0));
        double dTravelTime = dDist / dMod->getParentModule()->par("speed").doubleValue();
        if (dTravelTime > maxTravelTime) maxTravelTime = dTravelTime;
    }

    // DISPATCH
    auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbs);
    int mbsId = mbs->getParentModule()->getIndex();

    if (mbsApp->isIdle()) {
        mbsApp->assignInitialTask(taskId, centroidX, centroidY);
    } else {
        mbsApp->addCoveredTask(taskId);
        mbsApp->shiftPosition(centroidX, centroidY);
    }

    for (cModule* dMod : drones) {
        auto droneApp = check_and_cast<uavswarmta::DroneApp *>(dMod);
        droneApp->assignTask(taskId, maxTravelTime, task->getDuration(), targetX, targetY, mbsId);
    }

    EV << "Dispatched Task #" << taskId << " to " << drones.size() << " drones and MBS " << mbsId
       << ". Centroid at (" << centroidX << ", " << centroidY << "). Sync wait: " << maxTravelTime << "s.\n";
}

} // namespace
