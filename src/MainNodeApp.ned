package uavswarmta;

@namespace(uavswarmta);

//
// Task generator and central allocator. No network gates: drones report task
// completion / drop via direct C++ method calls (see MainNodeApp::onTask...).
//
simple MainNodeApp like IMobileApp
{
    parameters:
        volatile double taskGenerationInterval @unit(s) = default(uniform(1s, 5s));
        int taskLimit = default(100);
        string algorithmType = default("FIFO");   // "FIFO" or "AUCTION"
        bool bumpDroppedPriority = default(true);

        @display("i=device/server,gold");
}
