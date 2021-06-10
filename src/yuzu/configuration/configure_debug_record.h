// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>
#include <QTimer>
#include <QWidget>

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
    void UpdateViews(const QString& new_text);
    void OnFilterChanged(const QString& new_text);
    void HideFilterColumns(
        const std::array<QStringList, static_cast<s32>(Columns::COUNT)>& filters);
    std::array<QStringList, static_cast<s32>(Columns::COUNT)> ParseFilters(const QString& new_text);
    void ShowRows(s32 draw);
    void HideAllRows();
    void DrawIndexChanged(s32 currentRow);
    void ResizeColumns();
    void ClearResults();
    void BuildResults();
    void FillDrawIndex(u32 idx);
    void Print();

    std::unique_ptr<Ui::ConfigureDebugRecord> ui;
    Core::System& system;
    QTimer* resultsTimer;
    size_t savedFrame = 0;
    std::vector<u32> results_indexes;
    std::vector<u32> draw_indexes;
};
