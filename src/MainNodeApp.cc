/*
 * MainNodeApp.cc
 */
#include "MainNodeApp.h"
#include <limits>
#include <iomanip>
#include <sstream>
#include "inet/mobility/contract/IMobility.h"

#include "DroneApp.h"
#include "MbsApp.h"

namespace uavswarmta {

Define_Module(MainNodeApp);

MainNodeApp::~MainNodeApp() {
    cancelAndDelete(generateTaskTimer);
    taskQueue.clear();
    if (csvOut.is_open()) csvOut.close();

    // Detach any leftover task markers so OMNeT++ can free them cleanly.
    if (getParentModule() && getParentModule()->getParentModule()) {
        cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();
        for (auto& kv : taskFigures) {
            canvas->removeFigure(kv.second);
            delete kv.second;
        }
    }
    taskFigures.clear();
}

// -----------------------------------------------------------------------
// QUEUE SORTING LOGIC
// -----------------------------------------------------------------------
int MainNodeApp::compareTaskPriority(cObject *a, cObject *b) {
    TaskNotification *taskA = check_and_cast<TaskNotification*>(a);
    TaskNotification *taskB = check_and_cast<TaskNotification*>(b);
    // Priority convention: 1 = HIGH urgency, 3 = LOW. Return negative when A
    // should come first, so subtract A from B's number... i.e. a-b gives us
    // ascending priority numbers (high-urgency at the front of the queue).
    return taskA->getPriority() - taskB->getPriority();
}

// -----------------------------------------------------------------------
// INITIALIZATION
// -----------------------------------------------------------------------
void MainNodeApp::initialize(int stage) {
    if (stage == 0) {
        taskCounter = 0;
        maxQueueSize = 50;
        commRange = 1e9;  // mainNode is "god mode"; no link is range-limited

        std::string algo = par("algorithmType").stringValue();
        if (algo == "AUCTION" || algo == "COST") {
            taskQueue.setup(compareTaskPriority);
        }

        // Cache cost-function tunables once; they're hot-path constants.
        costEnergyWeight        = par("costEnergyWeight").doubleValue();
        costHaltPriorityWeight  = par("costHaltPriorityWeight").doubleValue();
        costHaltGroupSizeWeight = par("costHaltGroupSizeWeight").doubleValue();
        costMbsRelocationWeight = par("costMbsRelocationWeight").doubleValue();

        generateTaskTimer = new cMessage("generateTaskTimer");
        scheduleAt(simTime() + par("taskGenerationInterval"), generateTaskTimer);

        // Cache the drone comm range -- this is the strictest constraint when
        // deciding whether an MBS reposition would strand a busy drone.
        cModule *drone0 = getModuleByPath("^.^.drone[0].app[0]");
        if (drone0 != nullptr) {
            droneCommRange = drone0->par("drone_comm_range").doubleValue();
        }

        openCsv();

        createStatsPanel();
        refreshStatsPanel();
    }
}

// -----------------------------------------------------------------------
// MESSAGE HANDLING
// -----------------------------------------------------------------------
void MainNodeApp::handleMessage(cMessage *msg) {
    if (msg == generateTaskTimer) {
        if (taskQueue.getLength() < maxQueueSize) {
            generateNewTask();
        }
        if (taskCounter < par("taskLimit").intValue()) {
            scheduleAt(simTime() + par("taskGenerationInterval"), generateTaskTimer);
        } else {
            EV << "Task limit of " << par("taskLimit").intValue() << " reached! No more tasks.\n";
        }
    }
    else {
        // No network gates anymore: anything else is unexpected.
        delete msg;
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
    newTask->setDuration(par("taskDuration").doubleValue());

    // Record metadata for later CSV row.
    TaskRecord rec;
    rec.taskId = taskCounter;
    rec.priority = newTask->getPriority();
    rec.requiredDrones = newTask->getRequiredDrones();
    rec.targetX = newTask->getTargetX();
    rec.targetY = newTask->getTargetY();
    rec.duration = newTask->getDuration();
    rec.generatedAt = simTime();
    rec.dispatchedAt = -1;
    rec.mbsId = -1;
    rec.dronesAssigned = 0;
    rec.dropEvents = 0;
    taskRecords[taskCounter] = rec;

    EV << "Generated Task #" << taskCounter << "\n";
    addTaskFigure(taskCounter, newTask->getTargetX(), newTask->getTargetY());
    taskQueue.insert(newTask);
    refreshStatsPanel();
    tryAssignTask();
}

void MainNodeApp::tryAssignTask() {
    if (taskQueue.isEmpty()) return;

    std::string algo = par("algorithmType").stringValue();
    if (algo == "FIFO") {
        assignTaskFifo();
    } else if (algo == "COST") {
        assignTaskCost();
    } else if (algo == "AUCTION") {
        // Legacy alias kept for backward compatibility with old .ini files.
        assignTaskCost();
    } else {
        throw cRuntimeError("Unknown algorithmType '%s' (expected 'FIFO' or 'COST')",
                            algo.c_str());
    }
}

// -----------------------------------------------------------------------
// DIRECT-CALL API (replaces UDP messages from drones)
// -----------------------------------------------------------------------
void MainNodeApp::onTaskCompletion(int taskId, int droneId) {
    Enter_Method("onTaskCompletion");
    EV << "Main Node received TaskCompletion for Task #" << taskId
       << " from Drone " << droneId << ".\n";
    handleTaskUpdate(taskId, droneId, true);
}

void MainNodeApp::onTaskDropNotification(int taskId, int droneId, double remainingDuration) {
    Enter_Method("onTaskDropNotification");
    EV << "Main Node received TaskDropNotification for Task #" << taskId
       << " from Drone " << droneId
       << ". Time remaining: " << remainingDuration << "s.\n";
    handleTaskUpdate(taskId, droneId, false, remainingDuration);
}

void MainNodeApp::handleTaskUpdate(int taskId, int droneId, bool isCompletion, double remainingDuration) {
    // Drone failure path (death, recharge, connection timeout):
    //   * survivors stay on the task -- they just take longer to finish
    //     (duration scales by oldCount/newCount, per "fewer drones = slower").
    //   * if the failing drone was the LAST one, the task is marked DROPPED.
    //
    // Completion path:
    //   * just decrement; when count reaches 0 the task is marked COMPLETED.
    //     (Survivors don't need extension on a completion -- the completer
    //      already finished its share of the work.)
    auto rit = taskRecords.find(taskId);
    if (rit != taskRecords.end() && !isCompletion) rit->second.dropEvents++;

    auto cit = activeTaskDroneCount.find(taskId);
    if (cit == activeTaskDroneCount.end()) return;

    int oldCount = cit->second;
    int newCount = oldCount - 1;
    cit->second = newCount;

    if (!isCompletion) {
        EV << "WARNING: drone " << droneId << " bailed on Task #" << taskId
           << " (now " << newCount << "/" << oldCount << " drones).\n";
    }

    if (newCount > 0) {
        if (!isCompletion) {
            // The team shrank but the task continues; survivors do extra work.
            double scale = (double)oldCount / (double)newCount;
            scaleRemainingDurationForTask(taskId, droneId, scale);
        }
        return;
    }

    // newCount == 0 -- this was the last drone. Finalize the task.
    EV << "Task #" << taskId << " has no drones left ("
       << (isCompletion ? "completed" : "all bailed") << ").\n";
    finalizeTask(taskId, isCompletion ? "COMPLETED" : "DROPPED");
}

void MainNodeApp::removeTaskFromMbs(int taskId) {
    int totalMbs = getParentModule()->getParentModule()->par("numMbs").intValue();

    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);

        std::vector<int> currentTasks = mbsApp->getAssignedTasks();
        if (std::find(currentTasks.begin(), currentTasks.end(), taskId) != currentTasks.end()) {
            mbsApp->removeTask(taskId);
            break;
        }
    }
}

