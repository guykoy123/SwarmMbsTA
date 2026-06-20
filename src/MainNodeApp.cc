/*
 * MainNodeApp.cc
 *
 * Central task allocator + scheduler for the UAV swarm. One instance lives
 * inside the singleton `mainNode` host (see SwarmNetwork.ned). It is
 * deliberately the only place that decides which drones/MBSs serve which
 * task; the drone/MBS apps just execute orders and report back via the
 * direct-call API (onTaskCompletion / onTaskDropNotification).
 *
 * Responsibilities (one section per banner below):
 *   - Queue ordering           (compareTaskPriority + cQueue setup)
 *   - Optional GUI/CLI prompt  (promptUserForParameters)
 *   - Init / NED parsing       (initialize -- caches tunables, builds the
 *                              priority CDF, halt-weight table and per-
 *                              priority queue-wait deadline table)
 *   - Self-message router      (handleMessage -- generator + deadlines)
 *   - Task generation          (generateNewTask + drawTaskPriority,
 *                              schedules an optional EXPIRED-deadline timer)
 *   - Drone/MBS update path    (onTaskCompletion / onTaskDropNotification
 *                              -> handleTaskUpdate -> finalizeTask)
 *   - Allocators               (assignTaskFifo for FIFO/FIFO_PRIO/
 *                              FIFO_PREEMPT, assignTaskCost for COST)
 *   - Canvas markers + HUD     (addTaskFigure / refreshStatsPanel ...)
 *   - Per-task CSV log         (openCsv + writeCsvRow, one row per
 *                              finalized task; outcome enum documented
 *                              above writeCsvRow)
 *
 * Priority convention everywhere: integers 1..priorityLevels where
 *   1 = HIGH urgency (front of queue, shortest deadline)
 *   priorityLevels = LOW urgency (back of queue, longest deadline).
 */
#include "MainNodeApp.h"
#include <limits>
#include <iomanip>
#include <sstream>
#include "inet/mobility/contract/IMobility.h"

#include "DroneApp.h"
#include "MbsApp.h"
#include "ParamDialog.h"

