// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <QStringList>
#include <QTableWidget>
#include "common/fs/path_util.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/settings.h"
#include "configure_debug_record.h"
#include "core/core.h"
#include "ui_configure_debug_record.h"
#include "video_core/gpu.h"
#include "yuzu/debugger/console.h"
#include "yuzu/uisettings.h"

#pragma optimize("", off)
enum class Columns : u32 {
    TIME = 0,
    ENGINE,
    REG,
    METHOD,
    ARGUMENT,
    COUNT,
};

ConfigureDebugRecord::ConfigureDebugRecord(QWidget* parent)
    : QDialog(parent),
      ui(new Ui::ConfigureDebugRecord), system{Core::System::GetInstance()}, resultsTimer{
                                                                                 new QTimer(this)} {
    ui->setupUi(this);

    ui->do_capture->setEnabled(true);
    ui->send_to_console->setEnabled(true);

    ui->table_record_draw_state->setColumnCount(static_cast<s32>(Columns::COUNT));
    ui->table_record_draw_state->setHorizontalHeaderLabels(QStringList(
        {QString::fromLatin1("Time"), QString::fromLatin1("Engine"), QString::fromLatin1("Reg"),
         QString::fromLatin1("Method"), QString::fromLatin1("Argument")}));
    ui->table_record_draw_state->verticalHeader()->setVisible(false);

    ui->table_record_pre_state->setColumnCount(static_cast<s32>(Columns::COUNT));
    ui->table_record_pre_state->setHorizontalHeaderLabels(QStringList(
        {QString::fromLatin1("Time"), QString::fromLatin1("Engine"), QString::fromLatin1("Reg"),
         QString::fromLatin1("Method"), QString::fromLatin1("Argument")}));
    ui->table_record_pre_state->verticalHeader()->setVisible(false);
    ui->table_record_pre_state->hideColumn(static_cast<s32>(Columns::TIME));

    ResizeColumns();

    connect(ui->list_record_draws, &QListWidget::currentRowChanged, this,
            &ConfigureDebugRecord::DrawIndexChanged);

    resultsTimer->setSingleShot(false);
    resultsTimer->setInterval(std::chrono::milliseconds(16));

    connect(resultsTimer, &QTimer::timeout, [this]() {
        if (!system.IsPoweredOn() ||
            (!Settings::values.pending_frame_record && !system.GPU().CURRENTLY_RECORDING)) {
            resultsTimer->stop();
            BuildResults();
        }
    });

    connect(ui->do_capture, &QPushButton::clicked, [this]() {
        if (system.IsPoweredOn()) {
            ui->do_capture->setEnabled(false);
            ui->send_to_console->setEnabled(false);
            Settings::values.pending_frame_record = true;
            resultsTimer->start();
        }
    });

    connect(ui->send_to_console, &QPushButton::clicked, [this]() { Print(); });
    connect(ui->lineEdit_filter, &QLineEdit::textEdited, this,
            &ConfigureDebugRecord::OnFilterChanged);
}

ConfigureDebugRecord::~ConfigureDebugRecord() = default;

void ConfigureDebugRecord::OnFilterChanged(const QString& new_text) {
    const auto& gpu = system.GPU();
    const auto currentDraw = ui->list_record_draws->currentRow();
    s32 i = 0;
    for (const auto& result : gpu.RECORD_RESULTS_CHANGED) {
        if (static_cast<s32>(result.draw) < currentDraw) {
            continue;
        } else if (static_cast<s32>(result.draw) > currentDraw) {
            break;
        }
        for (const auto& arg : result.args) {
            const QString text{QString::fromStdString(arg.first)};
            if (!new_text.isEmpty() && !text.contains(new_text)) {
                ui->table_record_draw_state->hideRow(i);
            }
            ++i;
        }
    }

    i = 0;
    for (const auto& result : gpu.RECORD_RESULTS_UNCHANGED) {
        for (const auto& arg : result.args) {
            const QString text{QString::fromStdString(arg.first)};
            if (!new_text.isEmpty() && !text.contains(new_text)) {
                ui->table_record_pre_state->hideRow(i);
            }
            ++i;
        }
    }

    ui->table_record_draw_state->scrollToTop();
    ui->table_record_pre_state->scrollToTop();
    ResizeColumns();
}

void ConfigureDebugRecord::DrawIndexChanged(int currentRow) {
    if (currentRow == -1) {
        return;
    }

    const auto& gpu = system.GPU();
    s32 i = 0;
    for (const auto& result : gpu.RECORD_RESULTS_CHANGED) {
        for (const auto& arg : result.args) {
            if (result.draw == currentRow) {
                ui->table_record_draw_state->showRow(i);
            } else {
                ui->table_record_draw_state->hideRow(i);
            }
            ++i;
        }
    }

    OnFilterChanged(ui->lineEdit_filter->text());
    ui->table_record_draw_state->scrollToTop();
    ResizeColumns();
}

void ConfigureDebugRecord::ResizeColumns() {
    ui->table_record_draw_state->resizeColumnsToContents();
    ui->table_record_pre_state->resizeColumnsToContents();

    s32 width = 0;
    for (s32 i = 0; i < ui->table_record_draw_state->columnCount(); ++i) {
        if (ui->table_record_draw_state->isColumnHidden(i) ||
            i == static_cast<s32>(Columns::METHOD)) {
            continue;
        }
        width += ui->table_record_draw_state->columnWidth(i);
    }
    ui->table_record_draw_state->setColumnWidth(static_cast<s32>(Columns::METHOD),
                                                ui->table_record_draw_state->width() - width - 15);

    width = 0;
    for (s32 i = 0; i < ui->table_record_pre_state->columnCount(); ++i) {
        if (ui->table_record_pre_state->isColumnHidden(i) ||
            i == static_cast<s32>(Columns::METHOD)) {
            continue;
        }
        width += ui->table_record_pre_state->columnWidth(i);
    }
    ui->table_record_pre_state->setColumnWidth(static_cast<s32>(Columns::METHOD),
                                               ui->table_record_pre_state->width() - width - 15);
}

