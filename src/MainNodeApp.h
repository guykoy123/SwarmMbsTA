#ifndef __UAVSWARMTA_MAINNODEAPP_H_
#define __UAVSWARMTA_MAINNODEAPP_H_

#include <omnetpp.h>
#include <map>
#include <vector>
#include "inet/common/geometry/common/Coord.h"
#include "inet/transportlayer/contract/udp/UdpSocket.h"
#include "TaskMessages_m.h"

using namespace omnetpp;

namespace uavswarmta {

class MainNodeApp : public cSimpleModule, public inet::UdpSocket::ICallback {
  public:
    // This MUST be static so the cQueue can use it to sort
    static int compareTaskPriority(cObject *a, cObject *b);

  protected:
    cQueue taskQueue; // Use cQueue instead of cPacketQueue
    std::map<int, inet::Coord> activeTaskLocations;
    std::map<int, int> activeTaskDroneCount;
    double commRange; // Fixes the "undeclared identifier" error

    int taskCounter;
    int maxQueueSize;
    inet::UdpSocket socket;
    cMessage *generateTaskTimer;
    virtual void generateNewTask();

    virtual void initialize(int stage) override;
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void handleMessage(cMessage *msg) override;

    // Assignment logic
    virtual void tryAssignTask();
    virtual void assignTaskFifo(); // Matches the .cc file (no arguments)
    virtual std::vector<cModule*> findClosestIdleDrones(double targetX, double targetY, int reqDrones);
    virtual cModule* findSuitableMbs(double targetX, double targetY, double& outCentroidX, double& outCentroidY);
    virtual void dispatchUnits(TaskNotification* task, std::vector<cModule*>& drones, cModule* mbs, double centroidX, double centroidY);

    // UDP & Socket logic
    virtual void socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) override;
    virtual void socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) override {}
    virtual void socketClosed(inet::UdpSocket *socket) override {}
    virtual void handleTaskUpdate(int taskId, int droneId, bool isCompletion, double remainingDuration = 0.0);
    virtual void removeTaskFromMbs(int taskId);
};

} // namespace
#endif // __UAVSWARMTA_MAINNODEAPP_H_