// -----------------------------------------------------------------------
// SHARED HELPERS for "some drones leave, others slow down"
// -----------------------------------------------------------------------
// Tell every drone still on `taskId` (except the one leaving) to extend its
// remaining work by `scale`. Skipping the leaver is important because the
// caller is in the middle of removing it, and its bookkeeping state hasn't
// settled yet.
void MainNodeApp::scaleRemainingDurationForTask(int taskId, int leavingDroneId, double scale) {
    if (scale <= 1.0) return;
    int totalDrones = getParentModule()->getParentModule()->par("numDrones").intValue();
    int affected = 0;
    for (int i = 0; i < totalDrones; i++) {
        if (i == leavingDroneId) continue;
        cModule *dm = getModuleByPath(("^.^.drone[" + std::to_string(i) + "].app[0]").c_str());
        auto dApp = check_and_cast<uavswarmta::DroneApp *>(dm);
        if (dApp->getCurrentTaskId() == taskId) {
            dApp->extendRemainingDuration(scale);
            affected++;
        }
    }
    EV << "Task #" << taskId << " survivors (" << affected
       << " drones) had remaining duration scaled by " << scale << "x.\n";
}

// Bookkeeping + CSV row for a task that has ended (one way or another).
void MainNodeApp::finalizeTask(int taskId, const std::string& outcome) {
    if (outcome == "DROPPED") tasksDropped++;
    else                      tasksCompleted++;

    auto rit = taskRecords.find(taskId);
    if (rit != taskRecords.end()) {
        writeCsvRow(rit->second, outcome);
        taskRecords.erase(rit);
    }

    activeTaskDroneCount.erase(taskId);
    activeTaskLocations.erase(taskId);
    activeTaskHadDrop.erase(taskId);
    removeTaskFigure(taskId);
    removeTaskFromMbs(taskId);
    refreshStatsPanel();
    tryAssignTask();
}

