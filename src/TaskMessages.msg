
//the simulation has three types of messages for the 3 stages of communication
//the main node notifies the entire system of new task
//available drones send bids in rounds until they reach consensus
//main node assings the task to the winning drones


import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;

namespace uavswarmta;

// 1. MAIN NODE -> DRONES: Notifies the swarm that a new task exists
class TaskNotification extends inet::FieldsChunk {
    int taskId;               // Unique ID
    double targetX;           // X coordinate
    double targetY;           // Y coordinate
    int priority;             // Priority level (1, 2, or 3)
    int requiredDrones;       // How many drones are needed to complete it
    double duration;          // How long the task takes to complete once they arrive (in seconds)
}

// 2. DRONES -> MAIN NODE: A drone's auction bid for a specific task
class TaskBid extends inet::FieldsChunk {
    int taskId;               // The task they are bidding on
    int droneId;              // The ID of the drone making the bid
    double bidValue;          // The calculated bid (could be distance, remaining energy, etc.)
}

// 3. MAIN NODE -> DRONES: The final assignment of who won the auction
class TaskAssignment extends inet::FieldsChunk {
    int taskId;               // The task being assigned
    int assignedDroneIds[];   // Dynamic array containing the IDs of all winning drones
}

// DRONE -> MAIN NODE: Task is fully finished
class TaskCompletion extends inet::FieldsChunk {
    int taskId;
    int droneId;
}

// DRONE -> MAIN NODE & PEER DRONES: Task is dropped before completion
class TaskDropNotification extends inet::FieldsChunk {
    int taskId;
    int droppingDroneId;
    double remainingDuration; // So the Main Node knows how much time is left
}

