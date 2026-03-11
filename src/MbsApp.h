/*
 * MbsApp.h
 *
 *  Created on: Mar 7, 2026
 *      Author: Guy Koyfman
 */
#ifndef __UAVSWARMTA_MBSAPP_H_
#define __UAVSWARMTA_MBSAPP_H_

#include <omnetpp.h>
#include <vector>
#include <algorithm>
#include "inet/transportlayer/contract/udp/UdpSocket.h" // Required for UdpSocket
#include "inet/common/packet/Packet.h"               // Required for Packet
#include "inet/mobility/contract/IMobility.h"

using namespace omnetpp;

namespace uavswarmta {

class MbsApp : public cSimpleModule, public inet::UdpSocket::ICallback {
  public:
    bool isIdle() const { return assignedTasks.empty(); }
    void assignInitialTask(int taskId, double targetX, double targetY);
    void addCoveredTask(int taskId);
    void removeTask(int taskId);
    void shiftPosition(double newX, double newY);

    inet::Coord getTargetPosition() const { return targetPos; }
    std::vector<int> getAssignedTasks() const { return assignedTasks; }

  protected:
    inet::UdpSocket socket;
    inet::IMobility *mobility;

    int mbsId;
    std::vector<int> assignedTasks;
    inet::Coord targetPos;

    virtual void initialize(int stage) override;
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void handleMessage(cMessage *msg) override;

    // Required Callback Implementations
    virtual void socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) override;
    virtual void socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) override;
    virtual void socketClosed(inet::UdpSocket *socket) override;
};

} // namespace
#endif
