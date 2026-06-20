/*
 * MbsApp.cc
 *
 * Mobile Base Station behaviour. The MBS owns a list of covered tasks and
 * a target position; the actual flying is delegated to DroneMobility (the
 * same class drones use). All state changes come from MainNodeApp via
 * direct calls -- there are no inbound network messages.
 */
#include "MbsApp.h"

namespace uavswarmta {

Define_Module(MbsApp);

// Cache NED params and snapshot the starting position as the initial target
// (so an MBS that never gets assigned a task stays put).
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

// MBS has no inbound network messages; only its own (unused) self-messages
// might reach this method. Defensive cleanup, never expected to fire.
void MbsApp::handleMessage(cMessage *msg) {
    if (msg->isSelfMessage()) return;
    delete msg;
}

// First task picked up by an idle MBS: record it and fly straight to the
// task site (the only covered task, so no centroid math needed).
void MbsApp::assignInitialTask(int taskId, double targetX, double targetY) {
    Enter_Method("assignInitialTask");
    assignedTasks.push_back(taskId);
    shiftPosition(targetX, targetY);
}

// Mark a task as covered without moving. MainNodeApp's MBS picker has
// already verified that the current position keeps every drone in range;
// a recentre via shiftPosition() is a separate decision the caller makes.
void MbsApp::addCoveredTask(int taskId) {
    Enter_Method("addCoveredTask");
    assignedTasks.push_back(taskId);
}

// Forget a covered task (because it COMPLETED, was DROPPED, or this MBS
// was unassigned during a RELOCATE-AND-DROP). Logs IDLE on the last drop.
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

// Command the mobility to drive to (newX, newY) at our configured speed.
// MainNodeApp passes the centroid of all currently-covered tasks so the
// MBS stays equidistant from every drone it's serving.
void MbsApp::shiftPosition(double newX, double newY) {
    Enter_Method("shiftPosition");
    targetPos = inet::Coord(newX, newY, mobility->getCurrentPosition().z);

    EV << "MBS " << mbsId << " shifting to (" << newX << ", " << newY
       << ") to cover " << assignedTasks.size() << " active tasks.\n";

    // Actually command the mobility to drive there.
    mobility->setTarget(targetPos, par("speed").doubleValue());
}

} // namespace uavswarmta
