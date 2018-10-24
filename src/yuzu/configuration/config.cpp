// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QSettings>
#include "common/file_util.h"
#include "input_common/main.h"
#include "yuzu/configuration/config.h"
#include "yuzu/ui_settings.h"

Config::Config() {
    // TODO: Don't hardcode the path; let the frontend decide where to put the config files.
    qt_config_loc = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "qt-config.ini";
    FileUtil::CreateFullPath(qt_config_loc);
    qt_config = new QSettings(QString::fromStdString(qt_config_loc), QSettings::IniFormat);

    Reload();
}

const std::array<int, Settings::NativeButton::NumButtons> Config::default_buttons = {
    Qt::Key_A, Qt::Key_S, Qt::Key_Z,    Qt::Key_X,  Qt::Key_3,     Qt::Key_4,    Qt::Key_Q,
    Qt::Key_W, Qt::Key_1, Qt::Key_2,    Qt::Key_N,  Qt::Key_M,     Qt::Key_F,    Qt::Key_T,
    Qt::Key_H, Qt::Key_G, Qt::Key_Left, Qt::Key_Up, Qt::Key_Right, Qt::Key_Down, Qt::Key_J,
    Qt::Key_I, Qt::Key_L, Qt::Key_K,    Qt::Key_D,  Qt::Key_C,     Qt::Key_B,    Qt::Key_V,
};

const std::array<std::array<int, 5>, Settings::NativeAnalog::NumAnalogs> Config::default_analogs{{
    {
        Qt::Key_Up,
        Qt::Key_Down,
        Qt::Key_Left,
        Qt::Key_Right,
        Qt::Key_E,
    },
    {
        Qt::Key_I,
        Qt::Key_K,
        Qt::Key_J,
        Qt::Key_L,
        Qt::Key_R,
    },
}};