// Bookkeeping for a single COST-allocator preemption.
//   * if other drones remain on the old task, they pick up the slack
//     (longer duration, no penalty to stats)
//   * if this was the LAST drone, the old task is marked DROPPED -- the
//     allocator chose to sacrifice it for a higher-priority task, and that
//     decision shows up in the stats so it can be measured/tuned.
void MainNodeApp::handlePreemption(int oldTaskId, int leavingDroneId, int newTaskId) {
    auto cit = activeTaskDroneCount.find(oldTaskId);
    if (cit == activeTaskDroneCount.end()) return;

    int oldCount = cit->second;
    int newCount = oldCount - 1;
    cit->second = newCount;

    EV << "COST: preempting drone " << leavingDroneId << " from Task #"
       << oldTaskId << " for new Task #" << newTaskId
       << " (group " << oldCount << " -> " << newCount << ").\n";

    if (newCount > 0) {
        double scale = (double)oldCount / (double)newCount;
        scaleRemainingDurationForTask(oldTaskId, leavingDroneId, scale);
    } else {
        EV << "Task #" << oldTaskId << " lost its last drone to preemption; "
           << "marking as DROPPED.\n";
        finalizeTask(oldTaskId, "DROPPED");
    }
}

// -----------------------------------------------------------------------
// FIFO ASSIGNMENT
// -----------------------------------------------------------------------
void MainNodeApp::assignTaskFifo() {
    TaskNotification *task = check_and_cast<TaskNotification *>(taskQueue.front());
    double targetX = task->getTargetX();
    double targetY = task->getTargetY();
    int reqDrones = task->getRequiredDrones();
    EV << "Trying to assign " << reqDrones << " drones to task " << task->getTaskId()
       << " at position (" << targetX << "," << targetY << ")\n";

    std::vector<cModule*> assignedDrones = findClosestIdleDrones(targetX, targetY, reqDrones);
    if ((int)assignedDrones.size() < reqDrones) {
        EV << "Couldn't find enough drones to assign to task: " << task->getTaskId() << "\n";
        return;
    }

    double centroidX = 0, centroidY = 0;
    cModule* assignedMbs = findSuitableMbs(targetX, targetY, centroidX, centroidY);
    if (assignedMbs == nullptr) {
        EV << "Couldn't find MBS to cover task: " << task->getTaskId() << "\n";
        return;
    }

    taskQueue.pop();
    dispatchUnits(task, assignedDrones, assignedMbs, centroidX, centroidY);

    delete task;
    tryAssignTask();
}

// -----------------------------------------------------------------------
// COST ASSIGNMENT (auction with preemption, per the project paper)
// -----------------------------------------------------------------------
// The COST allocator mirrors the algorithm in the preliminary report:
//   step 1 - pick the lowest-cost set of UAVs (idle OR busy on a lower-prio
//            task that we are willing to halt)
//   step 2 - pick the lowest-cost MBS to cover the new task site
// Cost weights are NED params so the math can be tuned at run time.
//
// PRIORITY CONVENTION: integer 1..3 where 1 = HIGH urgency, 3 = LOW urgency
// (matches generateNewTask's intuniform(1,3)). Lower number = more important.
// A busy drone is preemptible only when its currentPriority > newPriority.
//
// The two helpers below are the *only* place the cost math lives -- keep them
// short. Multipliers come from NED params named cost*.
// -----------------------------------------------------------------------

double MainNodeApp::computeHaltPenalty(int currentPriority, int currentGroupSize) {
    // halt(t) -- the punishment for abandoning the task currently being run.
    // Higher-priority (lower-numbered) tasks are MORE painful to halt, so the
    // priority term is (4 - p): priority 1 -> 3x weight, priority 3 -> 1x.
    // The group-size term scales with how many drones we'd be disrupting.
    return costHaltPriorityWeight * (4 - currentPriority)
         + costHaltGroupSizeWeight * currentGroupSize;
}

