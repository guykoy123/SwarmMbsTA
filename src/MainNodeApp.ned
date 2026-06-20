package uavswarmta;

@namespace(uavswarmta);

//
// Task generator and central allocator. No network gates: drones report task
// completion / drop via direct C++ method calls (see MainNodeApp::onTask...).
//
simple MainNodeApp like IMobileApp
{
    parameters:
        volatile double taskGenerationInterval @mutable @unit(s) = default(uniform(1s, 5s));
        double taskDuration @unit(s) = default(30s);  // constant work-time per task (was uniform(10s,60s))
        int taskLimit = default(100);
        string algorithmType @mutable = default("FIFO");   // "FIFO", "FIFO_PRIO", "FIFO_PREEMPT", or "COST"
        bool bumpDroppedPriority = default(true);
        bool ask_user = default(false);   // if true, popup-prompt for every tunable at sim start

        // --- Task priority distribution ---------------------------------
        // priorityLevels = number of priority classes (1 = HIGH urgency, N = LOW).
        // priorityWeights = space- or comma-separated weights, one per class
        // in priority order (leftmost = priority 1). Empty string => uniform.
        // The weights are normalised internally, so absolute magnitudes don't
        // matter -- only the ratios do.
        //   ""           => uniform (intuniform(1, priorityLevels))
        //   "5 2 1"      => mostly high-pri  (pri1:pri2:pri3 = 5:2:1)
        //   "1 2 5"      => mostly low-pri
        //   "1 3 1"      => peaked on middle priority
        int priorityLevels = default(3);
        string priorityWeights @mutable = default("");

        // --- Per-priority wait deadlines (queue-only) -------------------
        // Tasks that sit in the queue longer than their deadline are
        // dropped (CSV outcome = EXPIRED). Once a task is dispatched the
        // deadline no longer applies -- this only enforces SLAs on queue
        // wait time, not on in-flight work.
        //
        // Empty string => no deadlines (current behaviour, tasks wait
        // forever). Otherwise space- or comma-separated **seconds**, one
        // per priority class (leftmost = priority 1 = HIGH).
        //
        // Typical pattern (high-pri stale faster, low-pri patient):
        //   ""               => no expiry
        //   "10 30 120"      => HIGH 10s, MID 30s, LOW 120s
        //   "5 20 60 180 600" => 5-level SLA ladder
        string taskDeadlineWeights @mutable = default("");

        // Per-task CSV output path. May contain the placeholders
        //   {configname}  {runnumber}  {algorithm}
        // which are expanded at sim start (NOTE: braces only, no '$' --
        // OMNeT++'s ini parser would otherwise treat them as iteration
        // variables). Set to empty string to disable CSV logging.
        // Parent dirs are created if missing.
        string csvOutputPath = default("results/{configname}-run{runnumber}-{algorithm}-tasks.csv");

        // --- COST-algorithm tunables (only used when algorithmType == "COST")
        // The cost function lives in MainNodeApp::computeDroneCost / computeMbsCost;
        // tweak these multipliers to play with how the auction values energy vs.
        // disruption.  Defaults are intentionally round numbers so you can scale
        // them by 10x to see clear behaviour changes.
        double costEnergyWeight        = default(1.0);    // per meter (drone->task and mbs relocation)
        // Halt penalty for preempting a running task. Two ways to configure:
        //   (a) Leave costHaltPriorityWeights = "" and use the scalar
        //       costHaltPriorityWeight -- the halt penalty for class p is
        //          costHaltPriorityWeight * (priorityLevels + 1 - p)
        //       so priority 1 (HIGH) is most expensive and class
        //       priorityLevels (LOW) is cheapest, with a linear ramp.
        //       This matches the original (4 - p) formula when
        //       priorityLevels = 3, but stays sane for any N.
        //   (b) Set costHaltPriorityWeights to a space/comma-separated list
        //       of EXACTLY priorityLevels positive weights, leftmost =
        //       priority 1. The scalar costHaltPriorityWeight is ignored
        //       and the per-class table is used verbatim. Use this when
        //       you want non-linear scaling (e.g. "1000 100 10 1 0" to
        //       make the highest classes drastically more protected than
        //       a linear ramp would).
        double costHaltPriorityWeight  = default(100.0);  // scalar fallback (see above)
        string costHaltPriorityWeights @mutable = default("");     // "" => use scalar with linear ramp
        double costHaltGroupSizeWeight = default(10.0);   // per drone currently on the preempted task
        double costMbsRelocationWeight = default(1.0);    // per meter the MBS would move

        @display("i=device/server,gold");
}