void Config::ReadValues() {
    qt_config->beginGroup("Controls");
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        Settings::values.buttons[i] =
            qt_config
                ->value(Settings::NativeButton::mapping[i], QString::fromStdString(default_param))
                .toString()
                .toStdString();
        if (Settings::values.buttons[i].empty())
            Settings::values.buttons[i] = default_param;
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_analogs[i][4], 0.5f);
        Settings::values.analogs[i] =
            qt_config
                ->value(Settings::NativeAnalog::mapping[i], QString::fromStdString(default_param))
                .toString()
                .toStdString();
        if (Settings::values.analogs[i].empty())
            Settings::values.analogs[i] = default_param;
    }

    Settings::values.motion_device =
        qt_config->value("motion_device", "engine:motion_emu,update_period:100,sensitivity:0.01")
            .toString()
            .toStdString();
    Settings::values.touch_device =
        qt_config->value("touch_device", "engine:emu_window").toString().toStdString();

    qt_config->endGroup();

    qt_config->beginGroup("Core");
    Settings::values.use_cpu_jit = qt_config->value("use_cpu_jit", true).toBool();
    Settings::values.use_multi_core = qt_config->value("use_multi_core", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    Settings::values.resolution_factor = qt_config->value("resolution_factor", 1.0).toFloat();
    Settings::values.use_frame_limit = qt_config->value("use_frame_limit", true).toBool();
    Settings::values.frame_limit = qt_config->value("frame_limit", 100).toInt();
    Settings::values.use_accurate_gpu_emulation =
        qt_config->value("use_accurate_gpu_emulation", false).toBool();

    Settings::values.bg_red = qt_config->value("bg_red", 0.0).toFloat();
    Settings::values.bg_green = qt_config->value("bg_green", 0.0).toFloat();
    Settings::values.bg_blue = qt_config->value("bg_blue", 0.0).toFloat();
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    Settings::values.sink_id = qt_config->value("output_engine", "auto").toString().toStdString();
    Settings::values.enable_audio_stretching =
        qt_config->value("enable_audio_stretching", true).toBool();
    Settings::values.audio_device_id =
        qt_config->value("output_device", "auto").toString().toStdString();
    Settings::values.volume = qt_config->value("volume", 1).toFloat();
    qt_config->endGroup();

    qt_config->beginGroup("Data Storage");
    Settings::values.use_virtual_sd = qt_config->value("use_virtual_sd", true).toBool();
    FileUtil::GetUserPath(
        FileUtil::UserPath::NANDDir,
        qt_config
            ->value("nand_directory",
                    QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)))
            .toString()
            .toStdString());
    FileUtil::GetUserPath(
        FileUtil::UserPath::SDMCDir,
        qt_config
            ->value("sdmc_directory",
                    QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)))
            .toString()
            .toStdString());
    qt_config->endGroup();

    qt_config->beginGroup("System");
    Settings::values.use_docked_mode = qt_config->value("use_docked_mode", false).toBool();
    Settings::values.enable_nfc = qt_config->value("enable_nfc", true).toBool();
    Settings::values.username = qt_config->value("username", "yuzu").toString().toStdString();
    Settings::values.language_index = qt_config->value("language_index", 1).toInt();
    qt_config->endGroup();

    qt_config->beginGroup("Miscellaneous");
    Settings::values.log_filter = qt_config->value("log_filter", "*:Info").toString().toStdString();
    Settings::values.use_dev_keys = qt_config->value("use_dev_keys", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    Settings::values.use_gdbstub = qt_config->value("use_gdbstub", false).toBool();
    Settings::values.gdbstub_port = qt_config->value("gdbstub_port", 24689).toInt();
    Settings::values.program_args = qt_config->value("program_args", "").toString().toStdString();
    qt_config->endGroup();

    qt_config->beginGroup("WebService");
    Settings::values.enable_telemetry = qt_config->value("enable_telemetry", true).toBool();
    Settings::values.web_api_url =
        qt_config->value("web_api_url", "https://api.yuzu-emu.org").toString().toStdString();
    Settings::values.yuzu_username = qt_config->value("yuzu_username").toString().toStdString();
    Settings::values.yuzu_token = qt_config->value("yuzu_token").toString().toStdString();
    qt_config->endGroup();

    qt_config->beginGroup("UI");
    UISettings::values.theme = qt_config->value("theme", UISettings::themes[0].second).toString();
    UISettings::values.enable_discord_presence =
        qt_config->value("enable_discord_presence", true).toBool();

    qt_config->beginGroup("UIGameList");
    UISettings::values.show_unknown = qt_config->value("show_unknown", true).toBool();
    UISettings::values.icon_size = qt_config->value("icon_size", 64).toUInt();
    UISettings::values.row_1_text_id = qt_config->value("row_1_text_id", 3).toUInt();
    UISettings::values.row_2_text_id = qt_config->value("row_2_text_id", 2).toUInt();
    qt_config->endGroup();

    qt_config->beginGroup("UILayout");
    UISettings::values.geometry = qt_config->value("geometry").toByteArray();
    UISettings::values.state = qt_config->value("state").toByteArray();
    UISettings::values.renderwindow_geometry =
        qt_config->value("geometryRenderWindow").toByteArray();
    UISettings::values.gamelist_header_state =
        qt_config->value("gameListHeaderState").toByteArray();
    UISettings::values.microprofile_geometry =
        qt_config->value("microProfileDialogGeometry").toByteArray();
    UISettings::values.microprofile_visible =
        qt_config->value("microProfileDialogVisible", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Paths");
    UISettings::values.roms_path = qt_config->value("romsPath").toString();
    UISettings::values.symbols_path = qt_config->value("symbolsPath").toString();
    UISettings::values.gamedir = qt_config->value("gameListRootDir", ".").toString();
    UISettings::values.gamedir_deepscan = qt_config->value("gameListDeepScan", false).toBool();
    UISettings::values.recent_files = qt_config->value("recentFiles").toStringList();
    qt_config->endGroup();

    qt_config->beginGroup("Shortcuts");
    QStringList groups = qt_config->childGroups();
    for (auto group : groups) {
        qt_config->beginGroup(group);

        QStringList hotkeys = qt_config->childGroups();
        for (auto hotkey : hotkeys) {
            qt_config->beginGroup(hotkey);
            UISettings::values.shortcuts.emplace_back(UISettings::Shortcut(
                group + "/" + hotkey,
                UISettings::ContextualShortcut(qt_config->value("KeySeq").toString(),
                                               qt_config->value("Context").toInt())));
            qt_config->endGroup();
        }

        qt_config->endGroup();
    }
    qt_config->endGroup();

    UISettings::values.single_window_mode = qt_config->value("singleWindowMode", true).toBool();
    UISettings::values.fullscreen = qt_config->value("fullscreen", false).toBool();
    UISettings::values.display_titlebar = qt_config->value("displayTitleBars", true).toBool();
    UISettings::values.show_filter_bar = qt_config->value("showFilterBar", true).toBool();
    UISettings::values.show_status_bar = qt_config->value("showStatusBar", true).toBool();
    UISettings::values.confirm_before_closing = qt_config->value("confirmClose", true).toBool();
    UISettings::values.first_start = qt_config->value("firstStart", true).toBool();
    UISettings::values.callout_flags = qt_config->value("calloutFlags", 0).toUInt();
    UISettings::values.show_console = qt_config->value("showConsole", false).toBool();

    qt_config->endGroup();
}

void Config::SaveValues() {
    qt_config->beginGroup("Controls");
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        qt_config->setValue(QString::fromStdString(Settings::NativeButton::mapping[i]),
                            QString::fromStdString(Settings::values.buttons[i]));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        qt_config->setValue(QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                            QString::fromStdString(Settings::values.analogs[i]));
    }
    qt_config->setValue("motion_device", QString::fromStdString(Settings::values.motion_device));
    qt_config->setValue("touch_device", QString::fromStdString(Settings::values.touch_device));
    qt_config->endGroup();

    qt_config->beginGroup("Core");
    qt_config->setValue("use_cpu_jit", Settings::values.use_cpu_jit);
    qt_config->setValue("use_multi_core", Settings::values.use_multi_core);
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    qt_config->setValue("resolution_factor", (double)Settings::values.resolution_factor);
    qt_config->setValue("use_frame_limit", Settings::values.use_frame_limit);
    qt_config->setValue("frame_limit", Settings::values.frame_limit);
    qt_config->setValue("use_accurate_gpu_emulation", Settings::values.use_accurate_gpu_emulation);

    // Cast to double because Qt's written float values are not human-readable
    qt_config->setValue("bg_red", (double)Settings::values.bg_red);
    qt_config->setValue("bg_green", (double)Settings::values.bg_green);
    qt_config->setValue("bg_blue", (double)Settings::values.bg_blue);
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    qt_config->setValue("output_engine", QString::fromStdString(Settings::values.sink_id));
    qt_config->setValue("enable_audio_stretching", Settings::values.enable_audio_stretching);
    qt_config->setValue("output_device", QString::fromStdString(Settings::values.audio_device_id));
    qt_config->setValue("volume", Settings::values.volume);
    qt_config->endGroup();

    qt_config->beginGroup("Data Storage");
    qt_config->setValue("use_virtual_sd", Settings::values.use_virtual_sd);
    qt_config->setValue("nand_directory",
                        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)));
    qt_config->setValue("sdmc_directory",
                        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)));
    qt_config->endGroup();

    qt_config->beginGroup("System");
    qt_config->setValue("use_docked_mode", Settings::values.use_docked_mode);
    qt_config->setValue("enable_nfc", Settings::values.enable_nfc);
    qt_config->setValue("username", QString::fromStdString(Settings::values.username));
    qt_config->setValue("language_index", Settings::values.language_index);
    qt_config->endGroup();

    qt_config->beginGroup("Miscellaneous");
    qt_config->setValue("log_filter", QString::fromStdString(Settings::values.log_filter));
    qt_config->setValue("use_dev_keys", Settings::values.use_dev_keys);
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    qt_config->setValue("use_gdbstub", Settings::values.use_gdbstub);
    qt_config->setValue("gdbstub_port", Settings::values.gdbstub_port);
    qt_config->setValue("program_args", QString::fromStdString(Settings::values.program_args));
    qt_config->endGroup();

    qt_config->beginGroup("WebService");
    qt_config->setValue("enable_telemetry", Settings::values.enable_telemetry);
    qt_config->setValue("web_api_url", QString::fromStdString(Settings::values.web_api_url));
    qt_config->setValue("yuzu_username", QString::fromStdString(Settings::values.yuzu_username));
    qt_config->setValue("yuzu_token", QString::fromStdString(Settings::values.yuzu_token));
    qt_config->endGroup();

    qt_config->beginGroup("UI");
    qt_config->setValue("theme", UISettings::values.theme);
    qt_config->setValue("enable_discord_presence", UISettings::values.enable_discord_presence);

    qt_config->beginGroup("UIGameList");
    qt_config->setValue("show_unknown", UISettings::values.show_unknown);
    qt_config->setValue("icon_size", UISettings::values.icon_size);
    qt_config->setValue("row_1_text_id", UISettings::values.row_1_text_id);
    qt_config->setValue("row_2_text_id", UISettings::values.row_2_text_id);
    qt_config->endGroup();

    qt_config->beginGroup("UILayout");
    qt_config->setValue("geometry", UISettings::values.geometry);
    qt_config->setValue("state", UISettings::values.state);
    qt_config->setValue("geometryRenderWindow", UISettings::values.renderwindow_geometry);
    qt_config->setValue("gameListHeaderState", UISettings::values.gamelist_header_state);
    qt_config->setValue("microProfileDialogGeometry", UISettings::values.microprofile_geometry);
    qt_config->setValue("microProfileDialogVisible", UISettings::values.microprofile_visible);
    qt_config->endGroup();

    qt_config->beginGroup("Paths");
    qt_config->setValue("romsPath", UISettings::values.roms_path);
    qt_config->setValue("symbolsPath", UISettings::values.symbols_path);
    qt_config->setValue("gameListRootDir", UISettings::values.gamedir);
    qt_config->setValue("gameListDeepScan", UISettings::values.gamedir_deepscan);
    qt_config->setValue("recentFiles", UISettings::values.recent_files);
    qt_config->endGroup();

    qt_config->beginGroup("Shortcuts");
    for (auto shortcut : UISettings::values.shortcuts) {
        qt_config->setValue(shortcut.first + "/KeySeq", shortcut.second.first);
        qt_config->setValue(shortcut.first + "/Context", shortcut.second.second);
    }
    qt_config->endGroup();

    qt_config->setValue("singleWindowMode", UISettings::values.single_window_mode);
    qt_config->setValue("fullscreen", UISettings::values.fullscreen);
    qt_config->setValue("displayTitleBars", UISettings::values.display_titlebar);
    qt_config->setValue("showFilterBar", UISettings::values.show_filter_bar);
    qt_config->setValue("showStatusBar", UISettings::values.show_status_bar);
    qt_config->setValue("confirmClose", UISettings::values.confirm_before_closing);
    qt_config->setValue("firstStart", UISettings::values.first_start);
    qt_config->setValue("calloutFlags", UISettings::values.callout_flags);
    qt_config->setValue("showConsole", UISettings::values.show_console);
    qt_config->endGroup();
}

void Config::Reload() {
    ReadValues();
    Settings::Apply();
}

void Config::Save() {
    SaveValues();
}

Config::~Config() {
    Save();

    delete qt_config;
}
