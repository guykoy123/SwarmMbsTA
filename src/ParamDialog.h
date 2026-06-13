/*
 * ParamDialog.h -- a small Qt-based form dialog used by MainNodeApp at sim
 * startup when ask_user=true. Lives in our project (no OMNeT++ source is
 * modified). Uses the Qt6 instance that Qtenv has already loaded into the
 * process; falls back gracefully under Cmdenv (see isQtGuiAvailable()).
 */
#ifndef UAVSWARMTA_PARAMDIALOG_H
#define UAVSWARMTA_PARAMDIALOG_H

#include <string>
#include <vector>

namespace uavswarmta {

struct ParamFieldSpec {
    std::string label;   // shown in the form, e.g. "taskGenerationInterval"
    std::string value;   // current value (default); overwritten on accept
    std::string hint;    // optional tooltip / "@unit(s)" hint, may be empty

    // If non-empty, the field is rendered as a QComboBox populated with
    // these options instead of a free-form QLineEdit. If `value` matches one
    // of the choices it becomes the initial selection; otherwise (or when
    // `editable` is true) the user can pick any choice OR type a custom
    // value.
    std::vector<std::string> choices;
    bool editable = false;
};

// True iff a QApplication is alive in this process (i.e. we are running
// under Qtenv, not Cmdenv). Cheap; safe to call before any Qt headers
// pulled into the caller.
bool isQtGuiAvailable();

// Show a modal form with one QLineEdit per entry. Returns:
//   true  -- user clicked OK; `fields[i].value` updated in-place.
//   false -- user cancelled OR Qt is not available.
// Caller must handle the false case (e.g. keep ini defaults).
bool showParamDialog(std::vector<ParamFieldSpec>& fields,
                     const std::string& title);

} // namespace uavswarmta

#endif // UAVSWARMTA_PARAMDIALOG_H
