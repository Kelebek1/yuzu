// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <QStandardItemModel>
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

ConfigureDebugRecord::ConfigureDebugRecord(QWidget* parent)
    : QDialog(parent),
      ui(new Ui::ConfigureDebugRecord), system{Core::System::GetInstance()}, resultsTimer{
                                                                                 new QTimer(this)} {
    ui->setupUi(this);

    ui->do_capture->setEnabled(true);
    ui->send_to_console->setEnabled(true);

    QStandardItemModel* drawModel = new QStandardItemModel();
    ui->table_record_draw_state->setModel(drawModel);
    drawModel->insertColumns(0, static_cast<s32>(Columns::COUNT));
    drawModel->setHorizontalHeaderLabels(QStringList(
        {QString::fromLatin1("Time"), QString::fromLatin1("Engine"), QString::fromLatin1("Reg"),
         QString::fromLatin1("Method"), QString::fromLatin1("Argument")}));
    ui->table_record_draw_state->verticalHeader()->setVisible(false);
    ui->table_record_draw_state->horizontalHeader()->setStretchLastSection(false);
    ui->table_record_draw_state->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    ui->table_record_draw_state->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeMode::Fixed);
    ui->table_record_draw_state->verticalHeader()->setSectionResizeMode(
        QHeaderView::ResizeMode::Fixed);

    QStandardItemModel* preModel = new QStandardItemModel();
    ui->table_record_pre_state->setModel(preModel);
    preModel->insertColumns(0, static_cast<s32>(Columns::COUNT));
    preModel->setHorizontalHeaderLabels(QStringList(
        {QString::fromLatin1("Time"), QString::fromLatin1("Engine"), QString::fromLatin1("Reg"),
         QString::fromLatin1("Method"), QString::fromLatin1("Argument")}));
    ui->table_record_pre_state->verticalHeader()->setVisible(false);
    ui->table_record_pre_state->hideColumn(static_cast<s32>(Columns::TIME));
    ui->table_record_pre_state->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    ui->table_record_pre_state->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeMode::Fixed);
    ui->table_record_pre_state->verticalHeader()->setSectionResizeMode(
        QHeaderView::ResizeMode::Fixed);

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

void ConfigureDebugRecord::HideFilterColumns(
    const std::array<QStringList, static_cast<s32>(Columns::COUNT)>& filters) {
    const auto& gpu = system.GPU();
    const auto currentDraw = ui->list_record_draws->currentRow();
    QStandardItemModel* drawModel =
        static_cast<QStandardItemModel*>(ui->table_record_draw_state->model());
    QStandardItemModel* preModel =
        static_cast<QStandardItemModel*>(ui->table_record_pre_state->model());

    for (u32 i = draw_indexes[currentDraw]; i < draw_indexes[currentDraw + 1]; ++i) {
        std::array<bool, static_cast<s32>(Columns::COUNT)> showThisRow{};
        for (size_t col = 0; col < filters.size(); ++col) {
            const auto item = drawModel->item(i, static_cast<s32>(col));
            for (auto& filter : filters[col]) {
                if (item->text().contains(filter)) {
                    showThisRow[col] = true;
                }
            }
        }

        bool shouldShow =
            std::any_of(showThisRow.begin(), showThisRow.end(), [](bool in) { return in; });
        if (!shouldShow) {
            ui->table_record_draw_state->hideRow(i);
        }
    }

    for (s32 i = 0; i < preModel->rowCount(); ++i) {
        std::array<bool, static_cast<s32>(Columns::COUNT)> showThisRow{};
        for (size_t col = 0; col < filters.size(); ++col) {
            const auto item = preModel->item(i, static_cast<s32>(col));
            for (auto& filter : filters[col]) {
                if (item->text().contains(filter)) {
                    showThisRow[col] = true;
                }
            }
        }

        bool shouldShow =
            std::any_of(showThisRow.begin(), showThisRow.end(), [](bool in) { return in; });
        if (!shouldShow) {
            ui->table_record_pre_state->hideRow(i);
        }
    }
}

void ConfigureDebugRecord::UpdateViews(const QString& new_text) {
    const auto filters = ParseFilters(new_text);
    const bool activeFilter = std::any_of(filters.begin(), filters.end(),
                                          [](const QStringList& col) { return col.size() > 0; });
    if (activeFilter) {
        HideFilterColumns(filters);
    }

    ui->table_record_draw_state->scrollToTop();
    ui->table_record_pre_state->scrollToTop();
    ResizeColumns();
    ui->table_record_draw_state->update();
    ui->table_record_pre_state->update();
}

