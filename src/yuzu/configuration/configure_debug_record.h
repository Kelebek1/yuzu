// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>
#include <unordered_set>
#include <QDialog>
#include <QTimer>
#include <QWidget>
#include <QLabel>
#include <QTreeView>
#include <QHeaderView>
#include "video_core/gpu.h"

namespace Core {
class System;
}
namespace Tegra {
class GPU;
class Record;
}

namespace Ui {
class ConfigureDebugRecord;
}

enum class Columns : u32 {
    TIME = 0,
    ENGINE,
    REG,
    METHOD,
    ARGUMENT,
    COUNT,
};

class ThumbnailWindow;

class ConfigureDebugRecord : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureDebugRecord(QWidget* parent = nullptr);
    ~ConfigureDebugRecord() override;
    std::unique_ptr<Ui::ConfigureDebugRecord> ui;

    void UpdateViews();
    void HideUnkStateChanged(s32 state);
    void PauseClicked(s32 state);
    void RunClicked(s32 state);
    void StepFrameClicked(s32 state);
    void OnFilterChanged(const QString& new_text);
    void HideFilterColumns(
        const std::array<QStringList, static_cast<s32>(Columns::COUNT)>& filters);
    std::array<QStringList, static_cast<s32>(Columns::COUNT)> ParseFilters(const QString& new_text);
    void ShowRows();
    void HideAllRows();
    void FindAndSetPreRow(s32 row);
    void DrawIndexChanged(const QModelIndex& new_index);
    void ResizeColumns();
    void ClearResults();
    void BuildResults();
    void FillDrawIndex(u32 frame, u32 draw = 0);
    void FillPreFrame(u32 frame);
    void ShowThumbnail(bool force = false);
    void Print();

    void OnThumbnailFrameHide();

    ThumbnailWindow* thumbnail_frame;
    Core::System& system;
    QTimer* resultsTimer;
    size_t savedFrame = 0;
    s32 current_frame = 0;
    s32 current_draw = 0;
    std::vector<std::vector<Tegra::GPU::DrawResult>> results_changed;
    std::vector<std::vector<Tegra::GPU::DrawResult>> results_unchanged;
    std::vector<u32> results_frames;
    std::vector<Tegra::GPU::RecordThumbnail> results_thumbnails;
    std::vector<std::vector<u32>> results_changed_indexes;
    std::vector<std::vector<u32>> results_unchanged_indexes;
    std::vector<std::vector<u32>> draw_indexes;
    std::vector<std::array<std::vector<u32>, 5>> pre_indexes;
    std::vector<QStandardItemModel*> draw_models;
    std::vector<QStandardItemModel*> pre_models;

    QHeaderView* draw_vertical_header;
    QHeaderView* draw_horizontal_header;
    QHeaderView* pre_vertical_header;
    QHeaderView* pre_horizontal_header;
    QByteArray thumbnail_geometry;
};

class ThumbnailWindow : public QWidget {
    Q_OBJECT
public:
    ThumbnailWindow(ConfigureDebugRecord* main) : debugWindow{main} {
        this->hide();
        lbl_thumbnail = new QLabel(this);
        lbl_thumbnail->setContentsMargins(0, 0, 0, 0);
    }

    void closeEvent(QCloseEvent* event) override {
        debugWindow->OnThumbnailFrameHide();
    }

    void resizeEvent(QResizeEvent* event) override {
        lbl_thumbnail->resize(event->size());
    }

    void setPixmap(QImage& image) {
        lbl_thumbnail->setPixmap(QPixmap::fromImage(image));
    }

private:
    ConfigureDebugRecord* debugWindow;
    QLabel* lbl_thumbnail;
};