namespace uavswarmta {

Define_Module(MainNodeApp);

MainNodeApp::~MainNodeApp() {
    // Deletion order matters: cancel timers BEFORE touching the queue
    // (deadline-timer cancellation does not depend on queue state, but the
    // reverse would briefly leave timers pointing at freed task ids).
    cancelAndDelete(generateTaskTimer);
    // Cancel + delete any pending per-task deadline timers.
    for (auto& kv : taskDeadlineTimers) {
        cancelAndDelete(kv.second);
    }
    taskDeadlineTimers.clear();
    taskQueue.clear();
    if (csvOut.is_open()) csvOut.close();

    // Detach any leftover task markers so OMNeT++ can free them cleanly.
    if (getParentModule() && getParentModule()->getParentModule()) {
        cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();
        for (auto& kv : taskFigures) {
            canvas->removeFigure(kv.second);
            delete kv.second;
        }
    }
    taskFigures.clear();
}

// -----------------------------------------------------------------------
// QUEUE SORTING LOGIC
// -----------------------------------------------------------------------
int MainNodeApp::compareTaskPriority(cObject *a, cObject *b) {
    TaskNotification *taskA = check_and_cast<TaskNotification*>(a);
    TaskNotification *taskB = check_and_cast<TaskNotification*>(b);
    // Priority convention: 1 = HIGH urgency, 3 = LOW. Return negative when A
    // should come first, so subtract A from B's number... i.e. a-b gives us
    // ascending priority numbers (high-urgency at the front of the queue).
    return taskA->getPriority() - taskB->getPriority();
}

// -----------------------------------------------------------------------
// OPTIONAL prompt for every tunable param
// -----------------------------------------------------------------------
// When ask_user=true in the ini, we pop a dialog at sim start showing every
// tunable in a form (one row per param). Under Qtenv we use a real Qt
// QDialog (see ParamDialog.{h,cc}); under Cmdenv we fall back to a single
// semicolon-separated text prompt via getEnvir()->gets() so the feature
// still works headless. Cross-module params (drone_comm_range,
// commTimeoutDuration, mbs_comm_range) are shown once and broadcast to
// every drone[i] / mbs[i] submodule. Safe because mainNode is declared
// before drone[]/mbs[] in SwarmNetwork.ned, so MainNodeApp's stage-0
// initialize runs before any drone/mbs reads those params.
void MainNodeApp::promptUserForParameters() {
    int numDrones = getParentModule()->getParentModule()->par("numDrones").intValue();
    int numMbs    = getParentModule()->getParentModule()->par("numMbs").intValue();

    struct Row {
        std::string label;
        std::string hint;             // optional: unit / short description
        cPar* primary;
        std::vector<cPar*> broadcastTo;
        std::vector<std::string> choices;   // empty -> free-text field
        bool editable = false;              // applies only when choices set
    };
    std::vector<Row> rows;

    auto unitHint = [](cPar& p) -> std::string {
        const char* u = p.getUnit();
        return u ? std::string("unit: ") + u : std::string();
    };
    // addLocal     -> free-text row for a param owned by this module.
    // addLocalChoice -> same, but the GUI shows a dropdown (editable=true
    //                   keeps the field typeable so users can supply
    //                   custom values not in the preset list).
    // addBroadcast -> single row whose value is fanned out to the named
    //                 param on every drone[i].app[0] or mbs[i].app[0]
    //                 submodule -- so the user sets e.g. drone_comm_range
    //                 once and every drone picks it up.
    auto addLocal = [&](const char* name) {
        cPar& p = par(name);
        rows.push_back({name, unitHint(p), &p, {}, {}, false});
    };
    auto addLocalChoice = [&](const char* name,
                              std::vector<std::string> opts,
                              bool editable) {
        cPar& p = par(name);
        rows.push_back({name, unitHint(p), &p, {}, std::move(opts), editable});
    };
    auto addBroadcast = [&](const char* label, const char* parentPattern,
                            int count, const char* paramName) {
        if (count <= 0) return;
        Row r{label, "", nullptr, {}, {}, false};
        for (int i = 0; i < count; i++) {
            std::string path = std::string(parentPattern) + "[" + std::to_string(i) + "].app[0]";
            cModule* m = getModuleByPath(path.c_str());
            if (!m) continue;
            if (!r.primary) {
                r.primary = &m->par(paramName);
                r.hint = unitHint(*r.primary);
            } else {
                r.broadcastTo.push_back(&m->par(paramName));
            }
        }
        if (r.primary) rows.push_back(std::move(r));
    };

    // algorithmType: strict dropdown -- four allocator variants are implemented.
    //   FIFO          : insertion-order queue, idle-only dispatch, no preemption.
    //   FIFO_PRIO     : priority-ordered queue (higher-priority tasks jump
    //                   ahead), idle-only dispatch, no preemption.
    //   FIFO_PREEMPT  : priority-ordered queue + may preempt drones currently
    //                   running a STRICTLY lower-priority task when there
    //                   aren't enough idle drones (MBS preemption stays a
    //                   COST-only feature). Dispatch picks closest drones.
    //   COST          : full auction, see assignTaskCost.
    addLocalChoice("algorithmType",
                   {"FIFO", "FIFO_PRIO", "FIFO_PREEMPT", "COST"},
                   /*editable=*/false);
    addLocal("taskLimit");
    // taskGenerationInterval: editable dropdown with common arrival-pattern
    // presets. UNIFORM = flat between bounds; NORMAL = Gaussian around a mean
    // (a.k.a. "gaussian"); EXPONENTIAL = Poisson process, naturally produces
    // bursts (short inter-arrival clusters); CONSTANT = back-to-back at a
    // fixed cadence (the most extreme burst). Users can also type any
    // custom OMNeT++ volatile expression here.
    addLocalChoice("taskGenerationInterval", {
        "uniform(10s, 30s)",        // uniform
        "normal(20s, 5s)",          // gaussian
        "exponential(15s)",         // Poisson / bursty
        "2s"                        // constant cadence (max burst)
    }, /*editable=*/true);
    addLocal("taskDuration");
    addLocal("bumpDroppedPriority");
    addLocal("priorityLevels");
    // priorityWeights: editable dropdown of common priority-distribution
    // presets. Empty string = uniform; otherwise one weight per class
    // (leftmost = priority 1 = HIGH). Weights are normalised internally.
    addLocalChoice("priorityWeights", {
        "",                          // uniform
        "5 2 1",                     // mostly high-pri
        "1 2 5",                     // mostly low-pri
        "1 3 1"                      // peaked on middle priority
    }, /*editable=*/true);
    // taskDeadlineWeights: per-class queue-wait deadlines in seconds.
    // Empty string = no expiry; otherwise priorityLevels positive values
    // (leftmost = priority 1 = HIGH, conventionally shortest).
    addLocalChoice("taskDeadlineWeights", {
        "",                          // no deadlines
        "10 30 120",                 // HIGH stale fast, LOW patient
        "30 60 180",                 // looser SLA ladder
        "5 20 60 180 600"            // 5-level example
    }, /*editable=*/true);
    addLocal("costEnergyWeight");
    addLocal("costHaltPriorityWeight");
    // costHaltPriorityWeights: editable dropdown of per-class halt-weight
    // presets. Empty string = use the scalar costHaltPriorityWeight with a
    // linear ramp; otherwise one positive weight per priority class
    // (leftmost = priority 1 = HIGH).
    addLocalChoice("costHaltPriorityWeights", {
        "",                          // linear ramp from the scalar weight
        "300 200 100",               // explicit linear, equiv to scalar=100
        "1000 100 10",               // steep: HIGH ~100x more protected than LOW
        "1000 500 100 50 10"         // example 5-level table
    }, /*editable=*/true);
    addLocal("costHaltGroupSizeWeight");
    addLocal("costMbsRelocationWeight");
    addBroadcast("drone_comm_range",    "^.^.drone", numDrones, "drone_comm_range");
    addBroadcast("commTimeoutDuration", "^.^.drone", numDrones, "commTimeoutDuration");
    addBroadcast("mbs_comm_range",      "^.^.mbs",   numMbs,    "mbs_comm_range");
    addBroadcast("drone_speed",         "^.^.drone", numDrones, "speed");
    addBroadcast("mbs_speed",           "^.^.mbs",   numMbs,    "speed");

    // Helper: write parsed value into primary + every broadcast target.
    // For STRING-typed params we accept unquoted user input (e.g. "FIFO"
    // from a dropdown) and re-add the quotes that cPar::parse() expects.
    //   displayValue  -- strip the surrounding quotes for showing in the UI
    //   toParseValue  -- inverse: re-quote (and escape) before parse()
    //   applyValue    -- skip the parse() entirely if the user didn't
    //                    change anything (cheap and avoids triggering the
    //                    @mutable check for params that don't need it)
    auto displayValue = [](cPar& p) -> std::string {
        std::string s = p.str();
        if (p.getType() == cPar::STRING && s.size() >= 2 &&
            s.front() == '"' && s.back() == '"') {
            return s.substr(1, s.size() - 2);
        }
        return s;
    };
    auto toParseValue = [](cPar& p, const std::string& v) -> std::string {
        if (p.getType() == cPar::STRING) {
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return v;
            // Escape any embedded quotes/backslashes the user might type.
            std::string esc;
            esc.reserve(v.size() + 2);
            esc.push_back('"');
            for (char c : v) {
                if (c == '\\' || c == '"') esc.push_back('\\');
                esc.push_back(c);
            }
            esc.push_back('"');
            return esc;
        }
        return v;
    };

    auto applyValue = [&](const Row& r, const std::string& vRaw, std::string& warnOut) {
        std::string current = displayValue(*r.primary);
        if (vRaw == current) return true;       // unchanged -> skip
        std::string v = toParseValue(*r.primary, vRaw);
        try {
            r.primary->parse(v.c_str());
            for (cPar* extra : r.broadcastTo) extra->parse(v.c_str());
            return true;
        } catch (std::exception& e) {
            warnOut = std::string("could not parse '") + vRaw + "' for " +
                      r.label + " (" + e.what() + ") -- keeping default";
            return false;
        }
    };

    // ---- Path A: Qt GUI form (Qtenv only) ----
    if (isQtGuiAvailable()) {
        std::vector<ParamFieldSpec> fields;
        fields.reserve(rows.size());
        for (auto& r : rows) {
            ParamFieldSpec spec;
            spec.label    = r.label;
            spec.value    = displayValue(*r.primary);
            spec.hint     = r.hint;
            spec.choices  = r.choices;
            spec.editable = r.editable;
            fields.push_back(std::move(spec));
        }

        bool accepted = showParamDialog(
            fields, "UAV Swarm TA -- simulation parameters");
        if (!accepted) {
            EV << "ask_user: dialog cancelled; keeping all ini defaults.\n";
            return;
        }

        for (size_t i = 0; i < rows.size(); ++i) {
            std::string warn;
            if (applyValue(rows[i], fields[i].value, warn)) {
                EV << "ask_user: " << rows[i].label << " = "
                   << rows[i].primary->str()
                   << (rows[i].broadcastTo.empty() ? "" : "  (broadcast)") << "\n";
            } else {
                EV_WARN << "ask_user: " << warn << "\n";
            }
        }
        return;
    }

    // ---- Path B: text prompt fallback (Cmdenv / no GUI) ----
    std::string defaultLine;
    for (size_t i = 0; i < rows.size(); i++) {
        if (i) defaultLine += "; ";
        defaultLine += rows[i].label + "=" + displayValue(*rows[i].primary);
    }
    std::string answer = getEnvir()->gets(
        "Edit tunable parameters (semicolon-separated name=value pairs):",
        defaultLine.c_str());
    if (answer.empty() || answer == defaultLine) {
        EV << "ask_user: keeping all ini defaults.\n";
        return;
    }

    std::map<std::string, std::string> kv;
    size_t pos = 0;
    while (pos < answer.size()) {
        size_t semi = answer.find(';', pos);
        std::string token = answer.substr(pos, semi - pos);
        pos = (semi == std::string::npos) ? answer.size() : semi + 1;
        size_t start = token.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        size_t end = token.find_last_not_of(" \t");
        token = token.substr(start, end - start + 1);
        size_t eq = token.find('=');
        if (eq == std::string::npos) {
            EV_WARN << "ask_user: ignoring malformed token '" << token << "'\n";
            continue;
        }
        std::string name  = token.substr(0, eq);
        std::string value = token.substr(eq + 1);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
        kv[name] = value;
    }

    for (auto& r : rows) {
        auto it = kv.find(r.label);
        if (it == kv.end()) continue;
        std::string warn;
        if (applyValue(r, it->second, warn)) {
            EV << "ask_user: " << r.label << " = " << r.primary->str()
               << (r.broadcastTo.empty() ? "" : "  (broadcast)") << "\n";
        } else {
            EV_WARN << "ask_user: " << warn << "\n";
        }
    }
}

// -----------------------------------------------------------------------
// INITIALIZATION
// -----------------------------------------------------------------------
void MainNodeApp::initialize(int stage) {
    if (stage == 0) {
        taskCounter = 0;
        maxQueueSize = 50;
        commRange = 1e9;  // mainNode is "god mode"; no link is range-limited

        // Hide the mainNode submodule from Qtenv. MobileHost inherits an
        // icon from INET; setting `i=` in NED isn't always enough to clear
        // it, so we override the host's display tags at runtime. Done here
        // (stage 0) so the hide takes effect before Qtenv first renders.
        cDisplayString& hostDS = getParentModule()->getDisplayString();
        hostDS.setTagArg("i", 0, "");          // no icon
        hostDS.setTagArg("i2", 0, "");         // no overlay icon either
        hostDS.setTagArg("b", 0, "1");         // 1x1 bounding box
        hostDS.setTagArg("b", 1, "1");
        hostDS.setTagArg("is", 0, "vs");       // very small in case b is ignored
        hostDS.setTagArg("p", 0, "-10000");    // off the visible 2000x2000 canvas
        hostDS.setTagArg("p", 1, "-10000");
        hostDS.setTagArg("t", 0, "");          // clear any text label

        // OPTIONAL prompt: if the ini sets ask_user=true, pop a single dialog
        // for every tunable BEFORE we cache any values below.
        if (par("ask_user").boolValue()) {
            promptUserForParameters();
        }

        std::string algo = par("algorithmType").stringValue();
        // Priority-ordered queue is shared by every variant that cares about
        // priority (the two preempting allocators + the FIFO variant that
        // only re-orders the queue without preempting).
        if (algo == "AUCTION" || algo == "COST"
            || algo == "FIFO_PRIO" || algo == "FIFO_PREEMPT") {
            taskQueue.setup(compareTaskPriority);
        }

        // Cache cost-function tunables once; they're hot-path constants.
        costEnergyWeight        = par("costEnergyWeight").doubleValue();
        costHaltPriorityWeight  = par("costHaltPriorityWeight").doubleValue();
        costHaltGroupSizeWeight = par("costHaltGroupSizeWeight").doubleValue();
        costMbsRelocationWeight = par("costMbsRelocationWeight").doubleValue();
        costHaltPriorityTable.clear();   // populated below once priorityLevels is known

        // ---- Task-priority distribution -----------------------------------
        // Parse priorityLevels + priorityWeights into a CDF (or leave empty
        // for uniform sampling). Done once at init -- the weights are static
        // configuration, not a per-task volatile param.
        //
        // PARSER PATTERN: the same shape is repeated below for
        // costHaltPriorityWeights and taskDeadlineWeights (trim ws, split
        // on ',' or whitespace, verify count == priorityLevels, sanity-check
        // values, store in a std::vector<double>). Kept inline (rather than
        // factored out) so each block can throw cRuntimeError with the
        // specific param name and so per-parameter validation rules
        // (e.g. positivity, normalisation) live next to the data they
        // validate. Refactor with care -- the error messages are part of
        // the user-facing diagnostics.
        priorityLevels = par("priorityLevels").intValue();
        if (priorityLevels < 1) {
            throw cRuntimeError("priorityLevels must be >= 1 (got %d)", priorityLevels);
        }
        priorityCdf.clear();
        std::string weightsStr = par("priorityWeights").stringValue();
        // Trim leading/trailing whitespace so " " is treated as empty.
        auto isWs = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!weightsStr.empty() && isWs(weightsStr.front())) weightsStr.erase(weightsStr.begin());
        while (!weightsStr.empty() && isWs(weightsStr.back()))  weightsStr.pop_back();
        if (!weightsStr.empty()) {
            // Accept commas or whitespace as separators.
            std::vector<double> weights;
            std::string token;
            for (char c : weightsStr) {
                if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
                    if (!token.empty()) { weights.push_back(std::stod(token)); token.clear(); }
                } else {
                    token.push_back(c);
                }
            }
            if (!token.empty()) weights.push_back(std::stod(token));
            if ((int)weights.size() != priorityLevels) {
                throw cRuntimeError("priorityWeights has %d entries but priorityLevels=%d "
                                    "(expected one weight per priority class)",
                                    (int)weights.size(), priorityLevels);
            }
            double sum = 0;
            for (double w : weights) {
                if (w < 0) throw cRuntimeError("priorityWeights contains a negative value");
                sum += w;
            }
            if (sum <= 0) throw cRuntimeError("priorityWeights sum to zero");
            priorityCdf.reserve(weights.size());
            double acc = 0;
            for (double w : weights) { acc += w / sum; priorityCdf.push_back(acc); }
            priorityCdf.back() = 1.0;  // guard against float rounding
            EV << "Task priority distribution (weighted): ";
            double prev = 0;
            for (size_t i = 0; i < priorityCdf.size(); ++i) {
                EV << "p" << (i+1) << "=" << (priorityCdf[i] - prev) << " ";
                prev = priorityCdf[i];
            }
            EV << "\n";
        } else {
            EV << "Task priority distribution: uniform over 1.." << priorityLevels << "\n";
        }

