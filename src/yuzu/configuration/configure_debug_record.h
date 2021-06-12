// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>
#include <QDialog>
#include <QTimer>
#include <QWidget>
#include <QTreeView>
#include <QHeaderView>

namespace Core {
class System;
}
namespace Tegra {
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

class ConfigureDebugRecord : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureDebugRecord(QWidget* parent = nullptr);
    ~ConfigureDebugRecord() override;

private:
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
    void DrawIndexChanged(const QModelIndex& new_index);
    void ResizeColumns();
    void ClearResults();
    void BuildResults();
    void FillDrawIndex(u32 frame, u32 draw = 0);
    void FillPreFrame(u32 frame);
    void Print();

    std::unique_ptr<Ui::ConfigureDebugRecord> ui;
    Core::System& system;
    QTimer* resultsTimer;
    size_t savedFrame = 0;
    s32 current_frame = 0;
    s32 current_draw = 0;
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
};
