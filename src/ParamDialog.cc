/*
 * ParamDialog.cc -- Qt6 form dialog for editing tunables at sim start.
 *
 * Design notes:
 *  - No Q_OBJECT subclassing: we only instantiate stock Qt classes and use
 *    the new-style connect() with stock signals. That means no moc step,
 *    so the project's Makefile only needs Qt's cflags+libs (see makefrag).
 *  - We never construct a QApplication ourselves; Qtenv already did.
 *    Under Cmdenv there is no QApplication, so isQtGuiAvailable() returns
 *    false and the caller falls back to a text prompt.
 */
#include "ParamDialog.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace uavswarmta {

bool isQtGuiAvailable() {
    // QApplication (not QCoreApplication) is required for widgets.
    return qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr;
}

bool showParamDialog(std::vector<ParamFieldSpec>& fields,
                     const std::string& title) {
    if (!isQtGuiAvailable()) return false;

    QDialog dlg;
    dlg.setWindowTitle(QString::fromStdString(title));
    dlg.setModal(true);

    auto* outer = new QVBoxLayout(&dlg);

    auto* intro = new QLabel(QStringLiteral(
        "Edit any tunable parameter, then press OK to start the simulation.\n"
        "Leave a value unchanged to keep the .ini default."));
    intro->setWordWrap(true);
    outer->addWidget(intro);

    // Form lives inside a scroll area so a long list still fits on small
    // displays. The form widget itself owns the QFormLayout.
    auto* formWidget = new QWidget();
    auto* form = new QFormLayout(formWidget);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    std::vector<QLineEdit*> edits(fields.size(), nullptr);
    std::vector<QComboBox*> combos(fields.size(), nullptr);
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& f = fields[i];
        QString labelText = QString::fromStdString(f.label);
        if (!f.hint.empty())
            labelText += QStringLiteral("  (") + QString::fromStdString(f.hint) + QStringLiteral(")");

        if (!f.choices.empty()) {
            // Dropdown. If editable, behaves like a combo with free text;
            // otherwise it's a strict choice list.
            auto* combo = new QComboBox();
            combo->setEditable(f.editable);
            combo->setMinimumWidth(280);
            int selectedIdx = -1;
            for (size_t k = 0; k < f.choices.size(); ++k) {
                combo->addItem(QString::fromStdString(f.choices[k]));
                if (f.choices[k] == f.value)
                    selectedIdx = static_cast<int>(k);
            }
            if (selectedIdx >= 0) {
                combo->setCurrentIndex(selectedIdx);
            } else if (f.editable) {
                // Current value isn't one of the presets, but combo is
                // editable: show it as a custom entry.
                combo->setEditText(QString::fromStdString(f.value));
            } else if (combo->count() > 0) {
                combo->setCurrentIndex(0);
            }
            if (!f.hint.empty()) combo->setToolTip(QString::fromStdString(f.hint));
            form->addRow(labelText, combo);
            combos[i] = combo;
        } else {
            auto* edit = new QLineEdit(QString::fromStdString(f.value));
            edit->setMinimumWidth(280);
            if (!f.hint.empty()) edit->setToolTip(QString::fromStdString(f.hint));
            form->addRow(labelText, edit);
            edits[i] = edit;
        }
    }

    auto* scroll = new QScrollArea();
    scroll->setWidget(formWidget);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll, /*stretch=*/1);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(btns, &QDialogButtonBox::accepted,
                     &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected,
                     &dlg, &QDialog::reject);
    outer->addWidget(btns);

    // Sensible default size: wide enough for "uniform(10s,30s)" style values,
    // tall enough for ~12 fields without scrolling.
    int rows = static_cast<int>(fields.size());
    int desiredHeight = 140 + 34 * rows;
    if (desiredHeight > 640) desiredHeight = 640;
    dlg.resize(620, desiredHeight);

    int result = dlg.exec();
    if (result != QDialog::Accepted) return false;

    for (size_t i = 0; i < fields.size(); ++i) {
        if (combos[i]) {
            fields[i].value = combos[i]->currentText().toStdString();
        } else if (edits[i]) {
            fields[i].value = edits[i]->text().toStdString();
        }
    }
    return true;
}

} // namespace uavswarmta
