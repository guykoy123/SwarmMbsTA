package uavswarmta;

@namespace(uavswarmta); // This tells NED to look inside your C++ namespace

import inet.applications.contract.IApp;

// This defines the application that runs on the Main Node
simple MainNodeApp like IApp
{
    parameters:
        // 'volatile' means it draws a new random number every time the C++ code reads it
        volatile double taskGenerationInterval @unit(s) = default(uniform(1s, 5s));
		int taskLimit = default(100); // Stop generating after this many tasks
        // The parameter to switch between your simulation types
        string algorithmType = default("FIFO"); // Can be "FIFO" or "AUCTION"
	
        int localPort = default(5000);  // Port for UDP traffic
        int destPort = default(5000);   // Port the drones listen on

        bool bumpDroppedPriority = default(true); // Choose if dropped tasks get a priority boost

        // A gold server icon
        @display("i=device/server,gold");
    gates:
        input socketIn;
        output socketOut;
}