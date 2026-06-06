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
    return taskB->getPriority() - taskA->getPriority();
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
        if (algo == "AUCTION") {
            taskQueue.setup(compareTaskPriority);
        }

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
    newTask->setDuration(uniform(10, 60));

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
    } else if (algo == "AUCTION") {
        // assignTaskAuction();  // TODO
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
    if (!isCompletion) {
        EV << "WARNING: Task #" << taskId << " was dropped by Drone " << droneId << "!\n";
        activeTaskHadDrop[taskId] = true;
        auto rit = taskRecords.find(taskId);
        if (rit != taskRecords.end()) rit->second.dropEvents++;
        // TODO: re-queue with remainingDuration if desired
    }

    if (activeTaskDroneCount.find(taskId) != activeTaskDroneCount.end()) {
        activeTaskDroneCount[taskId]--;

        if (activeTaskDroneCount[taskId] <= 0) {
            EV << "All assigned drones have reported back for Task #" << taskId
               << ". Releasing MBS coverage.\n";

            std::string outcome;
            if (activeTaskHadDrop[taskId]) { tasksDropped++;  outcome = "DROPPED"; }
            else                           { tasksCompleted++; outcome = "COMPLETED"; }

            // Emit CSV row (and forget the record).
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
    }
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
