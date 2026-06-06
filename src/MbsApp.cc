/*
 * MbsApp.cc
 */
#include "MbsApp.h"

namespace uavswarmta {

Define_Module(MbsApp);

void MbsApp::initialize(int stage) {
    if (stage == 0) {
        mbsId = getParentModule()->getIndex();
        mobility = check_and_cast<DroneMobility *>(
            getParentModule()->getSubmodule("mobility"));
        commRange = par("mbs_comm_range").doubleValue();
        targetPos = mobility->getCurrentPosition();

        // Draw the MBS coverage as a circle around the antenna icon.
        // cDisplayString& ds = getParentModule()->getDisplayString();
        // ds.setTagArg("r", 0, (long)commRange);
        // ds.setTagArg("r", 1, "");          // no fill
        // ds.setTagArg("r", 2, "blue");
    }
}

void MbsApp::handleMessage(cMessage *msg) {
    // No network input; just discard any unexpected messages.
    if (msg->isSelfMessage()) return;
    delete msg;
}

void MbsApp::assignInitialTask(int taskId, double targetX, double targetY) {
    Enter_Method("assignInitialTask");
    assignedTasks.push_back(taskId);
    shiftPosition(targetX, targetY);
}

void MbsApp::addCoveredTask(int taskId) {
    Enter_Method("addCoveredTask");
    assignedTasks.push_back(taskId);
}

void MbsApp::removeTask(int taskId) {
    Enter_Method("removeTask");
    auto it = std::find(assignedTasks.begin(), assignedTasks.end(), taskId);
    if (it != assignedTasks.end()) {
        assignedTasks.erase(it);
    }
    if (assignedTasks.empty()) {
        EV << "MBS " << mbsId << " has no more covered tasks. Now IDLE.\n";
    }
}

void MbsApp::shiftPosition(double newX, double newY) {
    Enter_Method("shiftPosition");
    targetPos = inet::Coord(newX, newY, mobility->getCurrentPosition().z);

    EV << "MBS " << mbsId << " shifting to (" << newX << ", " << newY
       << ") to cover " << assignedTasks.size() << " active tasks.\n";

    // Actually command the mobility to drive there.
    mobility->setTarget(targetPos, par("speed").doubleValue());
}

} // namespace uavswarmta
