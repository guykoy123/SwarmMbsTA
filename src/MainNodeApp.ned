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
        double taskDuration @unit(s) = default(30s);  // constant work-time per task (was uniform(10s,60s))
        int taskLimit = default(100);
        string algorithmType = default("FIFO");   // "FIFO" or "COST"
        bool bumpDroppedPriority = default(true);

        // --- COST-algorithm tunables (only used when algorithmType == "COST")
        // The cost function lives in MainNodeApp::computeDroneCost / computeMbsCost;
        // tweak these multipliers to play with how the auction values energy vs.
        // disruption.  Defaults are intentionally round numbers so you can scale
        // them by 10x to see clear behaviour changes.
        double costEnergyWeight        = default(1.0);    // per meter (drone->task and mbs relocation)
        double costHaltPriorityWeight  = default(100.0);  // per priority "level" (priority 1=high, 3=low)
        double costHaltGroupSizeWeight = default(10.0);   // per drone currently on the preempted task
        double costMbsRelocationWeight = default(1.0);    // per meter the MBS would move

        @display("i=device/server,gold");
}
