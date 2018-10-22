// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QDir>
#include <QFileInfo>

#include "common/common_paths.h"
#include "common/file_util.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "core/settings.h"
#include "yuzu/compatibility_list.h"
#include "yuzu/game_list.h"
#include "yuzu/game_list_p.h"
#include "yuzu/game_list_worker.h"
#include "yuzu/ui_settings.h"

namespace {
void GetMetadataFromControlNCA(const FileSys::PatchManager& patch_manager, const FileSys::NCA& nca,
                               std::vector<u8>& icon, std::string& name) {
    auto [nacp, icon_file] = patch_manager.ParseControlNCA(nca);
    if (icon_file != nullptr)
        icon = icon_file->ReadAllBytes();
    if (nacp != nullptr)
        name = nacp->GetApplicationName();
}

bool HasSupportedFileExtension(const std::string& file_name) {
    const QFileInfo file = QFileInfo(QString::fromStdString(file_name));
    return GameList::supported_file_extensions.contains(file.suffix(), Qt::CaseInsensitive);
}

bool IsExtractedNCAMain(const std::string& file_name) {
    return QFileInfo(QString::fromStdString(file_name)).fileName() == "main";
}

QString FormatGameName(const std::string& physical_name) {
    const QString physical_name_as_qstring = QString::fromStdString(physical_name);
    const QFileInfo file_info(physical_name_as_qstring);

    if (IsExtractedNCAMain(physical_name)) {
        return file_info.dir().path();
    }

    return physical_name_as_qstring;
}

QString FormatPatchNameVersions(const FileSys::PatchManager& patch_manager,
                                Loader::AppLoader& loader, bool updatable = true) {
    const auto& disabled = Settings::values[patch_manager.GetTitleID()].disabled_patches;
    QString out;
    FileSys::VirtualFile update_raw;
    loader.ReadUpdateRaw(update_raw);
    for (const auto& kv : patch_manager.GetPatchVersionNames(update_raw)) {
        if (!updatable && kv.first == "Update")
            continue;

        const auto patch_disabled =
            std::find(disabled.begin(), disabled.end(), kv.first) != disabled.end();

        if (kv.second.empty()) {
            out.append(fmt::format(patch_disabled ? "<s>{}</s><br>" : "{}<br>", kv.first).c_str());
        } else {
            auto ver = kv.second;

            // Display container name for packed updates
            if (ver == "PACKED" && kv.first == "Update")
                ver = Loader::GetFileTypeString(loader.GetFileType());

            out.append(
                fmt::format(patch_disabled ? "<s>{} ({})</s><br>" : "{} ({})<br>", kv.first, ver)
                    .c_str());
        }
    }

    out.chop(1);
    return out;
}
} // Anonymous namespace

GameListWorker::GameListWorker(FileSys::VirtualFilesystem vfs, QString dir_path, bool deep_scan,
                               const CompatibilityList& compatibility_list)
    : vfs(std::move(vfs)), dir_path(std::move(dir_path)), deep_scan(deep_scan),
      compatibility_list(compatibility_list) {}

GameListWorker::~GameListWorker() = default;

void GameListWorker::AddInstalledTitlesToGameList() {
    const auto cache = Service::FileSystem::GetUnionContents();
    const auto installed_games = cache->ListEntriesFilter(FileSys::TitleType::Application,
                                                          FileSys::ContentRecordType::Program);

    for (const auto& game : installed_games) {
        const auto file = cache->GetEntryUnparsed(game);
        std::unique_ptr<Loader::AppLoader> loader = Loader::GetLoader(file);
        if (!loader)
            continue;

        std::vector<u8> icon;
        std::string name;
        u64 program_id = 0;
        loader->ReadProgramId(program_id);

        const FileSys::PatchManager patch{program_id};
        const auto control = cache->GetEntry(game.title_id, FileSys::ContentRecordType::Control);
        if (control != nullptr)
            GetMetadataFromControlNCA(patch, *control, icon, name);

        auto it = FindMatchingCompatibilityEntry(compatibility_list, program_id);

        // The game list uses this as compatibility number for untested games
        QString compatibility("99");
        if (it != compatibility_list.end())
            compatibility = it->second.first;

        emit EntryReady({
            new GameListItemPath(
                FormatGameName(file->GetFullPath()), icon, QString::fromStdString(name),
                QString::fromStdString(Loader::GetFileTypeString(loader->GetFileType())),
                program_id),
            new GameListItemCompat(compatibility),
            new GameListItem(FormatPatchNameVersions(patch, *loader)),
            new GameListItem(
                QString::fromStdString(Loader::GetFileTypeString(loader->GetFileType()))),
            new GameListItemSize(file->GetSize()),
        });
    }

    const auto control_data = cache->ListEntriesFilter(FileSys::TitleType::Application,
                                                       FileSys::ContentRecordType::Control);

    for (const auto& entry : control_data) {
        auto nca = cache->GetEntry(entry);
        if (nca != nullptr) {
            nca_control_map.insert_or_assign(entry.title_id, std::move(nca));
        }
    }
}