        // ---- Per-priority halt-weight table (optional override) -----------
        // Parsed with the same separator rules as priorityWeights. When empty
        // we leave costHaltPriorityTable empty and computeHaltPenalty falls
        // back to the scalar * linear-ramp formula.
        std::string haltWeightsStr = par("costHaltPriorityWeights").stringValue();
        while (!haltWeightsStr.empty() && isWs(haltWeightsStr.front())) haltWeightsStr.erase(haltWeightsStr.begin());
        while (!haltWeightsStr.empty() && isWs(haltWeightsStr.back()))  haltWeightsStr.pop_back();
        if (!haltWeightsStr.empty()) {
            std::vector<double> weights;
            std::string token;
            for (char c : haltWeightsStr) {
                if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
                    if (!token.empty()) { weights.push_back(std::stod(token)); token.clear(); }
                } else {
                    token.push_back(c);
                }
            }
            if (!token.empty()) weights.push_back(std::stod(token));
            if ((int)weights.size() != priorityLevels) {
                throw cRuntimeError("costHaltPriorityWeights has %d entries but priorityLevels=%d "
                                    "(expected one weight per priority class)",
                                    (int)weights.size(), priorityLevels);
            }
            for (double w : weights) {
                if (w < 0) throw cRuntimeError("costHaltPriorityWeights contains a negative value");
            }
            costHaltPriorityTable = std::move(weights);
            EV << "Cost halt-priority table: ";
            for (size_t i = 0; i < costHaltPriorityTable.size(); ++i)
                EV << "p" << (i+1) << "=" << costHaltPriorityTable[i] << " ";
            EV << "\n";
        } else {
            EV << "Cost halt-priority: scalar=" << costHaltPriorityWeight
               << " with linear ramp (priorityLevels+1 - p), range "
               << costHaltPriorityWeight << ".." << (costHaltPriorityWeight * priorityLevels) << "\n";
        }

        // ---- Per-priority queue-wait deadlines (optional) -----------------
        // Same separator rules as the other tables. Empty => no expiry.
        taskDeadlineTable.clear();
        std::string deadlinesStr = par("taskDeadlineWeights").stringValue();
        while (!deadlinesStr.empty() && isWs(deadlinesStr.front())) deadlinesStr.erase(deadlinesStr.begin());
        while (!deadlinesStr.empty() && isWs(deadlinesStr.back()))  deadlinesStr.pop_back();
        if (!deadlinesStr.empty()) {
            std::vector<double> deadlines;
            std::string token;
            for (char c : deadlinesStr) {
                if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
                    if (!token.empty()) { deadlines.push_back(std::stod(token)); token.clear(); }
                } else {
                    token.push_back(c);
                }
            }
            if (!token.empty()) deadlines.push_back(std::stod(token));
            if ((int)deadlines.size() != priorityLevels) {
                throw cRuntimeError("taskDeadlineWeights has %d entries but priorityLevels=%d "
                                    "(expected one deadline per priority class)",
                                    (int)deadlines.size(), priorityLevels);
            }
            for (double d : deadlines) {
                if (d <= 0) throw cRuntimeError("taskDeadlineWeights entries must be positive (got %g)", d);
            }
            taskDeadlineTable = std::move(deadlines);
            EV << "Task wait deadlines (s): ";
            for (size_t i = 0; i < taskDeadlineTable.size(); ++i)
                EV << "p" << (i+1) << "=" << taskDeadlineTable[i] << " ";
            EV << "\n";
        } else {
            EV << "Task wait deadlines: disabled (tasks wait forever in queue)\n";
        }

        generateTaskTimer = new cMessage("generateTaskTimer");
        scheduleAt(simTime() + par("taskGenerationInterval"), generateTaskTimer);

        // Cache the drone comm range -- this is the strictest constraint when
        // deciding whether an MBS reposition would strand a busy drone.
        cModule *drone0 = getModuleByPath("^.^.drone[0].app[0]");
        if (drone0 != nullptr) {
            droneCommRange = drone0->par("drone_comm_range").doubleValue();
        }

        openCsv();

        createStatsPanel();
        refreshStatsPanel();
    }
}

// -----------------------------------------------------------------------
// MESSAGE HANDLING
// -----------------------------------------------------------------------
// All inputs are self-messages -- there are no network gates on the
// mainNode (drone/MBS feedback comes through direct method calls). Two
// kinds of self-messages exist:
//   (1) generateTaskTimer -- the periodic task-arrival tick. Reschedules
//       itself until taskLimit is reached.
//   (2) cMessage("taskDeadline") with kind() == taskId -- per-task
//       queue-wait expiry, scheduled in generateNewTask when
//       taskDeadlineTable is non-empty and cancelled in dispatchUnits.
// Anything else is dropped (kept as a guard against future stray sends).
void MainNodeApp::handleMessage(cMessage *msg) {
    if (msg == generateTaskTimer) {
        if (taskQueue.getLength() < maxQueueSize) {
            generateNewTask();
        }
        if (taskCounter < par("taskLimit").intValue()) {
            scheduleAt(simTime() + par("taskGenerationInterval"), generateTaskTimer);
        } else {
            EV << "Task limit of " << par("taskLimit").intValue() << " reached! No more tasks.\n";
        }
    }
    else if (msg->isSelfMessage() && strcmp(msg->getName(), "taskDeadline") == 0) {
        // The taskId is stashed in the message kind. Drop the map entry
        // (the handler does that) and then delete the fired message.
        int taskId = msg->getKind();
        onTaskDeadlineExpired(taskId);
        delete msg;
    }
    else {
        // No network gates anymore: anything else is unexpected.
        delete msg;
    }
}

// -----------------------------------------------------------------------
// TASK GENERATION & QUEUE MANAGEMENT
// -----------------------------------------------------------------------
int MainNodeApp::drawTaskPriority() {
    // Uniform fast-path: nothing was configured, fall back to the original
    // intuniform behaviour over [1..priorityLevels].
    if (priorityCdf.empty()) {
        return intuniform(1, priorityLevels);
    }
    // Weighted draw via inverse-CDF: priorityCdf[k] is the cumulative prob
    // of pri <= (k+1). Walk left-to-right -- vectors of 3..10 entries are
    // small enough that this is faster than binary search.
    double u = uniform(0, 1);
    for (size_t i = 0; i < priorityCdf.size(); ++i) {
        if (u < priorityCdf[i]) return static_cast<int>(i) + 1;
    }
    return priorityLevels;  // covers the u == 1.0 boundary case
}

void MainNodeApp::generateNewTask() {
    taskCounter++;
    TaskNotification *newTask = new TaskNotification();
    newTask->setTaskId(taskCounter);
    newTask->setTargetX(uniform(0, 2000));
    newTask->setTargetY(uniform(0, 2000));
    newTask->setPriority(drawTaskPriority());
    newTask->setRequiredDrones(intuniform(1, 3));
    newTask->setDuration(par("taskDuration").doubleValue());

    // Record metadata for later CSV row.
    TaskRecord rec;
    rec.taskId = taskCounter;
    rec.priority = newTask->getPriority();
    rec.requiredDrones = newTask->getRequiredDrones();
    rec.targetX = newTask->getTargetX();
    rec.targetY = newTask->getTargetY();
    rec.duration = newTask->getDuration();
    rec.generatedAt = simTime();
    rec.dispatchedAt = -1;
    rec.mbsId = -1;
    rec.dronesAssigned = 0;
    rec.dropEvents = 0;
    taskRecords[taskCounter] = rec;

    EV << "Generated Task #" << taskCounter << "\n";
    addTaskFigure(taskCounter, newTask->getTargetX(), newTask->getTargetY());
    taskQueue.insert(newTask);

    // Schedule a per-task wait deadline if one is configured for this
    // priority class. Cancelled in dispatchUnits when the task is served;
    // if the timer fires first the task is yanked from the queue and
    // finalized as EXPIRED.
    if (!taskDeadlineTable.empty()) {
        int p = rec.priority;
        if (p < 1) p = 1;
        if (p > priorityLevels) p = priorityLevels;
        double deadline = taskDeadlineTable[p - 1];
        cMessage *timer = new cMessage("taskDeadline");
        timer->setKind(taskCounter);   // carry the taskId through the scheduler
        taskDeadlineTimers[taskCounter] = timer;
        scheduleAt(simTime() + deadline, timer);
    }

    refreshStatsPanel();
    tryAssignTask();
}

