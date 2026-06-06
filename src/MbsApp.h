/*
 * MbsApp.h
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
    bool isIdle() const { return assignedTasks.empty(); }
    void assignInitialTask(int taskId, double targetX, double targetY);
    void addCoveredTask(int taskId);
    void removeTask(int taskId);
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
