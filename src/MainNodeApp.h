/*
 * MainNodeApp.h
 */
#ifndef __UAVSWARMTA_MAINNODEAPP_H_
#define __UAVSWARMTA_MAINNODEAPP_H_

#include <omnetpp.h>
#include <fstream>
#include <map>
#include <vector>
#include "inet/common/geometry/common/Coord.h"
#include "TaskMessages_m.h"

using namespace omnetpp;

namespace uavswarmta {

class MainNodeApp : public cSimpleModule {
  public:
    // Static comparator used by cQueue to sort TaskNotifications by priority.
    static int compareTaskPriority(cObject *a, cObject *b);

    // ---- Direct-call API replacing the old UDP messages from drones. ----
    // Called by a DroneApp when it finishes its share of a task.
    virtual void onTaskCompletion(int taskId, int droneId);
    // Called by a DroneApp when it has to give up on a task mid-flight.
    virtual void onTaskDropNotification(int taskId, int droneId, double remainingDuration);

    virtual ~MainNodeApp();

  protected:
    cQueue taskQueue;
    std::map<int, inet::Coord> activeTaskLocations;
    std::map<int, int> activeTaskDroneCount;
    std::map<int, bool> activeTaskHadDrop;  // true if any assigned drone dropped
    std::map<int, cFigure*> taskFigures;   // canvas markers, keyed by taskId
    double commRange;

    // Per-task bookkeeping for CSV output (one row per finalized task).
    struct TaskRecord {
        int taskId;
        int priority;
        int requiredDrones;
        double targetX;
        double targetY;
        double duration;          // nominal work time
        simtime_t generatedAt;
        simtime_t dispatchedAt;   // -1 until dispatch
        int mbsId;                // -1 until dispatch
        int dronesAssigned;       // 0 until dispatch
        int dropEvents;           // # of per-drone drop reports
    };
    std::map<int, TaskRecord> taskRecords;
    std::ofstream csvOut;

    int taskCounter;
    int maxQueueSize;
    double droneCommRange = 0;     // cached from drone[0].app[0].drone_comm_range

    // Cached COST tunables (NED params, see MainNodeApp.ned).
    double costEnergyWeight = 1.0;
    double costHaltPriorityWeight = 100.0;
    double costHaltGroupSizeWeight = 10.0;
    double costMbsRelocationWeight = 1.0;
    // Per-priority halt-weight table. Empty => use the linear scalar
    // fallback (costHaltPriorityWeight * (priorityLevels + 1 - p)).
    // When non-empty, size() == priorityLevels and the entry at
    // index (p - 1) is the absolute halt weight for priority p.
    std::vector<double> costHaltPriorityTable;

    // Task-priority distribution (parsed from NED params at init).
    // priorityCdf is empty => fall back to intuniform(1, priorityLevels).
    // Otherwise priorityCdf[k] holds the cumulative probability of drawing
    // priority <= (k+1); the last entry is always 1.0.
    int priorityLevels = 3;
    std::vector<double> priorityCdf;

    // Per-priority queue-wait deadlines (in seconds). Empty => no expiry.
    // Entry (p-1) is the maximum time a priority-p task may sit in the
    // queue before being dropped with outcome "EXPIRED". Cancelled the
    // moment the task is dispatched.
    std::vector<double> taskDeadlineTable;
    std::map<int, cMessage*> taskDeadlineTimers;

    // ---- stats for the HUD ----
    int tasksCompleted = 0;
    int tasksDropped = 0;
    cTextFigure *statsPanel = nullptr;

    cMessage *generateTaskTimer;

    virtual void initialize(int stage) override;
    virtual int numInitStages() const override { return 4; }
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;

    // If the NED param ask_user is true, pops a single GUI dialog at sim
    // start listing every tunable parameter as "name=value; name=value; ...".
    // The user edits inline; values are parsed back into the matching cPars.
    virtual void promptUserForParameters();

