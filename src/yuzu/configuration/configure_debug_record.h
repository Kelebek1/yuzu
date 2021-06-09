// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>
#include <QWidget>
#include <QTimer>

namespace Core {
class System;
}
namespace Tegra {
class Record;
}

namespace Ui {
class ConfigureDebugRecord;
}

class ConfigureDebugRecord : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureDebugRecord(QWidget* parent = nullptr);
    ~ConfigureDebugRecord() override;

private:
    void OnFilterChanged(const QString& new_text);
    void DrawIndexChanged(int currentRow);
    void ResizeColumns();
    void ClearResults();
    void BuildResults();
    void Print();

    std::unique_ptr<Ui::ConfigureDebugRecord> ui;
    Core::System& system;
    QTimer* resultsTimer;
    size_t savedFrame = 0;
};