double MainNodeApp::computeDroneCost(cModule* droneMod, double targetX, double targetY, int newTaskPriority) {
    auto droneApp = check_and_cast<uavswarmta::DroneApp *>(droneMod);
    auto dMob = check_and_cast<inet::IMobility *>(
        droneMod->getParentModule()->getSubmodule("mobility"));

    double dist = dMob->getCurrentPosition().distance(
        inet::Coord(targetX, targetY, dMob->getCurrentPosition().z));

    // c(u, t) = e(u, t) + halt(currentTask) -- per the paper.
    double energyCost = costEnergyWeight * dist;
    double haltCost   = 0.0;
    int oldTaskId = -1, oldPriority = -1, groupSize = 0;

    if (!droneApp->isIdle()) {
        oldTaskId = droneApp->getCurrentTaskId();
        oldPriority = 99;
        auto rit = taskRecords.find(oldTaskId);
        if (rit != taskRecords.end()) oldPriority = rit->second.priority;
        auto cit = activeTaskDroneCount.find(oldTaskId);
        if (cit != activeTaskDroneCount.end()) groupSize = cit->second;
        haltCost = computeHaltPenalty(oldPriority, groupSize);
    }

    int droneIdx = droneMod->getParentModule()->getIndex();
    EV << "  [COST/drone] D" << droneIdx
       << (droneApp->isIdle() ? " (idle)"
                              : (std::string(" (busy on T") + std::to_string(oldTaskId)
                                 + " prio=" + std::to_string(oldPriority)
                                 + " group=" + std::to_string(groupSize) + ")").c_str())
       << " dist=" << dist
       << " energyCost=" << energyCost
       << " haltCost=" << haltCost
       << " TOTAL=" << (energyCost + haltCost) << "\n";

    return energyCost + haltCost;
}

std::vector<cModule*> MainNodeApp::findCostMinimalDrones(
        double targetX, double targetY, int reqDrones, int newPriority) {
    // Eligible pool A = { idle drones } U { busy drones with a STRICTLY
    // lower-priority current task } (per step 1 of the algorithm). Dead
    // drones and equally-or-more-important busy drones are excluded.
    std::vector<std::pair<double, cModule*>> ranked;
    int totalDrones = getParentModule()->getParentModule()->par("numDrones").intValue();

    EV << " --- AUCTION drone scoring (need " << reqDrones
       << ", newPrio=" << newPriority << ") ---\n";
    int skippedDead = 0, skippedBusyHighPrio = 0;

    for (int i = 0; i < totalDrones; i++) {
        cModule *droneMod = getModuleByPath(("^.^.drone[" + std::to_string(i) + "].app[0]").c_str());
        auto droneApp = check_and_cast<uavswarmta::DroneApp *>(droneMod);
        if (!droneApp->isAlive()) { skippedDead++; continue; }

        bool eligible = false;
        if (droneApp->isIdle()) {
            eligible = true;
        } else {
            int oldTaskId = droneApp->getCurrentTaskId();
            auto rit = taskRecords.find(oldTaskId);
            if (rit != taskRecords.end() && rit->second.priority > newPriority) {
                eligible = true;   // current task is less urgent -> preemptible
            }
        }
        if (!eligible) { skippedBusyHighPrio++; continue; }

        double c = computeDroneCost(droneMod, targetX, targetY, newPriority);
        ranked.push_back({c, droneMod});
    }

    EV << " --- (" << ranked.size() << " eligible, " << skippedDead
       << " dead, " << skippedBusyHighPrio << " locked on equal/higher prio) ---\n";

    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<double, cModule*>& a, const std::pair<double, cModule*>& b) {
                  return a.first < b.first;
              });

    std::vector<cModule*> picked;
    for (int i = 0; i < reqDrones && i < (int)ranked.size(); i++) {
        picked.push_back(ranked[i].second);
        int idx = ranked[i].second->getParentModule()->getIndex();
        EV << " >> AUCTION picked drone D" << idx
           << " with cost " << ranked[i].first << "\n";
    }
    return picked;
}

