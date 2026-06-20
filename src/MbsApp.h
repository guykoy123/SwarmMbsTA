/*
 * MbsApp.h
 *
 * Mobile Base Station application. Holds one or more covered tasks, sits
 * (or moves) at the centroid of those tasks, and provides link-budget
 * coverage for the drones executing them. Purely reactive -- every state
 * change (assign / shift / drop) is initiated by MainNodeApp; this class
 * only stores the list of covered tasks and commands its mobility.
 */
#ifndef __UAVSWARMTA_MBSAPP_H_
#define __UAVSWARMTA_MBSAPP_H_

#include <omnetpp.h>
#include <vector>
#include <algorithm>
#include "DroneMobility.h"

using namespace omnetpp;

namespace uavswarmta {

class MbsApp : public cSimpleModule {
  public:
    // True iff this MBS has no covered tasks (and therefore no drones
    // depending on its position).
    bool isIdle() const { return assignedTasks.empty(); }
    // First task for an idle MBS: record it AND move to the task site.
    void assignInitialTask(int taskId, double targetX, double targetY);
    // Add another covered task without moving (caller has already verified
    // the existing position still covers the new site, or will call
    // shiftPosition() afterwards if a recentre is needed).
    void addCoveredTask(int taskId);
    // Drop a covered task from the list. Becomes IDLE iff the list empties.
    void removeTask(int taskId);
    // Command the mobility to drive to (newX, newY); typically the centroid
    // of all currently-covered tasks.
    void shiftPosition(double newX, double newY);

    inet::Coord getTargetPosition() const { return targetPos; }
    std::vector<int> getAssignedTasks() const { return assignedTasks; }
    double getCommRange() const { return commRange; }

  protected:
    DroneMobility *mobility;

    int mbsId;
    double commRange;
    std::vector<int> assignedTasks;
    inet::Coord targetPos;

    virtual void initialize(int stage) override;
    virtual int numInitStages() const override { return 4; }
    virtual void handleMessage(cMessage *msg) override;
};

} // namespace uavswarmta

#endif