void MainNodeApp::tryAssignTask() {
    if (taskQueue.isEmpty()) return;

    std::string algo = par("algorithmType").stringValue();
    if (algo == "FIFO" || algo == "FIFO_PRIO" || algo == "FIFO_PREEMPT") {
        // assignTaskFifo branches internally on the variant.
        assignTaskFifo();
    } else if (algo == "COST") {
        assignTaskCost();
    } else if (algo == "AUCTION") {
        // Legacy alias kept for backward compatibility with old .ini files.
        assignTaskCost();
    } else {
        throw cRuntimeError("Unknown algorithmType '%s' (expected 'FIFO', "
                            "'FIFO_PRIO', 'FIFO_PREEMPT' or 'COST')",
                            algo.c_str());
    }
}

// -----------------------------------------------------------------------
// DIRECT-CALL API (replaces UDP messages from drones)
// -----------------------------------------------------------------------
void MainNodeApp::onTaskCompletion(int taskId, int droneId) {
    Enter_Method("onTaskCompletion");
    EV << "Main Node received TaskCompletion for Task #" << taskId
       << " from Drone " << droneId << ".\n";
    handleTaskUpdate(taskId, droneId, true);
}

void MainNodeApp::onTaskDropNotification(int taskId, int droneId, double remainingDuration) {
    Enter_Method("onTaskDropNotification");
    EV << "Main Node received TaskDropNotification for Task #" << taskId
       << " from Drone " << droneId
       << ". Time remaining: " << remainingDuration << "s.\n";
    handleTaskUpdate(taskId, droneId, false, remainingDuration);
}

void MainNodeApp::handleTaskUpdate(int taskId, int droneId, bool isCompletion, double remainingDuration) {
    // Drone failure path (death, recharge, connection timeout):
    //   * survivors stay on the task -- they just take longer to finish
    //     (duration scales by oldCount/newCount, per "fewer drones = slower").
    //   * if the failing drone was the LAST one, the task is marked DROPPED.
    //
    // Completion path:
    //   * just decrement; when count reaches 0 the task is marked COMPLETED.
    //     (Survivors don't need extension on a completion -- the completer
    //      already finished its share of the work.)
    auto rit = taskRecords.find(taskId);
    if (rit != taskRecords.end() && !isCompletion) rit->second.dropEvents++;

    auto cit = activeTaskDroneCount.find(taskId);
    if (cit == activeTaskDroneCount.end()) return;

    int oldCount = cit->second;
    int newCount = oldCount - 1;
    cit->second = newCount;

    if (!isCompletion) {
        EV << "WARNING: drone " << droneId << " bailed on Task #" << taskId
           << " (now " << newCount << "/" << oldCount << " drones).\n";
    }

    if (newCount > 0) {
        if (!isCompletion) {
            // The team shrank but the task continues; survivors do extra work.
            double scale = (double)oldCount / (double)newCount;
            scaleRemainingDurationForTask(taskId, droneId, scale);
        }
        return;
    }

    // newCount == 0 -- this was the last drone. Finalize the task.
    EV << "Task #" << taskId << " has no drones left ("
       << (isCompletion ? "completed" : "all bailed") << ").\n";
    finalizeTask(taskId, isCompletion ? "COMPLETED" : "DROPPED");
}

void MainNodeApp::removeTaskFromMbs(int taskId) {
    int totalMbs = getParentModule()->getParentModule()->par("numMbs").intValue();

    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);

        std::vector<int> currentTasks = mbsApp->getAssignedTasks();
        if (std::find(currentTasks.begin(), currentTasks.end(), taskId) != currentTasks.end()) {
            mbsApp->removeTask(taskId);
            break;
        }
    }
}

// -----------------------------------------------------------------------
// SHARED HELPERS for "some drones leave, others slow down"
// -----------------------------------------------------------------------
// Tell every drone still on `taskId` (except the one leaving) to extend its
// remaining work by `scale`. Skipping the leaver is important because the
// caller is in the middle of removing it, and its bookkeeping state hasn't
// settled yet.
void MainNodeApp::scaleRemainingDurationForTask(int taskId, int leavingDroneId, double scale) {
    if (scale <= 1.0) return;
    int totalDrones = getParentModule()->getParentModule()->par("numDrones").intValue();
    int affected = 0;
    for (int i = 0; i < totalDrones; i++) {
        if (i == leavingDroneId) continue;
        cModule *dm = getModuleByPath(("^.^.drone[" + std::to_string(i) + "].app[0]").c_str());
        auto dApp = check_and_cast<uavswarmta::DroneApp *>(dm);
        if (dApp->getCurrentTaskId() == taskId) {
            dApp->extendRemainingDuration(scale);
            affected++;
        }
    }
    EV << "Task #" << taskId << " survivors (" << affected
       << " drones) had remaining duration scaled by " << scale << "x.\n";
}

// Bookkeeping + CSV row for a task that has ended (one way or another).
// Two outcome buckets are tracked for the HUD counter:
//   COMPLETED                      -> tasksCompleted++
//   DROPPED, EXPIRED (treated equivalently here -- both are failures from
//                     the operator's point of view; the CSV preserves the
//                     distinction so post-processing can split them).
//   UNFINISHED / QUEUED            -> only emitted from finish() for tasks
//                                     still in flight / still waiting when
//                                     the sim ends; never reach finalizeTask
//                                     during steady-state operation.
void MainNodeApp::finalizeTask(int taskId, const std::string& outcome) {
    if (outcome == "DROPPED" || outcome == "EXPIRED") tasksDropped++;
    else                                              tasksCompleted++;

    // Defensive: deadline timer is normally already gone by now
    // (cancelled at dispatch, or removed by the expiry handler).
    cancelTaskDeadline(taskId);

    auto rit = taskRecords.find(taskId);
    if (rit != taskRecords.end()) {
        writeCsvRow(rit->second, outcome);
        taskRecords.erase(rit);
    }

    activeTaskDroneCount.erase(taskId);
    activeTaskLocations.erase(taskId);
    activeTaskHadDrop.erase(taskId);
    removeTaskFigure(taskId);
    removeTaskFromMbs(taskId);
    refreshStatsPanel();
    tryAssignTask();
}

// Cancel + free a pending wait-deadline timer. Idempotent / safe to call
// when no timer exists (e.g. when deadlines are disabled, or when the
// timer has already fired and been deleted by handleMessage).
void MainNodeApp::cancelTaskDeadline(int taskId) {
    auto it = taskDeadlineTimers.find(taskId);
    if (it == taskDeadlineTimers.end()) return;
    cancelAndDelete(it->second);
    taskDeadlineTimers.erase(it);
}

// Called from handleMessage when a task's deadline timer fires.
// Walks the queue, pulls the matching TaskNotification out (if still
// there), and finalizes it as EXPIRED. No-op if the task was dispatched
// or already finalized through some other path between the timer firing
// and this handler running (shouldn't happen in practice because dispatch
// cancels the timer, but defensive).
void MainNodeApp::onTaskDeadlineExpired(int taskId) {
    // Drop the map entry first so subsequent cancelTaskDeadline() calls
    // (e.g. from finalizeTask) become no-ops -- the message itself is
    // owned by handleMessage now and will be deleted there.
    taskDeadlineTimers.erase(taskId);

    auto rit = taskRecords.find(taskId);
    if (rit == taskRecords.end()) {
        EV_WARN << "Deadline fired for unknown Task #" << taskId << " (already finalized?)\n";
        return;
    }
    if (rit->second.dispatchedAt >= SIMTIME_ZERO) {
        // Race: dispatched between timer firing and this handler.
        EV_WARN << "Deadline fired for Task #" << taskId
                << " but it was already dispatched; ignoring.\n";
        return;
    }

    // Find and remove the TaskNotification from the queue.
    TaskNotification *found = nullptr;
    for (cQueue::Iterator it(taskQueue); !it.end(); ++it) {
        TaskNotification *t = check_and_cast<TaskNotification *>(*it);
        if (t->getTaskId() == taskId) { found = t; break; }
    }
    if (found != nullptr) {
        taskQueue.remove(found);
        EV << "Task #" << taskId << " EXPIRED in queue (priority "
           << found->getPriority() << ", waited "
           << (simTime() - rit->second.generatedAt) << "s)\n";
        delete found;
    } else {
        EV_WARN << "Deadline fired for Task #" << taskId
                << " but it was not in the queue.\n";
    }

    finalizeTask(taskId, "EXPIRED");
}