cModule* MainNodeApp::findCostMinimalMbs(
        double targetX, double targetY, int newPriority,
        double& outCentroidX, double& outCentroidY) {
    // Step 2: pick the MBS that minimises c(m) = halt-of-its-tasks + relocation.
    // Three buckets of MBSs are evaluated uniformly:
    //   (a) idle MBS                       -> c = relocation distance
    //   (b) active MBS that can EXTEND to cover the new task (no drop)
    //                                      -> c = relocation distance
    //   (c) active MBS whose current tasks all have priority > newPriority,
    //       i.e. we are willing to drop them -> c = relocation + halt sum
    // Active MBSs holding a higher-or-equal-priority task are skipped.
    inet::Coord newTaskPos(targetX, targetY, 0);
    int totalMbs = getParentModule()->getParentModule()->par("numMbs").intValue();

    cModule* bestMbs = nullptr;
    double bestCost = std::numeric_limits<double>::infinity();
    double bestCx = targetX, bestCy = targetY;
    int bestIdx = -1;

    EV << " --- AUCTION MBS scoring (newPrio=" << newPriority << ") ---\n";

    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);
        auto mob = check_and_cast<inet::IMobility *>(
            mbsMod->getParentModule()->getSubmodule("mobility"));
        inet::Coord mbsPos = mob->getCurrentPosition();

        double proposedCx = targetX, proposedCy = targetY;
        double haltSum = 0.0;
        bool feasible = false;
        const char* mode = "IDLE";

        if (mbsApp->isIdle()) {
            feasible = true;
        } else {
            // First try EXTEND: can the MBS shift to a centroid that still covers
            // all of its tasks + the new task? (No drops, no halt cost.)
            std::vector<int> currentTasks = mbsApp->getAssignedTasks();
            double sumX = targetX, sumY = targetY;
            int count = 1;
            for (int tid : currentTasks) {
                sumX += activeTaskLocations[tid].x;
                sumY += activeTaskLocations[tid].y;
                count++;
            }
            double cX = sumX / count, cY = sumY / count;
            inet::Coord centroid(cX, cY, 0);
            double mbsRange = mbsApp->getCommRange();
            double effectiveRange = (droneCommRange > 0 && droneCommRange < mbsRange)
                                        ? droneCommRange : mbsRange;
            bool extendOk = (newTaskPos.distance(centroid) <= effectiveRange);
            for (int tid : currentTasks) {
                if (activeTaskLocations[tid].distance(centroid) > effectiveRange) {
                    extendOk = false; break;
                }
            }

            if (extendOk) {
                proposedCx = cX; proposedCy = cY;
                feasible = true;
                mode = "EXTEND";
            } else {
                // Falling back to RELOCATE-AND-DROP: only valid if every current
                // task is less urgent than the new one.
                bool allLower = true;
                for (int tid : currentTasks) {
                    auto rit = taskRecords.find(tid);
                    int p = (rit != taskRecords.end()) ? rit->second.priority : 99;
                    if (p <= newPriority) { allLower = false; break; }
                    int gs = activeTaskDroneCount.count(tid) ? activeTaskDroneCount.at(tid) : 0;
                    haltSum += computeHaltPenalty(p, gs);
                }
                if (allLower) { feasible = true; mode = "DROP"; }
                else          { mode = "LOCKED"; }
            }
        }

        if (!feasible) {
            EV << "  [COST/mbs] M" << i << " " << mode << " -> infeasible\n";
            continue;
        }

        double relocDist = mbsPos.distance(inet::Coord(proposedCx, proposedCy, mbsPos.z));
        double cost = costMbsRelocationWeight * relocDist + haltSum;

        EV << "  [COST/mbs] M" << i << " " << mode
           << " relocDist=" << relocDist
           << " haltSum=" << haltSum
           << " TOTAL=" << cost << "\n";

        if (cost < bestCost) {
            bestCost = cost;
            bestMbs = mbsMod;
            bestCx = proposedCx;
            bestCy = proposedCy;
            bestIdx = i;
        }
    }

    if (bestMbs != nullptr) {
        outCentroidX = bestCx;
        outCentroidY = bestCy;
        EV << " >> AUCTION picked MBS M" << bestIdx
           << " centroid=(" << bestCx << "," << bestCy
           << ") cost=" << bestCost << "\n";
    } else {
        EV << " >> AUCTION: NO feasible MBS!\n";
    }
    return bestMbs;
}

void MainNodeApp::preemptAndDispatchUnits(TaskNotification* task,
        std::vector<cModule*>& drones, cModule* mbs,
        double centroidX, double centroidY) {
    // Before the normal dispatch sequence, any drone we picked that's currently
    // busy must release its old task. We do the accounting HERE on the main
    // node (decrement counters, mark drop, possibly finalise) and then call
    // DroneApp::preempt() which just cleans local state.
    //
    // Order matters: bookkeep ALL preemptions first (so the cascading
    // finalisations / new tryAssignTask() calls happen against a coherent
    // world), THEN call preempt() on the drones, THEN dispatch the new task.

    struct Preemption { cModule* drone; int oldTaskId; int droneId; };
    std::vector<Preemption> preemptions;
    for (cModule* dMod : drones) {
        auto dApp = check_and_cast<uavswarmta::DroneApp *>(dMod);
        if (!dApp->isIdle()) {
            preemptions.push_back({dMod, dApp->getCurrentTaskId(),
                                   dMod->getParentModule()->getIndex()});
        }
    }

    for (auto& p : preemptions) {
        // handlePreemption decrements activeTaskDroneCount and either
        //   (a) extends the survivors' duration if peers remain, or
        //   (b) re-queues the old task if this was the last drone on it.
        // Either way, the old task is NOT dropped.
        handlePreemption(p.oldTaskId, p.droneId, task->getTaskId());
    }
    for (auto& p : preemptions) {
        auto dApp = check_and_cast<uavswarmta::DroneApp *>(p.drone);
        dApp->preempt();
    }

    dispatchUnits(task, drones, mbs, centroidX, centroidY);
}