std::array<QStringList, static_cast<s32>(Columns::COUNT)> ConfigureDebugRecord::ParseFilters(
    const QString& new_text) {
    std::array<QStringList, static_cast<s32>(Columns::COUNT)> filters;

    if (new_text.isEmpty()) {
        return filters;
    }

    auto in_filters = new_text.split(QString::fromLatin1(" "));

    for (auto& in_filter : in_filters) {
        if (in_filter.isEmpty()) {
            continue;
        }
        if (in_filter.contains(QString::fromLatin1(":"))) {
            QStringList a = in_filter.split(QString::fromLatin1(":"));
            if (a[0].contains(QString::fromLatin1("time"))) {
                filters[static_cast<s32>(Columns::TIME)].push_back(a[1]);
            } else if (a[0].contains(QString::fromLatin1("eng"))) {
                filters[static_cast<s32>(Columns::ENGINE)].push_back(a[1]);
            } else if (a[0].contains(QString::fromLatin1("reg"))) {
                filters[static_cast<s32>(Columns::REG)].push_back(a[1]);
            } else if (a[0].contains(QString::fromLatin1("meth"))) {
                filters[static_cast<s32>(Columns::METHOD)].push_back(a[1]);
            } else if (a[0].contains(QString::fromLatin1("arg"))) {
                filters[static_cast<s32>(Columns::ARGUMENT)].push_back(a[1]);
            }
        } else {
            filters[static_cast<s32>(Columns::METHOD)].push_back(in_filter);
        }
    }
    return filters;
}

void ConfigureDebugRecord::OnFilterChanged(const QString& new_text) {
    HideAllRows();
    ShowRows(ui->list_record_draws->currentRow());
    UpdateViews(new_text);
}

void ConfigureDebugRecord::ShowRows(s32 draw = -1) {
    const auto& gpu = system.GPU();

    s32 i = 0;
    for (const auto& result : gpu.RECORD_RESULTS_CHANGED) {
        for (const auto& arg : result.args) {
            if (draw == -1 || result.draw == draw) {
                ui->table_record_draw_state->showRow(i);
            }
            ++i;
        }
    }

    for (s32 j = 0; j < i; ++j) {
        ui->table_record_pre_state->showRow(j);
    }
}

void ConfigureDebugRecord::HideAllRows() {
    const auto& gpu = system.GPU();

    s32 i = 0;
    for (const auto& result : gpu.RECORD_RESULTS_CHANGED) {
        for (const auto& arg : result.args) {
            ui->table_record_draw_state->hideRow(i);
            ++i;
        }
    }
}

void ConfigureDebugRecord::DrawIndexChanged(s32 currentRow) {
    if (currentRow == -1) {
        return;
    }

    QStandardItemModel* drawModel =
        static_cast<QStandardItemModel*>(ui->table_record_draw_state->model());
    auto newDrawIdx = draw_indexes[currentRow];
    if (!drawModel->item(newDrawIdx, static_cast<s32>(Columns::TIME))) {
        FillDrawIndex(currentRow);
    }

    HideAllRows();
    ShowRows(currentRow);

    UpdateViews(ui->lineEdit_filter->text());
}

void ConfigureDebugRecord::ResizeColumns() {
    QStandardItemModel* drawModel =
        static_cast<QStandardItemModel*>(ui->table_record_draw_state->model());
    QStandardItemModel* preModel =
        static_cast<QStandardItemModel*>(ui->table_record_pre_state->model());

    ui->table_record_draw_state->resizeColumnsToContents();
    ui->table_record_pre_state->resizeColumnsToContents();

    ui->table_record_draw_state->setColumnWidth(
        static_cast<s32>(Columns::ARGUMENT),
        ui->table_record_draw_state->columnWidth(static_cast<s32>(Columns::ARGUMENT)) + 15);
    ui->table_record_pre_state->setColumnWidth(
        static_cast<s32>(Columns::ARGUMENT),
        ui->table_record_pre_state->columnWidth(static_cast<s32>(Columns::ARGUMENT)) + 15);

    s32 width = 0;
    for (s32 i = 0; i < drawModel->columnCount(); ++i) {
        if (ui->table_record_draw_state->isColumnHidden(i) ||
            i == static_cast<s32>(Columns::METHOD)) {
            continue;
        }
        width += ui->table_record_draw_state->columnWidth(i);
    }
    ui->table_record_draw_state->setColumnWidth(static_cast<s32>(Columns::METHOD),
                                                ui->table_record_draw_state->width() - width - 20);

    width = 0;
    for (s32 i = 0; i < preModel->columnCount(); ++i) {
        if (ui->table_record_pre_state->isColumnHidden(i) ||
            i == static_cast<s32>(Columns::METHOD)) {
            continue;
        }
        width += ui->table_record_pre_state->columnWidth(i);
    }
    ui->table_record_pre_state->setColumnWidth(static_cast<s32>(Columns::METHOD),
                                               ui->table_record_pre_state->width() - width - 20);
}