// Bookkeeping for a single COST-allocator preemption.
//   * if other drones remain on the old task, they pick up the slack
//     (longer duration, no penalty to stats)
//   * if this was the LAST drone, the old task is marked DROPPED -- the
//     allocator chose to sacrifice it for a higher-priority task, and that
//     decision shows up in the stats so it can be measured/tuned.
void MainNodeApp::handlePreemption(int oldTaskId, int leavingDroneId, int newTaskId) {
    auto cit = activeTaskDroneCount.find(oldTaskId);
    if (cit == activeTaskDroneCount.end()) return;

    int oldCount = cit->second;
    int newCount = oldCount - 1;
    cit->second = newCount;

    EV << "COST: preempting drone " << leavingDroneId << " from Task #"
       << oldTaskId << " for new Task #" << newTaskId
       << " (group " << oldCount << " -> " << newCount << ").\n";

    if (newCount > 0) {
        double scale = (double)oldCount / (double)newCount;
        scaleRemainingDurationForTask(oldTaskId, leavingDroneId, scale);
    } else {
        EV << "Task #" << oldTaskId << " lost its last drone to preemption; "
           << "marking as DROPPED.\n";
        finalizeTask(oldTaskId, "DROPPED");
    }
}

// -----------------------------------------------------------------------
// FIFO ASSIGNMENT (covers FIFO, FIFO_PRIO and FIFO_PREEMPT)
// -----------------------------------------------------------------------
// The three FIFO variants share this dispatcher and differ in two axes:
//   * queue ordering (FIFO insertion vs priority comparator), wired up
//     once in initialize() based on algorithmType -- nothing to do here.
//   * drone picking: plain FIFO/FIFO_PRIO only accept idle drones; the
//     FIFO_PREEMPT variant additionally interrupts drones currently on
//     a STRICTLY lower-priority task to fill the team. MBS preemption is
//     intentionally NOT enabled for any FIFO variant -- that's the COST
//     allocator's exclusive feature.
void MainNodeApp::assignTaskFifo() {
    TaskNotification *task = check_and_cast<TaskNotification *>(taskQueue.front());
    double targetX = task->getTargetX();
    double targetY = task->getTargetY();
    int reqDrones = task->getRequiredDrones();
    int priority  = task->getPriority();
    std::string algo = par("algorithmType").stringValue();
    bool allowPreempt = (algo == "FIFO_PREEMPT");

    EV << "FIFO[" << algo << "] trying to assign " << reqDrones
       << " drones to task " << task->getTaskId()
       << " (prio=" << priority << ") at (" << targetX << "," << targetY << ")\n";

    std::vector<cModule*> assignedDrones = allowPreempt
        ? findClosestDronesWithPreempt(targetX, targetY, reqDrones, priority)
        : findClosestIdleDrones(targetX, targetY, reqDrones);
    if ((int)assignedDrones.size() < reqDrones) {
        EV << "Couldn't find enough drones to assign to task: " << task->getTaskId() << "\n";
        return;
    }

    double centroidX = 0, centroidY = 0;
    cModule* assignedMbs = findSuitableMbs(targetX, targetY, centroidX, centroidY);
    if (assignedMbs == nullptr) {
        EV << "Couldn't find MBS to cover task: " << task->getTaskId() << "\n";
        return;
    }

    taskQueue.pop();
    // preemptAndDispatchUnits is a no-op on the preempt side when every
    // selected drone is idle, so it's safe to use unconditionally for the
    // preempting variant; the plain FIFO path keeps the lighter dispatchUnits.
    if (allowPreempt) {
        preemptAndDispatchUnits(task, assignedDrones, assignedMbs, centroidX, centroidY);
    } else {
        dispatchUnits(task, assignedDrones, assignedMbs, centroidX, centroidY);
    }

    delete task;
    tryAssignTask();
}

// -----------------------------------------------------------------------
// COST ASSIGNMENT (auction with preemption, per the project paper)
// -----------------------------------------------------------------------
// The COST allocator mirrors the algorithm in the preliminary report:
//   step 1 - pick the lowest-cost set of UAVs (idle OR busy on a lower-prio
//            task that we are willing to halt)
//   step 2 - pick the lowest-cost MBS to cover the new task site
// Cost weights are NED params so the math can be tuned at run time.
//
// PRIORITY CONVENTION: integer 1..3 where 1 = HIGH urgency, 3 = LOW urgency
// (matches generateNewTask's intuniform(1,3)). Lower number = more important.
// A busy drone is preemptible only when its currentPriority > newPriority.
//
// The two helpers below are the *only* place the cost math lives -- keep them
// short. Multipliers come from NED params named cost*.
// -----------------------------------------------------------------------

double MainNodeApp::computeHaltPenalty(int currentPriority, int currentGroupSize) {
    // halt(t) -- the punishment for abandoning the task currently being run.
    // Higher-priority (lower-numbered) tasks are MORE painful to halt.
    //
    // Two sources of the per-priority weight:
    //   * costHaltPriorityTable (set via costHaltPriorityWeights) is used
    //     verbatim when non-empty -- entry (p-1) is the weight for priority p.
    //   * Otherwise we fall back to costHaltPriorityWeight scaled by a linear
    //     ramp (priorityLevels + 1 - p), so priority 1 (HIGH) gets the largest
    //     weight and priority priorityLevels (LOW) gets weight 1. This
    //     generalises the original (4 - p) formula to any priorityLevels.
    //
    // currentPriority is clamped into [1, priorityLevels]: sentinel values
    // (e.g. 99 when the task record is missing) are treated as LOW.
    int p = currentPriority;
    if (p < 1) p = 1;
    if (p > priorityLevels) p = priorityLevels;

    double prioWeight;
    if (!costHaltPriorityTable.empty()) {
        prioWeight = costHaltPriorityTable[p - 1];
    } else {
        prioWeight = costHaltPriorityWeight * (priorityLevels + 1 - p);
    }
    return prioWeight + costHaltGroupSizeWeight * currentGroupSize;
}

double MainNodeApp::computeDroneCost(cModule* droneMod, double targetX, double targetY, int newTaskPriority) {
    auto droneApp = check_and_cast<uavswarmta::DroneApp *>(droneMod);
    auto dMob = check_and_cast<inet::IMobility *>(
        droneMod->getParentModule()->getSubmodule("mobility"));

    double dist = dMob->getCurrentPosition().distance(
        inet::Coord(targetX, targetY, dMob->getCurrentPosition().z));

    // c(u, t) = e(u, t) + halt(currentTask) -- per the paper.
    double energyCost = costEnergyWeight * dist;
    double haltCost   = 0.0;
    int oldTaskId = -1, oldPriority = -1, groupSize = 0;

    if (!droneApp->isIdle()) {
        oldTaskId = droneApp->getCurrentTaskId();
        oldPriority = 99;
        auto rit = taskRecords.find(oldTaskId);
        if (rit != taskRecords.end()) oldPriority = rit->second.priority;
        auto cit = activeTaskDroneCount.find(oldTaskId);
        if (cit != activeTaskDroneCount.end()) groupSize = cit->second;
        haltCost = computeHaltPenalty(oldPriority, groupSize);
    }

    int droneIdx = droneMod->getParentModule()->getIndex();
    EV << "  [COST/drone] D" << droneIdx
       << (droneApp->isIdle() ? " (idle)"
                              : (std::string(" (busy on T") + std::to_string(oldTaskId)
                                 + " prio=" + std::to_string(oldPriority)
                                 + " group=" + std::to_string(groupSize) + ")").c_str())
       << " dist=" << dist
       << " energyCost=" << energyCost
       << " haltCost=" << haltCost
       << " TOTAL=" << (energyCost + haltCost) << "\n";

    return energyCost + haltCost;
}

std::vector<cModule*> MainNodeApp::findCostMinimalDrones(
        double targetX, double targetY, int reqDrones, int newPriority) {
    // Eligible pool A = { idle drones } U { busy drones with a STRICTLY
    // lower-priority current task } (per step 1 of the algorithm). Dead
    // drones and equally-or-more-important busy drones are excluded.
    std::vector<std::pair<double, cModule*>> ranked;
    int totalDrones = getParentModule()->getParentModule()->par("numDrones").intValue();

    EV << " --- AUCTION drone scoring (need " << reqDrones
       << ", newPrio=" << newPriority << ") ---\n";
    int skippedDead = 0, skippedBusyHighPrio = 0;

    for (int i = 0; i < totalDrones; i++) {
        cModule *droneMod = getModuleByPath(("^.^.drone[" + std::to_string(i) + "].app[0]").c_str());
        auto droneApp = check_and_cast<uavswarmta::DroneApp *>(droneMod);
        if (!droneApp->isAlive()) { skippedDead++; continue; }

        bool eligible = false;
        if (droneApp->isIdle()) {
            eligible = true;
        } else {
            int oldTaskId = droneApp->getCurrentTaskId();
            auto rit = taskRecords.find(oldTaskId);
            if (rit != taskRecords.end() && rit->second.priority > newPriority) {
                eligible = true;   // current task is less urgent -> preemptible
            }
        }
        if (!eligible) { skippedBusyHighPrio++; continue; }

        double c = computeDroneCost(droneMod, targetX, targetY, newPriority);
        ranked.push_back({c, droneMod});
    }

    EV << " --- (" << ranked.size() << " eligible, " << skippedDead
       << " dead, " << skippedBusyHighPrio << " locked on equal/higher prio) ---\n";

    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<double, cModule*>& a, const std::pair<double, cModule*>& b) {
                  return a.first < b.first;
              });

    std::vector<cModule*> picked;
    for (int i = 0; i < reqDrones && i < (int)ranked.size(); i++) {
        picked.push_back(ranked[i].second);
        int idx = ranked[i].second->getParentModule()->getIndex();
        EV << " >> AUCTION picked drone D" << idx
           << " with cost " << ranked[i].first << "\n";
    }
    return picked;
}