void GameListWorker::FillControlMap(const std::string& dir_path) {
    const auto nca_control_callback = [this](u64* num_entries_out, const std::string& directory,
                                             const std::string& virtual_name) -> bool {
        std::string physical_name = directory + DIR_SEP + virtual_name;

        if (stop_processing)
            return false; // Breaks the callback loop.

        bool is_dir = FileUtil::IsDirectory(physical_name);
        QFileInfo file_info(physical_name.c_str());
        if (!is_dir && file_info.suffix().toStdString() == "nca") {
            auto nca =
                std::make_unique<FileSys::NCA>(vfs->OpenFile(physical_name, FileSys::Mode::Read));
            if (nca->GetType() == FileSys::NCAContentType::Control) {
                const u64 title_id = nca->GetTitleId();
                nca_control_map.insert_or_assign(title_id, std::move(nca));
            }
        }
        return true;
    };

    FileUtil::ForeachDirectoryEntry(nullptr, dir_path, nca_control_callback);
}

void GameListWorker::AddFstEntriesToGameList(const std::string& dir_path, unsigned int recursion) {
    const auto callback = [this, recursion](u64* num_entries_out, const std::string& directory,
                                            const std::string& virtual_name) -> bool {
        std::string physical_name = directory + DIR_SEP + virtual_name;

        if (stop_processing)
            return false; // Breaks the callback loop.

        bool is_dir = FileUtil::IsDirectory(physical_name);
        if (!is_dir &&
            (HasSupportedFileExtension(physical_name) || IsExtractedNCAMain(physical_name))) {
            std::unique_ptr<Loader::AppLoader> loader =
                Loader::GetLoader(vfs->OpenFile(physical_name, FileSys::Mode::Read));
            if (!loader || ((loader->GetFileType() == Loader::FileType::Unknown ||
                             loader->GetFileType() == Loader::FileType::Error) &&
                            !UISettings::values.show_unknown))
                return true;

            std::vector<u8> icon;
            const auto res1 = loader->ReadIcon(icon);

            u64 program_id = 0;
            const auto res2 = loader->ReadProgramId(program_id);

            std::string name = " ";
            const auto res3 = loader->ReadTitle(name);

            const FileSys::PatchManager patch{program_id};

            if (res1 != Loader::ResultStatus::Success && res3 != Loader::ResultStatus::Success &&
                res2 == Loader::ResultStatus::Success) {
                // Use from metadata pool.
                if (nca_control_map.find(program_id) != nca_control_map.end()) {
                    const auto& nca = nca_control_map[program_id];
                    GetMetadataFromControlNCA(patch, *nca, icon, name);
                }
            }

            auto it = FindMatchingCompatibilityEntry(compatibility_list, program_id);

            // The game list uses this as compatibility number for untested games
            QString compatibility("99");
            if (it != compatibility_list.end())
                compatibility = it->second.first;

            emit EntryReady({
                new GameListItemPath(
                    FormatGameName(physical_name), icon, QString::fromStdString(name),
                    QString::fromStdString(Loader::GetFileTypeString(loader->GetFileType())),
                    program_id),
                new GameListItemCompat(compatibility),
                new GameListItem(
                    FormatPatchNameVersions(patch, *loader, loader->IsRomFSUpdatable())),
                new GameListItem(
                    QString::fromStdString(Loader::GetFileTypeString(loader->GetFileType()))),
                new GameListItemSize(FileUtil::GetSize(physical_name)),
            });
        } else if (is_dir && recursion > 0) {
            watch_list.append(QString::fromStdString(physical_name));
            AddFstEntriesToGameList(physical_name, recursion - 1);
        }

        return true;
    };

    FileUtil::ForeachDirectoryEntry(nullptr, dir_path, callback);
}

void GameListWorker::run() {
    stop_processing = false;
    watch_list.append(dir_path);
    FillControlMap(dir_path.toStdString());
    AddInstalledTitlesToGameList();
    AddFstEntriesToGameList(dir_path.toStdString(), deep_scan ? 256 : 0);
    nca_control_map.clear();
    emit Finished(watch_list);
}

void GameListWorker::Cancel() {
    this->disconnect();
    stop_processing = true;
}
