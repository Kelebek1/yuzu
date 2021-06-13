// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <QCloseEvent>
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
#include "yuzu/debugger/console.h"
#include "yuzu/uisettings.h"

constexpr auto GetEngineIdx = [](std::string engine) -> s32 {
    if (engine.find("FERMI") != std::string::npos) {
        return 0;
    } else if (engine.find("MAXWELL") != std::string::npos) {
        return 1;
    } else if (engine.find("KEPLERC") != std::string::npos) {
        return 2;
    } else if (engine.find("KEPLERI") != std::string::npos) {
        return 3;
    } else if (engine.find("MAXDMA") != std::string::npos) {
        return 4;
    }
    return 1;
};

ConfigureDebugRecord::ConfigureDebugRecord(QWidget* parent)
    : QDialog(parent),
      ui(new Ui::ConfigureDebugRecord), system{Core::System::GetInstance()}, resultsTimer{
                                                                                 new QTimer(this)} {
    ui->setupUi(this);

    ui->do_capture->setEnabled(true);
    ui->send_to_console->setEnabled(true);
    ui->btn_StepFrame->setEnabled(false);
    ui->btn_StepFrame->setVisible(false);
    ui->btn_Run->setEnabled(false);
    ui->btnShowThumbnails->setEnabled(false);
    ui->btnHideThumbnails->setVisible(false);

    QStandardItemModel* listModel = new QStandardItemModel();
    ui->list_record_draws->setModel(listModel);
    ui->list_record_draws->setUniformRowHeights(true);

    thumbnail_frame = new ThumbnailWindow(this);

    draw_vertical_header = new QHeaderView(Qt::Orientation::Vertical, this);
    draw_vertical_header->setVisible(false);
    draw_vertical_header->setSectionResizeMode(QHeaderView::ResizeMode::Fixed);
    draw_vertical_header->setDefaultSectionSize(20);
    draw_vertical_header->setMinimumSectionSize(20);
    draw_horizontal_header = new QHeaderView(Qt::Orientation::Horizontal, this);
    draw_horizontal_header->setVisible(true);
    draw_horizontal_header->setStretchLastSection(false);
    draw_horizontal_header->setDefaultAlignment(Qt::AlignLeft);
    draw_horizontal_header->setSectionResizeMode(QHeaderView::ResizeMode::Fixed);

    pre_vertical_header = new QHeaderView(Qt::Orientation::Vertical, this);
    pre_vertical_header->setVisible(false);
    pre_vertical_header->setSectionResizeMode(QHeaderView::ResizeMode::Fixed);
    pre_vertical_header->setDefaultSectionSize(20);
    pre_vertical_header->setMinimumSectionSize(20);
    pre_horizontal_header = new QHeaderView(Qt::Orientation::Horizontal, this);
    pre_horizontal_header->setVisible(true);
    pre_horizontal_header->setStretchLastSection(false);
    pre_horizontal_header->setDefaultAlignment(Qt::AlignLeft);
    pre_horizontal_header->setSectionResizeMode(QHeaderView::ResizeMode::Fixed);

    connect(ui->list_record_draws, &QTreeView::clicked, this,
            &ConfigureDebugRecord::DrawIndexChanged);

    resultsTimer->setSingleShot(false);
    resultsTimer->setInterval(std::chrono::milliseconds(16));

    connect(resultsTimer, &QTimer::timeout, [this]() {
        if (!system.IsPoweredOn() ||
            (!Settings::values.pending_frame_record && !system.GPU().CURRENTLY_RECORDING)) {
            resultsTimer->stop();
            BuildResults();
            ui->btnShowThumbnails->setEnabled(true);
        }
    });

    connect(ui->do_capture, &QPushButton::clicked, [this]() {
        if (system.IsPoweredOn()) {
            ui->do_capture->setEnabled(false);
            ui->send_to_console->setEnabled(false);
            Settings::values.pending_frame_record = true;
            Settings::values.record_num_frames = ui->spin_numFrames->value();
            resultsTimer->start();
        }
    });

    connect(ui->send_to_console, &QPushButton::clicked, this, &ConfigureDebugRecord::Print);
    connect(ui->lineEdit_filter, &QLineEdit::textEdited, this,
            &ConfigureDebugRecord::OnFilterChanged);
    connect(ui->checkBox_hide_unk, &QCheckBox::stateChanged, this,
            &ConfigureDebugRecord::HideUnkStateChanged);
    connect(ui->btn_Pause, &QPushButton::clicked, this, &ConfigureDebugRecord::PauseClicked);
    connect(ui->btn_Run, &QPushButton::clicked, this, &ConfigureDebugRecord::RunClicked);
    connect(ui->btn_StepFrame, &QPushButton::clicked, this,
            &ConfigureDebugRecord::StepFrameClicked);
    connect(ui->btnShowThumbnails, &QPushButton::clicked, this, [this](s32 state) {
        // thumbnail_geometry = this->saveGeometry();
        ui->btnHideThumbnails->setVisible(true);
        ui->btnShowThumbnails->setVisible(false);
        // QRect currentGeo = this->geometry();
        // currentGeo.setHeight(currentGeo.height() + 370);
        // this->setGeometry(currentGeo);
        thumbnail_frame->show();
        ShowThumbnail(true);
    });
    connect(ui->btnHideThumbnails, &QPushButton::clicked, this, [this](s32 state) {
        // this->restoreGeometry(thumbnail_geometry);
        ui->btnHideThumbnails->setVisible(false);
        ui->btnShowThumbnails->setVisible(true);
        thumbnail_frame->hide();
    });
}