cModule* MainNodeApp::findCostMinimalMbs(
        double targetX, double targetY, int newPriority,
        double& outCentroidX, double& outCentroidY) {
    // Step 2: pick the MBS that minimises c(m) = halt-of-its-tasks + relocation.
    // Three buckets of MBSs are evaluated uniformly:
    //   (a) idle MBS                       -> c = relocation distance
    //   (b) active MBS that can EXTEND to cover the new task (no drop)
    //                                      -> c = relocation distance
    //   (c) active MBS whose current tasks all have priority > newPriority,
    //       i.e. we are willing to drop them -> c = relocation + halt sum
    // Active MBSs holding a higher-or-equal-priority task are skipped.
    inet::Coord newTaskPos(targetX, targetY, 0);
    int totalMbs = getParentModule()->getParentModule()->par("numMbs").intValue();

    cModule* bestMbs = nullptr;
    double bestCost = std::numeric_limits<double>::infinity();
    double bestCx = targetX, bestCy = targetY;
    int bestIdx = -1;

    EV << " --- AUCTION MBS scoring (newPrio=" << newPriority << ") ---\n";

    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);
        auto mob = check_and_cast<inet::IMobility *>(
            mbsMod->getParentModule()->getSubmodule("mobility"));
        inet::Coord mbsPos = mob->getCurrentPosition();

        double proposedCx = targetX, proposedCy = targetY;
        double haltSum = 0.0;
        bool feasible = false;
        const char* mode = "IDLE";

        if (mbsApp->isIdle()) {
            feasible = true;
        } else {
            // First try EXTEND: can the MBS shift to a centroid that still covers
            // all of its tasks + the new task? (No drops, no halt cost.)
            std::vector<int> currentTasks = mbsApp->getAssignedTasks();
            double sumX = targetX, sumY = targetY;
            int count = 1;
            for (int tid : currentTasks) {
                sumX += activeTaskLocations[tid].x;
                sumY += activeTaskLocations[tid].y;
                count++;
            }
            double cX = sumX / count, cY = sumY / count;
            inet::Coord centroid(cX, cY, 0);
            double mbsRange = mbsApp->getCommRange();
            double effectiveRange = (droneCommRange > 0 && droneCommRange < mbsRange)
                                        ? droneCommRange : mbsRange;
            bool extendOk = (newTaskPos.distance(centroid) <= effectiveRange);
            for (int tid : currentTasks) {
                if (activeTaskLocations[tid].distance(centroid) > effectiveRange) {
                    extendOk = false; break;
                }
            }

            if (extendOk) {
                proposedCx = cX; proposedCy = cY;
                feasible = true;
                mode = "EXTEND";
            } else {
                // Falling back to RELOCATE-AND-DROP: only valid if every current
                // task is less urgent than the new one.
                bool allLower = true;
                for (int tid : currentTasks) {
                    auto rit = taskRecords.find(tid);
                    int p = (rit != taskRecords.end()) ? rit->second.priority : 99;
                    if (p <= newPriority) { allLower = false; break; }
                    int gs = activeTaskDroneCount.count(tid) ? activeTaskDroneCount.at(tid) : 0;
                    haltSum += computeHaltPenalty(p, gs);
                }
                if (allLower) { feasible = true; mode = "DROP"; }
                else          { mode = "LOCKED"; }
            }
        }

        if (!feasible) {
            EV << "  [COST/mbs] M" << i << " " << mode << " -> infeasible\n";
            continue;
        }

        double relocDist = mbsPos.distance(inet::Coord(proposedCx, proposedCy, mbsPos.z));
        double cost = costMbsRelocationWeight * relocDist + haltSum;

        EV << "  [COST/mbs] M" << i << " " << mode
           << " relocDist=" << relocDist
           << " haltSum=" << haltSum
           << " TOTAL=" << cost << "\n";

        if (cost < bestCost) {
            bestCost = cost;
            bestMbs = mbsMod;
            bestCx = proposedCx;
            bestCy = proposedCy;
            bestIdx = i;
        }
    }

    if (bestMbs != nullptr) {
        outCentroidX = bestCx;
        outCentroidY = bestCy;
        EV << " >> AUCTION picked MBS M" << bestIdx
           << " centroid=(" << bestCx << "," << bestCy
           << ") cost=" << bestCost << "\n";
    } else {
        EV << " >> AUCTION: NO feasible MBS!\n";
    }
    return bestMbs;
}

void MainNodeApp::preemptAndDispatchUnits(TaskNotification* task,
        std::vector<cModule*>& drones, cModule* mbs,
        double centroidX, double centroidY) {
    // Before the normal dispatch sequence, any drone we picked that's currently
    // busy must release its old task. We do the accounting HERE on the main
    // node (decrement counters, mark drop, possibly finalise) and then call
    // DroneApp::preempt() which just cleans local state.
    //
    // Order matters: bookkeep ALL preemptions first (so the cascading
    // finalisations / new tryAssignTask() calls happen against a coherent
    // world), THEN call preempt() on the drones, THEN dispatch the new task.

    struct Preemption { cModule* drone; int oldTaskId; int droneId; };
    std::vector<Preemption> preemptions;
    for (cModule* dMod : drones) {
        auto dApp = check_and_cast<uavswarmta::DroneApp *>(dMod);
        if (!dApp->isIdle()) {
            preemptions.push_back({dMod, dApp->getCurrentTaskId(),
                                   dMod->getParentModule()->getIndex()});
        }
    }

    for (auto& p : preemptions) {
        // handlePreemption decrements activeTaskDroneCount and either
        //   (a) extends the survivors' duration if peers remain, or
        //   (b) re-queues the old task if this was the last drone on it.
        // Either way, the old task is NOT dropped.
        handlePreemption(p.oldTaskId, p.droneId, task->getTaskId());
    }
    for (auto& p : preemptions) {
        auto dApp = check_and_cast<uavswarmta::DroneApp *>(p.drone);
        dApp->preempt();
    }

    dispatchUnits(task, drones, mbs, centroidX, centroidY);
}

void MainNodeApp::assignTaskCost() {
    TaskNotification *task = check_and_cast<TaskNotification *>(taskQueue.front());
    double targetX = task->getTargetX();
    double targetY = task->getTargetY();
    int reqDrones = task->getRequiredDrones();
    int priority  = task->getPriority();
    EV << "\n===== AUCTION ROUND for Task #" << task->getTaskId()
       << " prio=" << priority << " needs=" << reqDrones
       << " at (" << targetX << "," << targetY
       << ") | queueDepth=" << taskQueue.getLength() << " =====\n";

    std::vector<cModule*> assignedDrones =
        findCostMinimalDrones(targetX, targetY, reqDrones, priority);
    if ((int)assignedDrones.size() < reqDrones) {
        EV << "COST: not enough eligible drones for task " << task->getTaskId() << "\n";
        return;
    }

    double centroidX = 0, centroidY = 0;
    cModule* assignedMbs = findCostMinimalMbs(targetX, targetY, priority, centroidX, centroidY);
    if (assignedMbs == nullptr) {
        EV << "COST: no MBS available for task " << task->getTaskId() << "\n";
        return;
    }

    taskQueue.pop();
    preemptAndDispatchUnits(task, assignedDrones, assignedMbs, centroidX, centroidY);

    delete task;
    tryAssignTask();
}

std::vector<cModule*> MainNodeApp::findClosestIdleDrones(double targetX, double targetY, int reqDrones) {
    // Plain FIFO / FIFO_PRIO drone picker: scan every drone, keep the IDLE
    // ones, sort by Euclidean distance to the task, return the closest N.
    // Busy drones are ignored entirely -- no preemption in these variants.
    std::vector<std::pair<double, cModule*>> idleDrones;
    int totalDrones = getParentModule()->getParentModule()->par("numDrones").intValue();

    for (int i = 0; i < totalDrones; i++) {
        cModule *droneMod = getModuleByPath(("^.^.drone[" + std::to_string(i) + "].app[0]").c_str());
        auto droneApp = check_and_cast<uavswarmta::DroneApp *>(droneMod);

        if (droneApp->isIdle()) {
            auto dMob = check_and_cast<inet::IMobility *>(droneMod->getParentModule()->getSubmodule("mobility"));
            double dist = dMob->getCurrentPosition().distance(inet::Coord(targetX, targetY, 0));
            idleDrones.push_back({dist, droneMod});
        }
    }

    std::sort(idleDrones.begin(), idleDrones.end());

    std::vector<cModule*> selectedDrones;
    for (int i = 0; i < reqDrones && i < (int)idleDrones.size(); i++) {
        selectedDrones.push_back(idleDrones[i].second);
    }
    return selectedDrones;
}