void MainNodeApp::assignTaskCost() {
    TaskNotification *task = check_and_cast<TaskNotification *>(taskQueue.front());
    double targetX = task->getTargetX();
    double targetY = task->getTargetY();
    int reqDrones = task->getRequiredDrones();
    int priority  = task->getPriority();
    EV << "\n===== AUCTION ROUND for Task #" << task->getTaskId()
       << " prio=" << priority << " needs=" << reqDrones
       << " at (" << targetX << "," << targetY
       << ") | queueDepth=" << taskQueue.getLength() << " =====\n";

    std::vector<cModule*> assignedDrones =
        findCostMinimalDrones(targetX, targetY, reqDrones, priority);
    if ((int)assignedDrones.size() < reqDrones) {
        EV << "COST: not enough eligible drones for task " << task->getTaskId() << "\n";
        return;
    }

    double centroidX = 0, centroidY = 0;
    cModule* assignedMbs = findCostMinimalMbs(targetX, targetY, priority, centroidX, centroidY);
    if (assignedMbs == nullptr) {
        EV << "COST: no MBS available for task " << task->getTaskId() << "\n";
        return;
    }

    taskQueue.pop();
    preemptAndDispatchUnits(task, assignedDrones, assignedMbs, centroidX, centroidY);

    delete task;
    tryAssignTask();
}

std::vector<cModule*> MainNodeApp::findClosestIdleDrones(double targetX, double targetY, int reqDrones) {
    std::vector<std::pair<double, cModule*>> idleDrones;
    int totalDrones = getParentModule()->getParentModule()->par("numDrones").intValue();

    for (int i = 0; i < totalDrones; i++) {
        cModule *droneMod = getModuleByPath(("^.^.drone[" + std::to_string(i) + "].app[0]").c_str());
        auto droneApp = check_and_cast<uavswarmta::DroneApp *>(droneMod);

        if (droneApp->isIdle()) {
            auto dMob = check_and_cast<inet::IMobility *>(droneMod->getParentModule()->getSubmodule("mobility"));
            double dist = dMob->getCurrentPosition().distance(inet::Coord(targetX, targetY, 0));
            idleDrones.push_back({dist, droneMod});
        }
    }

    std::sort(idleDrones.begin(), idleDrones.end());

    std::vector<cModule*> selectedDrones;
    for (int i = 0; i < reqDrones && i < (int)idleDrones.size(); i++) {
        selectedDrones.push_back(idleDrones[i].second);
    }
    return selectedDrones;
}

cModule* MainNodeApp::findSuitableMbs(double targetX, double targetY, double& outCentroidX, double& outCentroidY) {
    int totalMbs = getParentModule()->getParentModule()->par("numMbs").intValue();
    inet::Coord newTaskPos(targetX, targetY, 0);

    // Attempt A: extend an ACTIVE MBS to also cover this task.
    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);

        if (!mbsApp->isIdle()) {
            std::vector<int> currentTasks = mbsApp->getAssignedTasks();
            double sumX = targetX, sumY = targetY;
            int count = 1;
            for (int tid : currentTasks) {
                sumX += activeTaskLocations[tid].x;
                sumY += activeTaskLocations[tid].y;
                count++;
            }
            double cX = sumX / count;
            double cY = sumY / count;
            inet::Coord proposedCentroid(cX, cY, 0);

            // The strictest cap is the DRONE's comm range -- a drone parked at a
            // task site checks its distance to the MBS using its own range. If we
            // moved the MBS beyond that, busy drones would lose connection.
            double mbsRange = mbsApp->getCommRange();
            double effectiveRange = (droneCommRange > 0 && droneCommRange < mbsRange)
                                        ? droneCommRange : mbsRange;

            bool isValid = true;
            if (newTaskPos.distance(proposedCentroid) > effectiveRange) isValid = false;
            for (int tid : currentTasks) {
                if (activeTaskLocations[tid].distance(proposedCentroid) > effectiveRange) {
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

    // Attempt B: pick the IDLE MBS whose current position is closest to the task.
    cModule* bestMbs = nullptr;
    double bestDist = std::numeric_limits<double>::infinity();
    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);
        if (mbsApp->isIdle()) {
            auto mob = check_and_cast<inet::IMobility *>(
                mbsMod->getParentModule()->getSubmodule("mobility"));
            double d = mob->getCurrentPosition().distance(newTaskPos);
            if (d < bestDist) {
                bestDist = d;
                bestMbs = mbsMod;
            }
        }
    }
    if (bestMbs != nullptr) {
        outCentroidX = targetX;
        outCentroidY = targetY;
        return bestMbs;
    }

    return nullptr;
}