ConfigureDebugRecord::~ConfigureDebugRecord() = default;

void ConfigureDebugRecord::HideUnkStateChanged(s32 state) {
    HideAllRows();
    ShowRows();
    UpdateViews();
}

void ConfigureDebugRecord::PauseClicked(s32 state) {
    ui->btn_Pause->setEnabled(false);
    ui->btn_Pause->setVisible(false);
    ui->btn_StepFrame->setEnabled(true);
    ui->btn_StepFrame->setVisible(true);
    ui->btn_Run->setEnabled(true);
    const auto _ = system.Pause();
}

void ConfigureDebugRecord::RunClicked(s32 state) {
    ui->btn_Run->setEnabled(false);
    ui->btn_Pause->setEnabled(true);
    ui->btn_Pause->setVisible(true);
    ui->btn_StepFrame->setEnabled(false);
    ui->btn_StepFrame->setVisible(false);
    const auto _ = system.Run();
}

void ConfigureDebugRecord::StepFrameClicked(s32 state) {
    Settings::values.record_is_frame_stepping = true;
    const auto _ = system.Run();
}

void ConfigureDebugRecord::HideFilterColumns(
    const std::array<QStringList, static_cast<s32>(Columns::COUNT)>& filters) {

    ui->table_record_draw_state->blockSignals(true);
    ui->table_record_pre_state->blockSignals(true);

    auto& drawModel = draw_models[current_frame];
    auto& preModel = pre_models[current_frame];

    for (u32 i = draw_indexes[current_frame][current_draw];
         i < draw_indexes[current_frame][current_draw + 1]; ++i) {
        std::array<bool, static_cast<s32>(Columns::COUNT)> showThisRow{};
        for (size_t col = 0; col < filters.size(); ++col) {
            const auto item = drawModel->item(i, static_cast<s32>(col));
            for (auto& filter : filters[col]) {
                if (item->text().contains(filter, Qt::CaseInsensitive)) {
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
                if (item->text().contains(filter, Qt::CaseInsensitive)) {
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

    ui->table_record_draw_state->blockSignals(false);
    ui->table_record_pre_state->blockSignals(false);
}

void ConfigureDebugRecord::UpdateViews() {
    const auto filters = ParseFilters(ui->lineEdit_filter->text());
    const bool activeFilter = std::any_of(filters.begin(), filters.end(),
                                          [](const QStringList& col) { return col.size() > 0; });
    if (activeFilter) {
        HideFilterColumns(filters);
    }

    ui->table_record_draw_state->scrollToTop();
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

    auto in_filters = new_text.split(tr(" "));

    for (auto& in_filter : in_filters) {
        if (in_filter.isEmpty()) {
            continue;
        }
        if (in_filter.contains(tr(":"))) {
            QStringList a = in_filter.split(tr(":"));
            if (a[0].contains(tr("time"), Qt::CaseInsensitive)) {
                filters[static_cast<s32>(Columns::TIME)].push_back(a[1]);
            } else if (a[0].contains(tr("eng"), Qt::CaseInsensitive)) {
                filters[static_cast<s32>(Columns::ENGINE)].push_back(a[1]);
            } else if (a[0].contains(tr("reg"), Qt::CaseInsensitive)) {
                filters[static_cast<s32>(Columns::REG)].push_back(a[1]);
            } else if (a[0].contains(tr("meth"), Qt::CaseInsensitive)) {
                filters[static_cast<s32>(Columns::METHOD)].push_back(a[1]);
            } else if (a[0].contains(tr("arg"), Qt::CaseInsensitive)) {
                filters[static_cast<s32>(Columns::ARGUMENT)].push_back(a[1]);
            } else {
                filters[static_cast<s32>(Columns::METHOD)].push_back(in_filter);
            }
        } else {
            filters[static_cast<s32>(Columns::METHOD)].push_back(in_filter);
        }
    }
    return filters;
}

void ConfigureDebugRecord::OnFilterChanged(const QString& new_text) {
    HideAllRows();
    ShowRows();
    UpdateViews();
}

void ConfigureDebugRecord::FindAndSetPreRow(s32 row) {
    if (current_draw == 0) {
        return;
    }

    const auto lookingForEngine = pre_models[current_frame]
                                      ->item(row, static_cast<s32>(Columns::ENGINE))
                                      ->text()
                                      .toStdString();
    const auto lookingForReg = pre_models[current_frame]
                                   ->item(row, static_cast<s32>(Columns::REG))
                                   ->text()
                                   .toInt(nullptr, 16);
    const auto lookingForMethod = pre_models[current_frame]
                                      ->item(row, static_cast<s32>(Columns::METHOD))
                                      ->text()
                                      .toStdString();
    for (s32 draw = current_draw - 1; draw >= 0; --draw) {
        s32 result_index_start = results_changed_indexes[current_frame][draw + 1];
        s32 result_index_end = results_changed_indexes[current_frame][draw];
        for (s32 j = result_index_start; j > result_index_end; --j) {
            const auto& result = results_changed[current_frame][j];
            if (result.engineName != lookingForEngine || result.method != lookingForReg) {
                continue;
            }

            for (auto& [method, arg] : result.args) {
                if (method == lookingForMethod) {
                    pre_models[current_frame]
                        ->item(row, static_cast<s32>(Columns::ARGUMENT))
                        ->setText(QString::fromStdString(arg));
                    return;
                }
            }
        }
    }
}

void ConfigureDebugRecord::ShowRows() {
    ui->table_record_draw_state->blockSignals(true);
    ui->table_record_pre_state->blockSignals(true);

    std::unordered_set<std::string> encountered;

    for (u32 i = draw_indexes[current_frame][current_draw];
         i < draw_indexes[current_frame][current_draw + 1]; ++i) {
        const auto& text =
            draw_models[current_frame]->item(i, static_cast<s32>(Columns::METHOD))->text();
        if (ui->checkBox_hide_unk->isChecked() && text.contains(tr("unk_"))) {
            continue;
        }
        encountered.insert(text.toStdString());
        ui->table_record_draw_state->showRow(i);
    }

    for (s32 i = 0; i < pre_models[current_frame]->rowCount(); ++i) {
        const auto& text =
            pre_models[current_frame]->item(i, static_cast<s32>(Columns::METHOD))->text();
        if (ui->checkBox_hide_unk->isChecked() && text.contains(tr("unk_"))) {
            continue;
        }
        if (!encountered.contains(text.toStdString())) {
            FindAndSetPreRow(i);
            ui->table_record_pre_state->showRow(i);
        }
    }

    ui->table_record_draw_state->blockSignals(false);
    ui->table_record_pre_state->blockSignals(false);
}

void ConfigureDebugRecord::HideAllRows() {
    ui->table_record_draw_state->blockSignals(true);
    ui->table_record_pre_state->blockSignals(true);

    for (s32 i = 0; i < draw_models[current_frame]->rowCount(); ++i) {
        ui->table_record_draw_state->hideRow(i);
    }

    for (s32 i = 0; i < pre_models[current_frame]->rowCount(); ++i) {
        ui->table_record_pre_state->hideRow(i);
    }

    ui->table_record_draw_state->blockSignals(false);
    ui->table_record_pre_state->blockSignals(false);
}

void ConfigureDebugRecord::DrawIndexChanged(const QModelIndex& new_index) {
    QStandardItemModel* listModel =
        static_cast<QStandardItemModel*>(ui->list_record_draws->model());

    u32 new_frame;
    u32 new_draw;
    if (!listModel->itemFromIndex(new_index)->parent()) {
        // is a Frame, not a draw
        new_frame = new_index.row();
        new_draw = listModel->itemFromIndex(new_index)->child(0)->row();
    } else {
        new_frame = listModel->itemFromIndex(new_index)->parent()->row();
        new_draw = listModel->itemFromIndex(new_index)->row();
    }

    current_frame = new_frame;
    current_draw = new_draw;

    ui->table_record_draw_state->setModel(draw_models[new_frame]);
    ui->table_record_pre_state->setModel(pre_models[new_frame]);

    auto newDrawIdx = draw_indexes[new_frame][new_draw];
    if (!draw_models[new_frame]->item(newDrawIdx, static_cast<s32>(Columns::METHOD))) {
        FillDrawIndex(new_frame, new_draw);
    }

    // Pre regs are depending on draws already being filled
    if (!pre_models[new_frame]->item(0, static_cast<s32>(Columns::METHOD))) {
        FillPreFrame(new_frame);
    }

    HideAllRows();
    ShowRows();
    UpdateViews();

    ShowThumbnail();
}

void ConfigureDebugRecord::ResizeColumns() {
    ui->table_record_draw_state->blockSignals(true);
    ui->table_record_pre_state->blockSignals(true);

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

    ui->table_record_draw_state->blockSignals(false);
    ui->table_record_pre_state->blockSignals(false);
}

void ConfigureDebugRecord::ClearResults() {
    QStandardItemModel* listModel =
        static_cast<QStandardItemModel*>(ui->list_record_draws->model());
    listModel->clear();

    ui->table_record_draw_state->setModel(nullptr);
    for (auto& drawModel : draw_models) {
        drawModel->clear();
        delete drawModel;
    }

    ui->table_record_pre_state->setModel(nullptr);
    for (auto& preModel : pre_models) {
        preModel->clear();
        delete preModel;
    }

    draw_models.clear();
    pre_models.clear();
    results_changed_indexes.clear();
    results_unchanged_indexes.clear();
    draw_indexes.clear();
    pre_indexes.clear();
    results_changed.clear();
    results_unchanged.clear();
    results_frames.clear();
    for (auto thumbnail : results_thumbnails) {
        delete[] thumbnail.data;
    }
    results_thumbnails.clear();
}

void ConfigureDebugRecord::BuildResults() {
    const Tegra::GPU& gpu = system.GPU();

    ClearResults();

    results_changed = std::move(gpu.RECORD_RESULTS_CHANGED);
    results_unchanged = std::move(gpu.RECORD_RESULTS_UNCHANGED);
    results_frames = std::move(gpu.RECORDED_FRAMES);
    results_thumbnails = std::move(gpu.RECORD_THUMBNAILS);

    results_changed_indexes.resize(Settings::values.record_num_frames);
    draw_indexes.resize(Settings::values.record_num_frames);

    u32 frame_num = 0;
    for (auto& frame : results_changed) {
        u32 total_row_count = 0;
        u32 idx = 0;
        s32 lastDraw = -1;
        for (const auto& result : frame) {
            if (lastDraw != result.draw) {
                results_changed_indexes[frame_num].push_back(idx);
                draw_indexes[frame_num].push_back(total_row_count);
                lastDraw = result.draw;
            }
            for (const auto& arg : result.args) {
                ++total_row_count;
            }
            ++idx;
        }
        results_changed_indexes[frame_num].push_back(idx);
        draw_indexes[frame_num].push_back(total_row_count);

        QStandardItemModel* drawModel = new QStandardItemModel;
        drawModel->insertColumns(0, static_cast<s32>(Columns::COUNT));
        drawModel->setHorizontalHeaderLabels(
            QStringList({tr("Time"), tr("Engine"), tr("Reg"), tr("Method"), tr("Argument")}));
        drawModel->setRowCount(total_row_count);

        draw_models.push_back(drawModel);
        frame_num++;
    }

    results_unchanged_indexes.resize(Settings::values.record_num_frames);
    pre_indexes.resize(Settings::values.record_num_frames);

    frame_num = 0;
    for (auto& frame : results_unchanged) {
        std::string lastEngine{""};
        u32 total_row_count = 0;
        u32 idx = 0;
        for (const auto& result : frame) {
            if (lastEngine != result.engineName) {
                results_unchanged_indexes[frame_num].push_back(idx);
                pre_indexes[frame_num][GetEngineIdx(result.engineName)].push_back(total_row_count);
                lastEngine = result.engineName;
            }
            for (const auto& arg : result.args) {
                ++total_row_count;
            }
            ++idx;
        }

        QStandardItemModel* preModel = new QStandardItemModel;
        preModel->insertColumns(0, static_cast<s32>(Columns::COUNT));
        preModel->setHorizontalHeaderLabels(
            QStringList({tr("Time"), tr("Engine"), tr("Reg"), tr("Method"), tr("Argument")}));
        preModel->setRowCount(total_row_count);

        pre_models.push_back(preModel);
        frame_num++;
    }

    // Build out the list of draws
    QStandardItemModel* listModel =
        static_cast<QStandardItemModel*>(ui->list_record_draws->model());
    ui->list_record_draws->setModel(nullptr);
    for (u32 frame_num = 0; frame_num < Settings::values.record_num_frames; ++frame_num) {
        auto frame = new QStandardItem(
            QString::fromStdString(fmt::format("Frame {}", results_frames[frame_num])));
        for (u32 i = 0; i < draw_indexes[frame_num].size() - 1; ++i) {
            frame->appendRow(new QStandardItem(QString::fromStdString(fmt::format("Draw {}", i))));
        }
        listModel->invisibleRootItem()->appendRow(frame);
    }
    ui->list_record_draws->setModel(listModel);
    ui->list_record_draws->selectionModel()->setCurrentIndex(
        listModel->item(0)->child(0)->index(),
        QItemSelectionModel::SelectionFlag::Select | QItemSelectionModel::SelectionFlag::Rows);
    ui->list_record_draws->expandAll();
    ui->list_record_draws->update();

    DrawIndexChanged(ui->list_record_draws->currentIndex());

    ui->do_capture->setEnabled(true);
    ui->send_to_console->setEnabled(true);
}

void ConfigureDebugRecord::FillDrawIndex(u32 frame, u32 draw) {
    ui->table_record_draw_state->setModel(nullptr);
    QStandardItemModel* drawModel = draw_models[frame];

    u32 draw_idx = draw_indexes[frame].at(draw);
    u32 result_idx = results_changed_indexes[frame].at(draw);
    while (result_idx < results_changed_indexes[frame].at(draw + 1)) {
        const auto& result = results_changed[frame][result_idx++];
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
    ui->table_record_draw_state->setVerticalHeader(draw_vertical_header);
    ui->table_record_draw_state->setHorizontalHeader(draw_horizontal_header);
    ui->table_record_draw_state->horizontalHeader()->setVisible(true);
}

void ConfigureDebugRecord::FillPreFrame(u32 frame) {
    ui->table_record_pre_state->setModel(nullptr);
    QStandardItemModel* preModel = pre_models[frame];

    u32 i = 0;
    for (const auto& result : results_unchanged[frame]) {
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
    ui->table_record_pre_state->setVerticalHeader(pre_vertical_header);
    ui->table_record_pre_state->setHorizontalHeader(pre_horizontal_header);
    ui->table_record_pre_state->hideColumn(static_cast<s32>(Columns::TIME));
    ui->table_record_pre_state->horizontalHeader()->setVisible(true);
}

void ConfigureDebugRecord::ShowThumbnail(bool force) {
    if (!force && !ui->btnHideThumbnails->isVisible()) {
        return;
    }
    if (results_thumbnails.size() == 0) {
        return;
    }
    const auto& thumbnail = results_thumbnails[current_frame];
    QImage image(thumbnail.data, thumbnail.width, thumbnail.height, QImage::Format_RGB32);
    image = image.mirrored(false, true);
    thumbnail_frame->resize(thumbnail.width, thumbnail.height);
    thumbnail_frame->setPixmap(image);
}

void ConfigureDebugRecord::OnThumbnailFrameHide() {
    ui->btnHideThumbnails->setVisible(false);
    ui->btnShowThumbnails->setVisible(true);
}

void ConfigureDebugRecord::Print() {
    const auto& gpu = system.GetInstance().GPU();

    u32 frame_num = 0;
    for (auto& frame : results_unchanged) {
        std::string toPrint;
        toPrint.reserve(0x2000);

        toPrint += fmt::format(
            "\n\n========================================\n==========================FRAME "
            "{}=========================\n========================================",
            results_frames[frame_num]);

        toPrint += "\n\n====================\n======PRE STATE=====\n====================\n";

        for (auto& entry : frame) {
            if (entry.args.size() == 0) {
                continue;
            }
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
        frame_num++;

        for (auto& frame : results_changed) {
            size_t lastDraw = -1;
            for (auto& entry : frame) {
                if (lastDraw != entry.draw) {
                    lastDraw = entry.draw;
                    toPrint += fmt::format("\n\nDraw {}\n", entry.draw);
                }

                if (entry.args.size() == 0) {
                    continue;
                }

                size_t line_width = toPrint.size();
                toPrint += fmt::format("    {:4} {} (0x{:04X}) ", entry.time.count(),
                                       entry.engineName, entry.method);
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
        }
        LOG_INFO(Render_OpenGL, "{}", toPrint);
    }
}