// FIFO_PREEMPT picker -- "closest idle drones first, then top up with the
// closest drones currently on a STRICTLY lower-priority task." We always
// prefer idle, even if a busy lower-priority drone happens to be closer:
// the goal is to avoid the disruption cost of a preemption when an idle
// alternative exists. (COST's auction is the place to make that tradeoff
// quantitatively; FIFO_PREEMPT's rule is intentionally simpler.)
std::vector<cModule*> MainNodeApp::findClosestDronesWithPreempt(
        double targetX, double targetY, int reqDrones, int newPriority) {
    int totalDrones = getParentModule()->getParentModule()->par("numDrones").intValue();

    std::vector<std::pair<double, cModule*>> idle;
    std::vector<std::pair<double, cModule*>> preemptible;

    for (int i = 0; i < totalDrones; i++) {
        cModule *droneMod = getModuleByPath(("^.^.drone[" + std::to_string(i) + "].app[0]").c_str());
        auto droneApp = check_and_cast<uavswarmta::DroneApp *>(droneMod);
        auto dMob = check_and_cast<inet::IMobility *>(
            droneMod->getParentModule()->getSubmodule("mobility"));
        double dist = dMob->getCurrentPosition().distance(
            inet::Coord(targetX, targetY, 0));

        if (droneApp->isIdle()) {
            idle.push_back({dist, droneMod});
            continue;
        }
        int curTaskId = droneApp->getCurrentTaskId();
        auto rit = taskRecords.find(curTaskId);
        if (rit == taskRecords.end()) continue;             // shouldn't happen
        int curPrio = rit->second.priority;
        if (curPrio > newPriority) {                        // strictly lower urgency
            preemptible.push_back({dist, droneMod});
        }
    }

    std::sort(idle.begin(),        idle.end());
    std::sort(preemptible.begin(), preemptible.end());

    std::vector<cModule*> selected;
    selected.reserve(reqDrones);
    for (auto& p : idle) {
        if ((int)selected.size() >= reqDrones) break;
        selected.push_back(p.second);
    }
    for (auto& p : preemptible) {
        if ((int)selected.size() >= reqDrones) break;
        selected.push_back(p.second);
    }

    if ((int)selected.size() < reqDrones) {
        EV << "FIFO_PREEMPT: only " << selected.size()
           << "/" << reqDrones << " drones available (idle=" << idle.size()
           << ", preemptible=" << preemptible.size()
           << ", newPriority=" << newPriority << ")\n";
    } else {
        int nIdle = std::min((int)idle.size(), reqDrones);
        int nPreempt = (int)selected.size() - nIdle;
        if (nPreempt > 0) {
            EV << "FIFO_PREEMPT: picked " << nIdle << " idle + "
               << nPreempt << " preempted drones for prio=" << newPriority << "\n";
        }
    }
    return selected;
}

cModule* MainNodeApp::findSuitableMbs(double targetX, double targetY, double& outCentroidX, double& outCentroidY) {
    // FIFO-side MBS picker. Two-attempt strategy:
    //   A) prefer EXTENDING an already-active MBS so the new task and its
    //      existing tasks all stay within range of a recomputed centroid.
    //      No drops, no movement waste.
    //   B) fall back to the closest IDLE MBS, centred on the new task.
    // (The COST allocator uses findCostMinimalMbs instead, which considers
    // all options uniformly and may RELOCATE-AND-DROP lower-priority tasks.)
    int totalMbs = getParentModule()->getParentModule()->par("numMbs").intValue();
    inet::Coord newTaskPos(targetX, targetY, 0);

    // Attempt A: extend an ACTIVE MBS to also cover this task.
    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);

        if (!mbsApp->isIdle()) {
            std::vector<int> currentTasks = mbsApp->getAssignedTasks();
            double sumX = targetX, sumY = targetY;
            int count = 1;
            for (int tid : currentTasks) {
                sumX += activeTaskLocations[tid].x;
                sumY += activeTaskLocations[tid].y;
                count++;
            }
            double cX = sumX / count;
            double cY = sumY / count;
            inet::Coord proposedCentroid(cX, cY, 0);

            // The strictest cap is the DRONE's comm range -- a drone parked at a
            // task site checks its distance to the MBS using its own range. If we
            // moved the MBS beyond that, busy drones would lose connection.
            double mbsRange = mbsApp->getCommRange();
            double effectiveRange = (droneCommRange > 0 && droneCommRange < mbsRange)
                                        ? droneCommRange : mbsRange;

            bool isValid = true;
            if (newTaskPos.distance(proposedCentroid) > effectiveRange) isValid = false;
            for (int tid : currentTasks) {
                if (activeTaskLocations[tid].distance(proposedCentroid) > effectiveRange) {
                    isValid = false;
                    break;
                }
            }

            if (isValid) {
                outCentroidX = cX;
                outCentroidY = cY;
                return mbsMod;
            }
        }
    }

    // Attempt B: pick the IDLE MBS whose current position is closest to the task.
    cModule* bestMbs = nullptr;
    double bestDist = std::numeric_limits<double>::infinity();
    for (int i = 0; i < totalMbs; i++) {
        cModule *mbsMod = getModuleByPath(("^.^.mbs[" + std::to_string(i) + "].app[0]").c_str());
        auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbsMod);
        if (mbsApp->isIdle()) {
            auto mob = check_and_cast<inet::IMobility *>(
                mbsMod->getParentModule()->getSubmodule("mobility"));
            double d = mob->getCurrentPosition().distance(newTaskPos);
            if (d < bestDist) {
                bestDist = d;
                bestMbs = mbsMod;
            }
        }
    }
    if (bestMbs != nullptr) {
        outCentroidX = targetX;
        outCentroidY = targetY;
        return bestMbs;
    }

    return nullptr;
}

void MainNodeApp::dispatchUnits(TaskNotification* task, std::vector<cModule*>& drones, cModule* mbs, double centroidX, double centroidY) {
    // Common dispatch tail used by every allocator (FIFO variants directly,
    // COST via preemptAndDispatchUnits). Responsibilities:
    //   1. Record activeTask* state so subsequent allocator rounds know
    //      the drones/MBSs are busy.
    //   2. Stamp dispatchedAt + dronesAssigned on the TaskRecord (used by
    //      CSV waitTime, and by the deadline-expiry race guard).
    //   3. Cancel the queue-wait deadline timer -- once in flight, the
    //      task is no longer subject to taskDeadlineWeights.
    //   4. Compute a shared "sync wait" = max travel time across all
    //      participants, so drones don't start before the MBS arrives.
    //   5. Issue the orders (mbsApp->assignInitialTask / addCoveredTask,
    //      droneApp->assignTask) and flip the figure colour.
    int taskId = task->getTaskId();
    double targetX = task->getTargetX();
    double targetY = task->getTargetY();

    activeTaskLocations[taskId] = inet::Coord(targetX, targetY, 0);
    activeTaskDroneCount[taskId] = drones.size();
    activeTaskHadDrop[taskId] = false;

    // Stamp dispatch metadata on the record.
    auto rit = taskRecords.find(taskId);
    if (rit != taskRecords.end()) {
        rit->second.dispatchedAt = simTime();
        rit->second.dronesAssigned = (int)drones.size();
    }

    // The task is now in flight -- the queue-wait deadline no longer applies.
    cancelTaskDeadline(taskId);

    double maxTravelTime = 0.0;

    // MBS travel time
    auto mbsMob = check_and_cast<inet::IMobility *>(mbs->getParentModule()->getSubmodule("mobility"));
    double mbsDist = mbsMob->getCurrentPosition().distance(inet::Coord(centroidX, centroidY, 0));
    double mbsTravelTime = mbsDist / mbs->par("speed").doubleValue();
    if (mbsTravelTime > maxTravelTime) maxTravelTime = mbsTravelTime;

    // Drone travel times
    for (cModule* dMod : drones) {
        auto dMob = check_and_cast<inet::IMobility *>(dMod->getParentModule()->getSubmodule("mobility"));
        double dDist = dMob->getCurrentPosition().distance(inet::Coord(targetX, targetY, 0));
        double dTravelTime = dDist / dMod->par("speed").doubleValue();
        if (dTravelTime > maxTravelTime) maxTravelTime = dTravelTime;
    }

    auto mbsApp = check_and_cast<uavswarmta::MbsApp *>(mbs);
    int mbsId = mbs->getParentModule()->getIndex();

    // Stamp the chosen MBS on the record now that we have it.
    if (rit != taskRecords.end()) {
        rit->second.mbsId = mbsId;
    }

    if (mbsApp->isIdle()) {
        mbsApp->assignInitialTask(taskId, centroidX, centroidY);
    } else {
        mbsApp->addCoveredTask(taskId);
        mbsApp->shiftPosition(centroidX, centroidY);
    }

    for (cModule* dMod : drones) {
        auto droneApp = check_and_cast<uavswarmta::DroneApp *>(dMod);
        droneApp->assignTask(taskId, maxTravelTime, task->getDuration(), targetX, targetY, mbsId);
    }

    markTaskActive(taskId);
    refreshStatsPanel();

    EV << "Dispatched Task #" << taskId << " to " << drones.size() << " drones and MBS " << mbsId
       << ". Centroid at (" << centroidX << ", " << centroidY << "). Sync wait: " << maxTravelTime << "s.\n";
}