void MainNodeApp::dispatchUnits(TaskNotification* task, std::vector<cModule*>& drones, cModule* mbs, double centroidX, double centroidY) {
    int taskId = task->getTaskId();
    double targetX = task->getTargetX();
    double targetY = task->getTargetY();

    activeTaskLocations[taskId] = inet::Coord(targetX, targetY, 0);
    activeTaskDroneCount[taskId] = drones.size();
    activeTaskHadDrop[taskId] = false;

    // Stamp dispatch metadata on the record.
    auto rit = taskRecords.find(taskId);
    if (rit != taskRecords.end()) {
        rit->second.dispatchedAt = simTime();
        rit->second.dronesAssigned = (int)drones.size();
    }

    double maxTravelTime = 0.0;

    // MBS travel time
    auto mbsMob = check_and_cast<inet::IMobility *>(mbs->getParentModule()->getSubmodule("mobility"));
    double mbsDist = mbsMob->getCurrentPosition().distance(inet::Coord(centroidX, centroidY, 0));
    double mbsTravelTime = mbsDist / mbs->par("speed").doubleValue();
    if (mbsTravelTime > maxTravelTime) maxTravelTime = mbsTravelTime;

    // Drone travel times
    for (cModule* dMod : drones) {
        auto dMob = check_and_cast<inet::IMobility *>(dMod->getParentModule()->getSubmodule("mobility"));
        double dDist = dMob->getCurrentPosition().distance(inet::Coord(targetX, targetY, 0));
        double dTravelTime = dDist / dMod->par("speed").doubleValue();
        if (dTravelTime > maxTravelTime) maxTravelTime = dTravelTime;
    }

    auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbs);
    int mbsId = mbs->getParentModule()->getIndex();

    // Stamp the chosen MBS on the record now that we have it.
    if (rit != taskRecords.end()) {
        rit->second.mbsId = mbsId;
    }

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

    markTaskActive(taskId);
    refreshStatsPanel();

    EV << "Dispatched Task #" << taskId << " to " << drones.size() << " drones and MBS " << mbsId
       << ". Centroid at (" << centroidX << ", " << centroidY << "). Sync wait: " << maxTravelTime << "s.\n";
}

// -----------------------------------------------------------------------
// CANVAS MARKERS (visible in Qtenv)
// -----------------------------------------------------------------------
void MainNodeApp::addTaskFigure(int taskId, double x, double y) {
    cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();

    auto group = new cGroupFigure(("task" + std::to_string(taskId)).c_str());

    // Halo so the task is visible from far away.
    auto halo = new cOvalFigure("halo");
    halo->setBounds(cFigure::Rectangle(x - 35, y - 35, 70, 70));
    halo->setLineColor(cFigure::Color("red"));
    halo->setLineWidth(2);
    halo->setFilled(true);
    halo->setFillColor(cFigure::Color("yellow"));
    halo->setFillOpacity(0.25);
    group->addFigure(halo);

    // Crosshair dot at the exact spot.
    auto dot = new cOvalFigure("dot");
    dot->setBounds(cFigure::Rectangle(x - 5, y - 5, 10, 10));
    dot->setLineColor(cFigure::Color("red"));
    dot->setFilled(true);
    dot->setFillColor(cFigure::Color("red"));
    group->addFigure(dot);

    // Label "T<id>" placed to the right of the dot so it does not overlap the halo.
    auto text = new cTextFigure("label");
    text->setPosition(cFigure::Point(x + 12, y));
    text->setText(("T" + std::to_string(taskId)).c_str());
    text->setAnchor(cFigure::ANCHOR_W);
    text->setColor(cFigure::Color("black"));
    text->setFont(cFigure::Font("", 14, cFigure::FONT_BOLD));
    group->addFigure(text);

    canvas->addFigure(group);
    taskFigures[taskId] = group;
    refreshStatsPanel();
}

void MainNodeApp::markTaskActive(int taskId) {
    auto it = taskFigures.find(taskId);
    if (it == taskFigures.end()) return;
    // Turn the halo green to signal "someone is on it".
    auto group = check_and_cast<cGroupFigure *>(it->second);
    if (auto halo = dynamic_cast<cOvalFigure *>(group->getFigure(0))) {
        halo->setLineColor(cFigure::Color("green"));
        halo->setFillColor(cFigure::Color("green"));
    }
}

void MainNodeApp::removeTaskFigure(int taskId) {
    auto it = taskFigures.find(taskId);
    if (it == taskFigures.end()) return;
    cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();
    canvas->removeFigure(it->second);
    delete it->second;
    taskFigures.erase(it);
    refreshStatsPanel();
}