void ConfigureDebugRecord::ClearResults() {
    ui->list_record_draws->clear();
    ui->table_record_draw_state->clearContents();
    ui->table_record_pre_state->clearContents();
}

void ConfigureDebugRecord::BuildResults() {
    const Tegra::GPU& gpu = system.GPU();

    ClearResults();

    u32 total_row_count = 0;
    for (const auto& result : gpu.RECORD_RESULTS_CHANGED) {
        for (const auto& arg : result.args) {
            ++total_row_count;
        }
    }
    ui->table_record_draw_state->setRowCount(total_row_count);

    total_row_count = 0;
    for (const auto& result : gpu.RECORD_RESULTS_UNCHANGED) {
        for (const auto& arg : result.args) {
            ++total_row_count;
        }
    }
    ui->table_record_pre_state->setRowCount(total_row_count);

    u32 lastDraw = -1;
    u32 i = 0;
    for (const auto& result : gpu.RECORD_RESULTS_CHANGED) {
        for (const auto& arg : result.args) {
            // Build out the list of args
            ui->table_record_draw_state->setItem(i, static_cast<s32>(Columns::TIME),
                                                 new QTableWidgetItem(QString::fromStdString(
                                                     fmt::format("{}", result.time.count()))));
            ui->table_record_draw_state->setItem(
                i, static_cast<s32>(Columns::ENGINE),
                new QTableWidgetItem(QString::fromStdString(result.engineName)));
            ui->table_record_draw_state->setItem(i, static_cast<s32>(Columns::REG),
                                                 new QTableWidgetItem(QString::fromStdString(
                                                     fmt::format("0x{:04X}", result.method))));
            ui->table_record_draw_state->setItem(
                i, static_cast<s32>(Columns::METHOD),
                new QTableWidgetItem(QString::fromStdString(arg.first)));
            ui->table_record_draw_state->setItem(
                i, static_cast<s32>(Columns::ARGUMENT),
                new QTableWidgetItem(QString::fromStdString(arg.second)));
            ++i;
        }

        // Build out the list of draws
        if (lastDraw != result.draw) {
            ui->list_record_draws->addItem(
                new QListWidgetItem(QString::fromStdString(fmt::format("Draw {}", result.draw))));
            lastDraw = result.draw;
        }
    }

    i = 0;
    for (const auto& result : gpu.RECORD_RESULTS_UNCHANGED) {
        for (const auto& arg : result.args) {
            // Build out the list of args
            ui->table_record_pre_state->setItem(
                i, static_cast<s32>(Columns::ENGINE),
                new QTableWidgetItem(QString::fromStdString(result.engineName)));
            ui->table_record_pre_state->setItem(i, static_cast<s32>(Columns::REG),
                                                new QTableWidgetItem(QString::fromStdString(
                                                    fmt::format("0x{:04X}", result.method))));
            ui->table_record_pre_state->setItem(
                i, static_cast<s32>(Columns::METHOD),
                new QTableWidgetItem(QString::fromStdString(arg.first)));
            ui->table_record_pre_state->setItem(
                i, static_cast<s32>(Columns::ARGUMENT),
                new QTableWidgetItem(QString::fromStdString(arg.second)));
            ++i;
        }
    }

    ui->list_record_draws->setCurrentRow(0);
    ResizeColumns();

    ui->do_capture->setEnabled(true);
    ui->send_to_console->setEnabled(true);

    ui->list_record_draws->update();
    ui->table_record_draw_state->update();
    ui->table_record_pre_state->update();
}

void ConfigureDebugRecord::Print() {
    const auto& gpu = system.GetInstance().GPU();
    std::string toPrint;
    toPrint.reserve(0x2000);

    toPrint += "\n\n====================\n======PRE STATE=====\n====================\n";

    for (auto& entry : gpu.RECORD_RESULTS_UNCHANGED) {
        size_t line_width = toPrint.size();
        toPrint += fmt::format("    {} (0x{:04X}) ", entry.engineName, entry.method);
        line_width = toPrint.size() - line_width;
        size_t i = 0;
        for (const auto& [name, arg] : entry.args) {
            if (i > 0) {
                toPrint += std::string(line_width, ' ');
            }
            toPrint += fmt::format("  {} = {}\n", name, arg);
            ++i;
        }
    }

    toPrint += "\n\n====================\n======= DRAWS ======\n====================\n";

    size_t lastDraw = -1;
    for (auto& entry : gpu.RECORD_RESULTS_CHANGED) {
        if (lastDraw != entry.draw) {
            lastDraw = entry.draw;
            toPrint += fmt::format("\n\nDraw {}\n", entry.draw);
        }
        size_t line_width = toPrint.size();
        toPrint += fmt::format("    {:4} {} (0x{:04X}) ", entry.time.count(), entry.engineName,
                               entry.method);
        line_width = toPrint.size() - line_width;
        size_t i = 0;
        for (const auto& [name, arg] : entry.args) {
            if (i > 0) {
                toPrint += std::string(line_width, ' ');
            }
            toPrint += fmt::format("  {} = {}\n", name, arg);
            ++i;
        }
    }

    LOG_INFO(Render_OpenGL, "{}", toPrint);
}

#pragma optimize("", on)
