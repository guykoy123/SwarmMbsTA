/*
 * MbsApp.cc
 *
 *  Created on: Mar 7, 2026
 *      Author: Guy Koyfman & Omer
 */

#include "MbsApp.h"
#include "inet/common/Protocol.h"
#include "inet/common/ProtocolTag_m.h"

// השורה הזו הייתה חסרה והיא קריטית כדי ש-OMNeT++ יזהה את המחלקה!
Define_Module(uavswarmta::MbsApp);

namespace uavswarmta {

void MbsApp::initialize(int stage) {
    // שלב 0 - הגדרות בסיסיות של התחנה
    if (stage == inet::INITSTAGE_LOCAL) {
        mbsId = getParentModule()->getIndex();
        mobility = check_and_cast<inet::IMobility *>(getParentModule()->getSubmodule("mobility"));
    }
    // שלב 3 - הרשת באוויר, אפשר לפתוח פורטים (UDP)
    else if (stage == inet::INITSTAGE_APPLICATION_LAYER) {
        socket.setOutputGate(gate("socketOut"));

        // socket.setProtocol(&inet::Protocol::udp); // השורה נשארת בהערה כפי שביקש הקומפיילר

        socket.bind(par("localPort"));
        socket.setCallback(this);
    }
}

void MbsApp::handleMessage(cMessage *msg) {
    // 40 is the internal message kind for incoming UDP data
    if (msg->getKind() == 40 || msg->getKind() == inet::UDP_I_DATA) {
        socket.processMessage(msg);
    } else {
        delete msg;
    }
}

void MbsApp::assignInitialTask(int taskId, double targetX, double targetY) {
    assignedTasks.push_back(taskId);
    shiftPosition(targetX, targetY);
}

void MbsApp::addCoveredTask(int taskId) {
    assignedTasks.push_back(taskId);
}

void MbsApp::removeTask(int taskId) {
    auto it = std::find(assignedTasks.begin(), assignedTasks.end(), taskId);
    if (it != assignedTasks.end()) {
        assignedTasks.erase(it);
    }

    if (assignedTasks.empty()) {
        EV << "MBS " << mbsId << " has no more covered tasks. Now IDLE.\n";
    }
}

void MbsApp::shiftPosition(double newX, double newY) {
    targetPos = inet::Coord(newX, newY, 0);

    EV << "MBS " << mbsId << " shifting to (" << newX << ", " << newY
       << ") to cover " << assignedTasks.size() << " active tasks.\n";
}

void MbsApp::socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) {
    delete packet; // MBS doesn't process data yet
}

void MbsApp::socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) {
    delete indication;
}

void MbsApp::socketClosed(inet::UdpSocket *socket) {
    // Handle closure if needed
}

} // namespace