// -----------------------------------------------------------------------
// HUD (top-left of the network canvas, updates every state change)
// -----------------------------------------------------------------------
void MainNodeApp::createStatsPanel() {
    cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();

    // Background panel (top-left of the canvas)
    auto bg = new cRectangleFigure("statsBg");
    bg->setBounds(cFigure::Rectangle(10, 10, 420, 220));
    bg->setLineColor(cFigure::Color("black"));
    bg->setLineWidth(2);
    bg->setFilled(true);
    bg->setFillColor(cFigure::Color("white"));
    bg->setFillOpacity(0.9);
    canvas->addFigure(bg);

    // Text on top
    statsPanel = new cTextFigure("statsText");
    statsPanel->setPosition(cFigure::Point(25, 25));
    statsPanel->setAnchor(cFigure::ANCHOR_NW);
    statsPanel->setColor(cFigure::Color("black"));
    statsPanel->setFont(cFigure::Font("", 18, cFigure::FONT_BOLD));
    canvas->addFigure(statsPanel);
}

void MainNodeApp::refreshStatsPanel() {
    if (statsPanel == nullptr) return;
    int queued = taskQueue.getLength();
    int active = (int)activeTaskDroneCount.size();
    int generated = taskCounter;
    int taskLimit = par("taskLimit").intValue();

    std::string s = "== Task Queue ==\n";
    s += "Generated : " + std::to_string(generated) + " / " + std::to_string(taskLimit) + "\n";
    s += "Queued    : " + std::to_string(queued) + "\n";
    s += "In flight : " + std::to_string(active) + "\n";
    s += "Completed : " + std::to_string(tasksCompleted) + "\n";
    s += "Dropped   : " + std::to_string(tasksDropped);
    statsPanel->setText(s.c_str());
}

// -----------------------------------------------------------------------
// CSV per-task logging
// -----------------------------------------------------------------------
void MainNodeApp::openCsv() {
    // One file per run: results/<config>-<run>-tasks.csv
    cConfigurationEx *cfg = getEnvir()->getConfigEx();
    std::string configName = cfg->getActiveConfigName();
    int runNumber = cfg->getActiveRunNumber();
    std::string algo = par("algorithmType").stringValue();

    std::ostringstream path;
    path << "results/" << configName
         << "-run" << runNumber
         << "-" << algo
         << "-tasks.csv";

    // Make sure results/ exists (cheap, no-op if already there).
    std::system("mkdir -p results");

    csvOut.open(path.str());
    if (!csvOut.is_open()) {
        EV_WARN << "Could not open CSV file " << path.str() << "\n";
        return;
    }

    csvOut << "run,algorithm,taskId,priority,requiredDrones,dronesAssigned,"
              "mbsId,targetX,targetY,duration,generatedAt,dispatchedAt,"
              "finalizedAt,waitTime,turnaroundTime,dropEvents,outcome\n";
}

void MainNodeApp::writeCsvRow(const TaskRecord& r, const std::string& outcome) {
    if (!csvOut.is_open()) return;

    cConfigurationEx *cfg = getEnvir()->getConfigEx();
    int runNumber = cfg->getActiveRunNumber();
    std::string algo = par("algorithmType").stringValue();
    simtime_t now = simTime();

    // waitTime    = dispatch - generation (queue latency)
    // turnaround  = finalize - generation (total time in system)
    double waitTime = (r.dispatchedAt >= SIMTIME_ZERO)
                        ? (r.dispatchedAt - r.generatedAt).dbl()
                        : -1.0;
    double turnaround = (now - r.generatedAt).dbl();

    csvOut << runNumber << ","
           << algo << ","
           << r.taskId << ","
           << r.priority << ","
           << r.requiredDrones << ","
           << r.dronesAssigned << ","
           << r.mbsId << ","
           << r.targetX << ","
           << r.targetY << ","
           << r.duration << ","
           << r.generatedAt.dbl() << ","
           << (r.dispatchedAt >= SIMTIME_ZERO ? r.dispatchedAt.dbl() : -1.0) << ","
           << now.dbl() << ","
           << waitTime << ","
           << turnaround << ","
           << r.dropEvents << ","
           << outcome << "\n";
}

void MainNodeApp::finish() {
    // Emit a row for any task that was still in-flight when the sim ended,
    // so the CSV gives a complete picture of every generated task.
    for (auto& kv : taskRecords) {
        std::string outcome = (kv.second.dispatchedAt >= SIMTIME_ZERO)
                                ? "UNFINISHED" : "QUEUED";
        writeCsvRow(kv.second, outcome);
    }
    taskRecords.clear();

    if (csvOut.is_open()) {
        csvOut.flush();
        csvOut.close();
    }

    // Also record summary scalars so opp_scavetool / IDE plots can use them.
    recordScalar("tasksGenerated", taskCounter);
    recordScalar("tasksCompleted", tasksCompleted);
    recordScalar("tasksDropped", tasksDropped);
}

} // namespace uavswarmta