    virtual void generateNewTask();
    virtual void tryAssignTask();
    virtual void assignTaskFifo();
    virtual void assignTaskCost();
    // Draw a task priority in [1..priorityLevels] using the cached CDF
    // (or intuniform() when priorityWeights is empty).
    virtual int drawTaskPriority();
    // Called when a queued task's wait-deadline timer fires. Removes the
    // task from the queue and finalizes it as EXPIRED. No-op if the task
    // has already been dispatched (timer should have been cancelled).
    virtual void onTaskDeadlineExpired(int taskId);
    // Cancel + delete a pending deadline timer (called at dispatch and as
    // a defensive cleanup in finalizeTask). Safe to call when no timer
    // exists for the given taskId.
    virtual void cancelTaskDeadline(int taskId);
    virtual std::vector<cModule*> findClosestIdleDrones(double targetX, double targetY, int reqDrones);
    // FIFO_PREEMPT helper: take closest idle drones first, then top up with the
    // closest drones currently on a STRICTLY lower-priority task. Returns fewer
    // than reqDrones if even the preemptible pool is too small.
    virtual std::vector<cModule*> findClosestDronesWithPreempt(double targetX, double targetY, int reqDrones, int newPriority);
    virtual cModule* findSuitableMbs(double targetX, double targetY, double& outCentroidX, double& outCentroidY);
    virtual void dispatchUnits(TaskNotification* task, std::vector<cModule*>& drones, cModule* mbs, double centroidX, double centroidY);

    // ---- COST-algorithm helpers (drone auction + MBS selection) -----------
    // Keep computeDroneCost / computeMbsCost short and readable -- this is the
    // single place to play with the cost-function math. Inputs are pre-resolved
    // so the math itself is the only thing on screen.
    virtual double computeDroneCost(cModule* droneMod, double targetX, double targetY, int newTaskPriority);
    virtual double computeHaltPenalty(int currentPriority, int currentGroupSize);
    virtual std::vector<cModule*> findCostMinimalDrones(double targetX, double targetY, int reqDrones, int newPriority);
    virtual cModule* findCostMinimalMbs(double targetX, double targetY, int newPriority, double& outCentroidX, double& outCentroidY);
    virtual void preemptAndDispatchUnits(TaskNotification* task, std::vector<cModule*>& drones, cModule* mbs, double centroidX, double centroidY);

    virtual void handleTaskUpdate(int taskId, int droneId, bool isCompletion, double remainingDuration = 0.0);
    virtual void removeTaskFromMbs(int taskId);

    // ---- "Some drones leave, others pick up the slack" helpers ------------
    // Called whenever a drone exits a shared task. The survivors stay on the
    // task; their remaining work is multiplied by oldN/newN so the team still
    // finishes, just more slowly. If newN reaches 0 the caller decides
    // whether to finalize the task (failure path) or re-queue it (preemption).
    virtual void scaleRemainingDurationForTask(int taskId, int leavingDroneId, double scale);
    virtual void finalizeTask(int taskId, const std::string& outcome);

    // Preemption-specific: bookkeeping helper used by the COST allocator.
    // If other drones remain on the old task they pick up the slack; if the
    // preempted drone was the LAST one, the task is marked DROPPED (the
    // allocator's choice to sacrifice it shows up in the stats).
    virtual void handlePreemption(int oldTaskId, int leavingDroneId, int newTaskId);

    // Canvas markers so the user can see where each pending/active task is.
    virtual void addTaskFigure(int taskId, double x, double y);
    virtual void markTaskActive(int taskId);     // turn marker from queued -> active
    virtual void removeTaskFigure(int taskId);

    // HUD with live counters (top-left of the canvas).
    virtual void createStatsPanel();
    virtual void refreshStatsPanel();

    // CSV per-task logging
    virtual void openCsv();
    virtual void writeCsvRow(const TaskRecord& r, const std::string& outcome);
};

} // namespace uavswarmta

#endif