// -----------------------------------------------------------------------
// CANVAS MARKERS (visible in Qtenv)
// -----------------------------------------------------------------------
void MainNodeApp::addTaskFigure(int taskId, double x, double y) {
    cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();

    auto group = new cGroupFigure(("task" + std::to_string(taskId)).c_str());

    // Halo so the task is visible from far away.
    auto halo = new cOvalFigure("halo");
    halo->setBounds(cFigure::Rectangle(x - 35, y - 35, 70, 70));
    halo->setLineColor(cFigure::Color("red"));
    halo->setLineWidth(2);
    halo->setFilled(true);
    halo->setFillColor(cFigure::Color("yellow"));
    halo->setFillOpacity(0.25);
    group->addFigure(halo);

    // Crosshair dot at the exact spot.
    auto dot = new cOvalFigure("dot");
    dot->setBounds(cFigure::Rectangle(x - 5, y - 5, 10, 10));
    dot->setLineColor(cFigure::Color("red"));
    dot->setFilled(true);
    dot->setFillColor(cFigure::Color("red"));
    group->addFigure(dot);

    // Label "T<id>" placed to the right of the dot so it does not overlap the halo.
    auto text = new cTextFigure("label");
    text->setPosition(cFigure::Point(x + 12, y));
    text->setText(("T" + std::to_string(taskId)).c_str());
    text->setAnchor(cFigure::ANCHOR_W);
    text->setColor(cFigure::Color("black"));
    text->setFont(cFigure::Font("", 14, cFigure::FONT_BOLD));
    group->addFigure(text);

    canvas->addFigure(group);
    taskFigures[taskId] = group;
    refreshStatsPanel();
}

void MainNodeApp::markTaskActive(int taskId) {
    auto it = taskFigures.find(taskId);
    if (it == taskFigures.end()) return;
    // Turn the halo green to signal "someone is on it".
    auto group = check_and_cast<cGroupFigure *>(it->second);
    if (auto halo = dynamic_cast<cOvalFigure *>(group->getFigure(0))) {
        halo->setLineColor(cFigure::Color("green"));
        halo->setFillColor(cFigure::Color("green"));
    }
}

void MainNodeApp::removeTaskFigure(int taskId) {
    auto it = taskFigures.find(taskId);
    if (it == taskFigures.end()) return;
    cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();
    canvas->removeFigure(it->second);
    delete it->second;
    taskFigures.erase(it);
    refreshStatsPanel();
}

// -----------------------------------------------------------------------
// HUD (top-left of the network canvas, updates every state change)
// -----------------------------------------------------------------------
void MainNodeApp::createStatsPanel() {
    cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();

    // Background panel (top-left of the canvas)
    auto bg = new cRectangleFigure("statsBg");
    bg->setBounds(cFigure::Rectangle(10, 10, 420, 220));
    bg->setLineColor(cFigure::Color("black"));
    bg->setLineWidth(2);
    bg->setFilled(true);
    bg->setFillColor(cFigure::Color("white"));
    bg->setFillOpacity(0.9);
    canvas->addFigure(bg);

    // Text on top
    statsPanel = new cTextFigure("statsText");
    statsPanel->setPosition(cFigure::Point(25, 25));
    statsPanel->setAnchor(cFigure::ANCHOR_NW);
    statsPanel->setColor(cFigure::Color("black"));
    statsPanel->setFont(cFigure::Font("", 18, cFigure::FONT_BOLD));
    canvas->addFigure(statsPanel);
}

void MainNodeApp::refreshStatsPanel() {
    if (statsPanel == nullptr) return;
    int queued = taskQueue.getLength();
    int active = (int)activeTaskDroneCount.size();
    int generated = taskCounter;
    int taskLimit = par("taskLimit").intValue();

    std::string s = "== Task Queue ==\n";
    s += "Generated : " + std::to_string(generated) + " / " + std::to_string(taskLimit) + "\n";
    s += "Queued    : " + std::to_string(queued) + "\n";
    s += "In flight : " + std::to_string(active) + "\n";
    s += "Completed : " + std::to_string(tasksCompleted) + "\n";
    s += "Dropped   : " + std::to_string(tasksDropped);
    statsPanel->setText(s.c_str());
}

// -----------------------------------------------------------------------
// CSV per-task logging
// -----------------------------------------------------------------------
// One row per finalized task. The `outcome` column takes one of:
//   COMPLETED  -- every assigned drone successfully completed its share
//   DROPPED    -- task aborted in flight (last drone bailed, COST chose
//                 to sacrifice for a higher-priority task, or MBS
//                 RELOCATE-AND-DROP picked it as collateral)
//   EXPIRED    -- queue-wait deadline (taskDeadlineWeights) fired before
//                 dispatch; the task never left the queue
//   UNFINISHED -- emitted in finish() for tasks still in flight at
//                 sim-time-limit (dispatched but not completed)
//   QUEUED     -- emitted in finish() for tasks that were generated but
//                 never dispatched (only possible when deadlines are off)
// The plotter at sweep_plotter.py groups everything that isn't COMPLETED
// as a drop (drop_rate), and additionally exposes expired_rate and
// preempt_drop_rate to distinguish SLA misses from preemption losses.
void MainNodeApp::openCsv() {
    // Path comes from the NED/ini param `csvOutputPath`. Supported
    // placeholders (expanded once, here at sim start):
    //   {configname}  -- active OMNeT++ config name (e.g. "General")
    //   {runnumber}   -- active run number (0 for a single-run config)
    //   {algorithm}   -- value of algorithmType param (e.g. "FIFO", "COST")
    // Empty string disables logging entirely. Braces (no '$') intentionally,
    // so the OMNeT++ ini parser doesn't try to treat these as iteration vars.
    cConfigurationEx *cfg = getEnvir()->getConfigEx();
    std::string configName = cfg->getActiveConfigName();
    int runNumber = cfg->getActiveRunNumber();
    std::string algo = par("algorithmType").stringValue();

    std::string path = par("csvOutputPath").stringValue();
    if (path.empty()) {
        EV << "csvOutputPath is empty -- per-task CSV logging is disabled.\n";
        return;
    }

    auto replaceAll = [](std::string& s, const std::string& from,
                         const std::string& to) {
        if (from.empty()) return;
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replaceAll(path, "{configname}", configName);
    replaceAll(path, "{runnumber}",  std::to_string(runNumber));
    replaceAll(path, "{algorithm}",  algo);

    // Create any parent directories the user named (idempotent).
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        std::string dir = path.substr(0, slash);
        std::string cmd = "mkdir -p '" + dir + "'";
        std::system(cmd.c_str());
    }

    csvOut.open(path);
    if (!csvOut.is_open()) {
        EV_WARN << "Could not open CSV file " << path << "\n";
        return;
    }
    EV << "Per-task CSV log: " << path << "\n";

    csvOut << "run,algorithm,taskId,priority,requiredDrones,dronesAssigned,"
              "mbsId,targetX,targetY,duration,generatedAt,dispatchedAt,"
              "finalizedAt,waitTime,turnaroundTime,dropEvents,outcome\n";
}

void MainNodeApp::writeCsvRow(const TaskRecord& r, const std::string& outcome) {
    if (!csvOut.is_open()) return;

    cConfigurationEx *cfg = getEnvir()->getConfigEx();
    int runNumber = cfg->getActiveRunNumber();
    std::string algo = par("algorithmType").stringValue();
    simtime_t now = simTime();

    // waitTime    = dispatch - generation (queue latency)
    // turnaround  = finalize - generation (total time in system)
    double waitTime = (r.dispatchedAt >= SIMTIME_ZERO)
                        ? (r.dispatchedAt - r.generatedAt).dbl()
                        : -1.0;
    double turnaround = (now - r.generatedAt).dbl();

    csvOut << runNumber << ","
           << algo << ","
           << r.taskId << ","
           << r.priority << ","
           << r.requiredDrones << ","
           << r.dronesAssigned << ","
           << r.mbsId << ","
           << r.targetX << ","
           << r.targetY << ","
           << r.duration << ","
           << r.generatedAt.dbl() << ","
           << (r.dispatchedAt >= SIMTIME_ZERO ? r.dispatchedAt.dbl() : -1.0) << ","
           << now.dbl() << ","
           << waitTime << ","
           << turnaround << ","
           << r.dropEvents << ","
           << outcome << "\n";
}

void MainNodeApp::finish() {
    // Emit a row for any task that was still in-flight when the sim ended,
    // so the CSV gives a complete picture of every generated task.
    for (auto& kv : taskRecords) {
        std::string outcome = (kv.second.dispatchedAt >= SIMTIME_ZERO)
                                ? "UNFINISHED" : "QUEUED";
        writeCsvRow(kv.second, outcome);
    }
    taskRecords.clear();

    if (csvOut.is_open()) {
        csvOut.flush();
        csvOut.close();
    }

    // Also record summary scalars so opp_scavetool / IDE plots can use them.
    recordScalar("tasksGenerated", taskCounter);
    recordScalar("tasksCompleted", tasksCompleted);
    recordScalar("tasksDropped", tasksDropped);
}

} // namespace uavswarmta