void ConfigureDebugRecord::ClearResults() {
    ui->list_record_draws->clear();

    QStandardItemModel* drawModel =
        static_cast<QStandardItemModel*>(ui->table_record_draw_state->model());
    QStandardItemModel* preModel =
        static_cast<QStandardItemModel*>(ui->table_record_pre_state->model());
    ui->table_record_draw_state->setModel(nullptr);
    ui->table_record_pre_state->setModel(nullptr);
    drawModel->removeRows(0, drawModel->rowCount());
    preModel->removeRows(0, preModel->rowCount());
    ui->table_record_draw_state->setModel(drawModel);
    ui->table_record_pre_state->setModel(preModel);
    results_indexes.clear();
    draw_indexes.clear();
}

void ConfigureDebugRecord::BuildResults() {
    const Tegra::GPU& gpu = system.GPU();

    ClearResults();

    QStandardItemModel* drawModel =
        static_cast<QStandardItemModel*>(ui->table_record_draw_state->model());
    QStandardItemModel* preModel =
        static_cast<QStandardItemModel*>(ui->table_record_pre_state->model());

    u32 total_row_count = 0;
    u32 idx = 0;
    s32 lastDraw = -1;
    for (const auto& result : gpu.RECORD_RESULTS_CHANGED) {
        if (lastDraw != result.draw) {
            results_indexes.push_back(idx);
            draw_indexes.push_back(total_row_count);
            lastDraw = result.draw;
        }
        for (const auto& arg : result.args) {
            ++total_row_count;
        }
        ++idx;
    }
    results_indexes.push_back(idx);
    draw_indexes.push_back(total_row_count);
    drawModel->setRowCount(total_row_count);

    total_row_count = 0;
    for (const auto& result : gpu.RECORD_RESULTS_UNCHANGED) {
        for (const auto& arg : result.args) {
            ++total_row_count;
        }
    }
    preModel->setRowCount(total_row_count);

    // Build out the list of draws
    for (u32 i = 0; i < draw_indexes.size() - 1; ++i) {
        ui->list_record_draws->addItem(
            new QListWidgetItem(QString::fromStdString(fmt::format("Draw {}", i))));
    }

    ui->table_record_pre_state->setModel(nullptr);
    u32 i = 0;
    for (const auto& result : gpu.RECORD_RESULTS_UNCHANGED) {
        for (const auto& arg : result.args) {
            // Build out the list of args
            preModel->setItem(i, static_cast<s32>(Columns::ENGINE),
                              new QStandardItem(QString::fromStdString(result.engineName)));
            preModel->setItem(
                i, static_cast<s32>(Columns::REG),
                new QStandardItem(QString::fromStdString(fmt::format("0x{:04X}", result.method))));
            preModel->setItem(i, static_cast<s32>(Columns::METHOD),
                              new QStandardItem(QString::fromStdString(arg.first)));
            preModel->setItem(i, static_cast<s32>(Columns::ARGUMENT),
                              new QStandardItem(QString::fromStdString(arg.second)));
            ++i;
        }
    }
    ui->table_record_pre_state->setModel(preModel);
    ui->table_record_pre_state->hideColumn(static_cast<s32>(Columns::TIME));

    ui->list_record_draws->setCurrentRow(0);
    FillDrawIndex(0);
    DrawIndexChanged(0);

    ui->do_capture->setEnabled(true);
    ui->send_to_console->setEnabled(true);
}

void ConfigureDebugRecord::FillDrawIndex(u32 idx) {
    auto& gpu = system.GPU();

    QStandardItemModel* drawModel =
        static_cast<QStandardItemModel*>(ui->table_record_draw_state->model());
    ui->table_record_draw_state->setModel(nullptr);

    u32 draw_idx = draw_indexes.at(idx);
    u32 result_idx = results_indexes.at(idx);
    while (result_idx < results_indexes.at(idx + 1)) {
        const auto& result = gpu.RECORD_RESULTS_CHANGED[result_idx++];
        for (const auto& arg : result.args) {
            // Build out the list of args
            drawModel->setItem(
                draw_idx, static_cast<s32>(Columns::TIME),
                new QStandardItem(QString::fromStdString(fmt::format("{}", result.time.count()))));
            drawModel->setItem(draw_idx, static_cast<s32>(Columns::ENGINE),
                               new QStandardItem(QString::fromStdString(result.engineName)));
            drawModel->setItem(
                draw_idx, static_cast<s32>(Columns::REG),
                new QStandardItem(QString::fromStdString(fmt::format("0x{:04X}", result.method))));
            drawModel->setItem(draw_idx, static_cast<s32>(Columns::METHOD),
                               new QStandardItem(QString::fromStdString(arg.first)));
            drawModel->setItem(draw_idx, static_cast<s32>(Columns::ARGUMENT),
                               new QStandardItem(QString::fromStdString(arg.second)));
            ++draw_idx;
        }
    }

    ui->table_record_draw_state->setModel(drawModel);
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
