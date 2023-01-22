// SPDX-FileCopyrightText: 2015-2022 Alexey Rochev
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "torrent.h"

#include <algorithm>
#include <array>
#include <stdexcept>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>

#include "jsonutils.h"
#include "itemlistupdater.h"
#include "log.h"
#include "pathutils.h"
#include "rpc.h"
#include "stdutils.h"

namespace libtremotesf {
    using namespace impl;

    enum class TorrentData::UpdateKey {
        Id,
        HashString,
        AddedDate,
        Name,
        MagnetLink,
        QueuePosition,
        TotalSize,
        CompletedSize,
        LeftUntilDone,
        SizeWhenDone,
        PercentDone,
        RecheckProgress,
        Eta,
        MetadataPercentComplete,
        DownloadSpeed,
        UploadSpeed,
        DownloadSpeedLimited,
        DownloadSpeedLimit,
        UploadSpeedLimited,
        UploadSpeedLimit,
        TotalDownloaded,
        TotalUploaded,
        Ratio,
        RatioLimitMode,
        RatioLimit,
        Seeders,
        Leechers,
        Status,
        Error,
        ErrorString,
        ActivityDate,
        DoneDate,
        PeersLimit,
        HonorSessionLimits,
        BandwidthPriority,
        IdleSeedingLimitMode,
        IdleSeedingLimit,
        DownloadDirectory,
        Creator,
        CreationDate,
        Comment,
        WebSeeders,
        ActiveWebSeeders,
        TrackerStats,
        Count
    };

    namespace {
        constexpr QLatin1String updateKeyString(TorrentData::UpdateKey key) {
            switch (key) {
            case TorrentData::UpdateKey::Id:
                return "id"_l1;
            case TorrentData::UpdateKey::HashString:
                return "hashString"_l1;
            case TorrentData::UpdateKey::AddedDate:
                return "addedDate"_l1;
            case TorrentData::UpdateKey::Name:
                return "name"_l1;
            case TorrentData::UpdateKey::MagnetLink:
                return "magnetLink"_l1;
            case TorrentData::UpdateKey::QueuePosition:
                return "queuePosition"_l1;
            case TorrentData::UpdateKey::TotalSize:
                return "totalSize"_l1;
            case TorrentData::UpdateKey::CompletedSize:
                return "haveValid"_l1;
            case TorrentData::UpdateKey::LeftUntilDone:
                return "leftUntilDone"_l1;
            case TorrentData::UpdateKey::SizeWhenDone:
                return "sizeWhenDone"_l1;
            case TorrentData::UpdateKey::PercentDone:
                return "percentDone"_l1;
            case TorrentData::UpdateKey::RecheckProgress:
                return "recheckProgress"_l1;
            case TorrentData::UpdateKey::Eta:
                return "eta"_l1;
            case TorrentData::UpdateKey::MetadataPercentComplete:
                return "metadataPercentComplete"_l1;
            case TorrentData::UpdateKey::DownloadSpeed:
                return "rateDownload"_l1;
            case TorrentData::UpdateKey::UploadSpeed:
                return "rateUpload"_l1;
            case TorrentData::UpdateKey::DownloadSpeedLimited:
                return "downloadLimited"_l1;
            case TorrentData::UpdateKey::DownloadSpeedLimit:
                return "downloadLimit"_l1;
            case TorrentData::UpdateKey::UploadSpeedLimited:
                return "uploadLimited"_l1;
            case TorrentData::UpdateKey::UploadSpeedLimit:
                return "uploadLimit"_l1;
            case TorrentData::UpdateKey::TotalDownloaded:
                return "downloadedEver"_l1;
            case TorrentData::UpdateKey::TotalUploaded:
                return "uploadedEver"_l1;
            case TorrentData::UpdateKey::Ratio:
                return "uploadRatio"_l1;
            case TorrentData::UpdateKey::RatioLimitMode:
                return "seedRatioMode"_l1;
            case TorrentData::UpdateKey::RatioLimit:
                return "seedRatioLimit"_l1;
            case TorrentData::UpdateKey::Seeders:
                return "peersSendingToUs"_l1;
            case TorrentData::UpdateKey::Leechers:
                return "peersGettingFromUs"_l1;
            case TorrentData::UpdateKey::Status:
                return "status"_l1;
            case TorrentData::UpdateKey::Error:
                return "error"_l1;
            case TorrentData::UpdateKey::ErrorString:
                return "errorString"_l1;
            case TorrentData::UpdateKey::ActivityDate:
                return "activityDate"_l1;
            case TorrentData::UpdateKey::DoneDate:
                return "doneDate"_l1;
            case TorrentData::UpdateKey::PeersLimit:
                return "peer-limit"_l1;
            case TorrentData::UpdateKey::HonorSessionLimits:
                return "honorsSessionLimits"_l1;
            case TorrentData::UpdateKey::BandwidthPriority:
                return "bandwidthPriority"_l1;
            case TorrentData::UpdateKey::IdleSeedingLimitMode:
                return "seedIdleMode"_l1;
            case TorrentData::UpdateKey::IdleSeedingLimit:
                return "seedIdleLimit"_l1;
            case TorrentData::UpdateKey::DownloadDirectory:
                return "downloadDir"_l1;
            case TorrentData::UpdateKey::Creator:
                return "creator"_l1;
            case TorrentData::UpdateKey::CreationDate:
                return "dateCreated"_l1;
            case TorrentData::UpdateKey::Comment:
                return "comment"_l1;
            case TorrentData::UpdateKey::WebSeeders:
                return "webseeds"_l1;
            case TorrentData::UpdateKey::ActiveWebSeeders:
                return "webseedsSendingToUs"_l1;
            case TorrentData::UpdateKey::TrackerStats:
                return "trackerStats"_l1;
            case TorrentData::UpdateKey::Count:
                return {};
            }
            return {};
        }

        std::optional<TorrentData::UpdateKey> mapUpdateKey(const QString& stringKey) {
            static const auto mapping = [] {
                std::map<QLatin1String, TorrentData::UpdateKey, std::less<>> map{};
                for (int i = 0; i < static_cast<int>(TorrentData::UpdateKey::Count); ++i) {
                    const auto key = static_cast<TorrentData::UpdateKey>(i);
                    map.emplace(updateKeyString(key), key);
                }
                return map;
            }();
            const auto foundKey = mapping.find(stringKey);
            if (foundKey == mapping.end()) {
                logWarning("Unknown torrent field '{}'", stringKey);
                return {};
            }
            return static_cast<TorrentData::UpdateKey>(foundKey->second);
        }

        constexpr auto prioritiesKey = "priorities"_l1;
        constexpr auto wantedFilesKey = "files-wanted"_l1;
        constexpr auto unwantedFilesKey = "files-unwanted"_l1;

        constexpr auto lowPriorityKey = "priority-low"_l1;
        constexpr auto normalPriorityKey = "priority-normal"_l1;
        constexpr auto highPriorityKey = "priority-high"_l1;

        constexpr auto addTrackerKey = "trackerAdd"_l1;
        constexpr auto replaceTrackerKey = "trackerReplace"_l1;
        constexpr auto removeTrackerKey = "trackerRemove"_l1;

        constexpr auto statusMapper = EnumMapper(std::array{
            EnumMapping(TorrentData::Status::Paused, 0),
            EnumMapping(TorrentData::Status::QueuedForChecking, 1),
            EnumMapping(TorrentData::Status::Checking, 2),
            EnumMapping(TorrentData::Status::QueuedForDownloading, 3),
            EnumMapping(TorrentData::Status::Downloading, 4),
            EnumMapping(TorrentData::Status::QueuedForSeeding, 5),
            EnumMapping(TorrentData::Status::Seeding, 6)});

        constexpr auto errorMapper = EnumMapper(std::array{
            EnumMapping(TorrentData::Error::None, 0),
            EnumMapping(TorrentData::Error::TrackerWarning, 1),
            EnumMapping(TorrentData::Error::TrackerError, 2),
            EnumMapping(TorrentData::Error::LocalError, 3)});

        constexpr auto priorityMapper = EnumMapper(std::array{
            EnumMapping(TorrentData::Priority::Low, -1),
            EnumMapping(TorrentData::Priority::Normal, 0),
            EnumMapping(TorrentData::Priority::High, 1)});

        constexpr auto ratioLimitModeMapper = EnumMapper(std::array{
            EnumMapping(TorrentData::RatioLimitMode::Global, 0),
            EnumMapping(TorrentData::RatioLimitMode::Single, 1),
            EnumMapping(TorrentData::RatioLimitMode::Unlimited, 2)});

        constexpr auto idleSeedingLimitModeMapper = EnumMapper(std::array{
            EnumMapping(TorrentData::IdleSeedingLimitMode::Global, 0),
            EnumMapping(TorrentData::IdleSeedingLimitMode::Single, 1),
            EnumMapping(TorrentData::IdleSeedingLimitMode::Unlimited, 2)});
    }

    int TorrentData::priorityToInt(Priority value) { return priorityMapper.toJsonValue(value); }

    bool TorrentData::update(const QJsonObject& object, bool firstTime) {
        bool changed = false;
        for (auto i = object.begin(), end = object.end(); i != end; ++i) {
            const auto key = mapUpdateKey(i.key());
            if (key.has_value()) {
                updateProperty(*key, i.value(), changed, firstTime);
            }
        }
        return changed;
    }

    bool TorrentData::update(
        const std::vector<std::optional<TorrentData::UpdateKey>>& keys, const QJsonArray& values, bool firstTime
    ) {
        bool changed = false;
        const auto count = std::min(keys.size(), static_cast<size_t>(values.size()));
        for (size_t i = 0; i < count; ++i) {
            const auto key = keys[i];
            if (key.has_value()) {
                updateProperty(*key, values[static_cast<QJsonArray::size_type>(i)], changed, firstTime);
            }
        }
        return changed;
    }

    void
    TorrentData::updateProperty(TorrentData::UpdateKey intKey, const QJsonValue& value, bool& changed, bool firstTime) {
        const auto key = static_cast<UpdateKey>(intKey);
        switch (static_cast<UpdateKey>(key)) {
        case TorrentData::UpdateKey::Id:
            return;
        case TorrentData::UpdateKey::HashString:
            if (firstTime) {
                hashString = value.toString();
            }
            return;
        case TorrentData::UpdateKey::AddedDate:
            return updateDateTime(addedDate, value, changed);
        case TorrentData::UpdateKey::Name:
            return setChanged(name, value.toString(), changed);
        case TorrentData::UpdateKey::MagnetLink:
            return setChanged(magnetLink, value.toString(), changed);
        case TorrentData::UpdateKey::QueuePosition:
            return setChanged(queuePosition, value.toInt(), changed);
        case TorrentData::UpdateKey::TotalSize:
            return setChanged(totalSize, static_cast<long long>(value.toDouble()), changed);
        case TorrentData::UpdateKey::CompletedSize:
            return setChanged(completedSize, static_cast<long long>(value.toDouble()), changed);
        case TorrentData::UpdateKey::LeftUntilDone:
            return setChanged(leftUntilDone, static_cast<long long>(value.toDouble()), changed);
        case TorrentData::UpdateKey::SizeWhenDone:
            return setChanged(sizeWhenDone, static_cast<long long>(value.toDouble()), changed);
        case TorrentData::UpdateKey::PercentDone:
            return setChanged(percentDone, value.toDouble(), changed);
        case TorrentData::UpdateKey::RecheckProgress:
            return setChanged(recheckProgress, value.toDouble(), changed);
        case TorrentData::UpdateKey::Eta:
            return setChanged(eta, value.toInt(), changed);
        case TorrentData::UpdateKey::MetadataPercentComplete:
            return setChanged(metadataComplete, value.toInt() == 1, changed);
        case TorrentData::UpdateKey::DownloadSpeed:
            return setChanged(downloadSpeed, static_cast<long long>(value.toDouble()), changed);
        case TorrentData::UpdateKey::UploadSpeed:
            return setChanged(uploadSpeed, static_cast<long long>(value.toDouble()), changed);
        case TorrentData::UpdateKey::DownloadSpeedLimited:
            return setChanged(downloadSpeedLimited, value.toBool(), changed);
        case TorrentData::UpdateKey::DownloadSpeedLimit:
            return setChanged(downloadSpeedLimit, value.toInt(), changed);
        case TorrentData::UpdateKey::UploadSpeedLimited:
            return setChanged(uploadSpeedLimited, value.toBool(), changed);
        case TorrentData::UpdateKey::UploadSpeedLimit:
            return setChanged(uploadSpeedLimit, value.toInt(), changed);
        case TorrentData::UpdateKey::TotalDownloaded:
            return setChanged(totalDownloaded, static_cast<long long>(value.toDouble()), changed);
        case TorrentData::UpdateKey::TotalUploaded:
            return setChanged(totalUploaded, static_cast<long long>(value.toDouble()), changed);
        case TorrentData::UpdateKey::Ratio:
            return setChanged(ratio, value.toDouble(), changed);
        case TorrentData::UpdateKey::RatioLimitMode:
            return setChanged(ratioLimitMode, ratioLimitModeMapper.fromJsonValue(value, updateKeyString(key)), changed);
        case TorrentData::UpdateKey::RatioLimit:
            return setChanged(ratioLimit, value.toDouble(), changed);
        case TorrentData::UpdateKey::Seeders:
            return setChanged(seeders, value.toInt(), changed);
        case TorrentData::UpdateKey::Leechers:
            return setChanged(leechers, value.toInt(), changed);
        case TorrentData::UpdateKey::Status:
            return setChanged(status, statusMapper.fromJsonValue(value, updateKeyString(key)), changed);
            break;
        case TorrentData::UpdateKey::Error:
            return setChanged(error, errorMapper.fromJsonValue(value, updateKeyString(key)), changed);
        case TorrentData::UpdateKey::ErrorString:
            return setChanged(errorString, value.toString(), changed);
        case TorrentData::UpdateKey::ActivityDate:
            return updateDateTime(activityDate, value, changed);
        case TorrentData::UpdateKey::DoneDate:
            return updateDateTime(doneDate, value, changed);
        case TorrentData::UpdateKey::PeersLimit:
            return setChanged(peersLimit, value.toInt(), changed);
        case TorrentData::UpdateKey::HonorSessionLimits:
            return setChanged(honorSessionLimits, value.toBool(), changed);
        case TorrentData::UpdateKey::BandwidthPriority:
            return setChanged(bandwidthPriority, priorityMapper.fromJsonValue(value, updateKeyString(key)), changed);
        case TorrentData::UpdateKey::IdleSeedingLimitMode:
            return setChanged(
                idleSeedingLimitMode,
                idleSeedingLimitModeMapper.fromJsonValue(value, updateKeyString(key)),
                changed
            );
        case TorrentData::UpdateKey::IdleSeedingLimit:
            return setChanged(idleSeedingLimit, value.toInt(), changed);
        case TorrentData::UpdateKey::DownloadDirectory:
            return setChanged(downloadDirectory, normalizePath(value.toString()), changed);
        case TorrentData::UpdateKey::Creator:
            return setChanged(creator, value.toString(), changed);
        case TorrentData::UpdateKey::CreationDate:
            return updateDateTime(creationDate, value, changed);
        case TorrentData::UpdateKey::Comment:
            return setChanged(comment, value.toString(), changed);
        case TorrentData::UpdateKey::WebSeeders: {
            std::vector<QString> newWebSeeders;
            const auto webSeedersStrings = value.toArray();
            newWebSeeders.reserve(static_cast<size_t>(webSeedersStrings.size()));
            std::transform(
                webSeedersStrings.begin(),
                webSeedersStrings.end(),
                std::back_insert_iterator(newWebSeeders),
                [](const auto& value) { return value.toString(); }
            );
            return setChanged(webSeeders, std::move(newWebSeeders), changed);
        }
        case TorrentData::UpdateKey::ActiveWebSeeders:
            return setChanged(activeWebSeeders, value.toInt(), changed);
        case TorrentData::UpdateKey::TrackerStats: {
            std::vector<Tracker> newTrackers{};
            const QJsonArray trackerJsons = value.toArray();
            newTrackers.reserve(static_cast<size_t>(trackerJsons.size()));
            for (const auto& i : trackerJsons) {
                const QJsonObject trackerMap = i.toObject();
                const int trackerId = trackerMap.value("id"_l1).toInt();
                const auto found = std::find_if(trackers.begin(), trackers.end(), [&](const auto& tracker) {
                    return tracker.id() == trackerId;
                });
                if (found == trackers.end()) {
                    newTrackers.emplace_back(trackerId, trackerMap);
                    changed = true;
                } else {
                    if (found->update(trackerMap)) {
                        changed = true;
                    }
                    newTrackers.push_back(std::move(*found));
                }
            }
            trackers = std::move(newTrackers);
            return;
        }
        case TorrentData::UpdateKey::Count:
            throw std::logic_error("UpdateKey::Count should not be mapped");
        }
        throw std::logic_error(fmt::format("Can't update key {}", static_cast<int>(intKey)));
    }

    Torrent::Torrent(int id, const QJsonObject& object, Rpc* rpc, QObject* parent) : QObject(parent), mRpc(rpc) {
        mData.id = id;
        [[maybe_unused]] bool changed = mData.update(object, true);
    }

    Torrent::Torrent(
        int id,
        const std::vector<std::optional<TorrentData::UpdateKey>>& keys,
        const QJsonArray& values,
        Rpc* rpc,
        QObject* parent
    )
        : QObject(parent), mRpc(rpc) {
        mData.id = id;
        [[maybe_unused]] bool changed = mData.update(keys, values, true);
    }

    QJsonArray Torrent::updateFields() {
        QJsonArray fields{};
        for (int i = 0; i < static_cast<int>(TorrentData::UpdateKey::Count); ++i) {
            const auto key = static_cast<TorrentData::UpdateKey>(i);
            fields.push_back(updateKeyString(key));
        }
        return fields;
    }

    std::optional<int> Torrent::idFromJson(const QJsonObject& object) {
        const auto value = object.value(updateKeyString(TorrentData::UpdateKey::Id));
        if (value.isDouble()) {
            return value.toInt();
        }
        return {};
    }

    std::optional<QJsonArray::size_type>
    Torrent::idKeyIndex(const std::vector<std::optional<TorrentData::UpdateKey>>& keys) {
        return indexOfCasted<QJsonArray::size_type>(keys, TorrentData::UpdateKey::Id);
    }

    std::vector<std::optional<TorrentData::UpdateKey>> Torrent::mapUpdateKeys(const QJsonArray& stringKeys) {
        std::vector<std::optional<TorrentData::UpdateKey>> keys{};
        keys.reserve(static_cast<size_t>(stringKeys.size()));
        std::transform(stringKeys.begin(), stringKeys.end(), std::back_inserter(keys), [](const QJsonValue& stringKey) {
            return mapUpdateKey(stringKey.toString());
        });
        return keys;
    }

    void Torrent::setDownloadSpeedLimited(bool limited) {
        mData.downloadSpeedLimited = limited;
        mRpc->setTorrentProperty(mData.id, updateKeyString(TorrentData::UpdateKey::DownloadSpeedLimited), limited);
    }

    void Torrent::setDownloadSpeedLimit(int limit) {
        mData.downloadSpeedLimit = limit;
        mRpc->setTorrentProperty(mData.id, updateKeyString(TorrentData::UpdateKey::DownloadSpeedLimit), limit);
    }

    void Torrent::setUploadSpeedLimited(bool limited) {
        mData.uploadSpeedLimited = limited;
        mRpc->setTorrentProperty(mData.id, updateKeyString(TorrentData::UpdateKey::UploadSpeedLimited), limited);
    }

    void Torrent::setUploadSpeedLimit(int limit) {
        mData.uploadSpeedLimit = limit;
        mRpc->setTorrentProperty(mData.id, updateKeyString(TorrentData::UpdateKey::UploadSpeedLimit), limit);
    }

    void Torrent::setRatioLimitMode(TorrentData::RatioLimitMode mode) {
        mData.ratioLimitMode = mode;
        mRpc->setTorrentProperty(
            mData.id,
            updateKeyString(TorrentData::UpdateKey::RatioLimitMode),
            ratioLimitModeMapper.toJsonValue(mode)
        );
    }

    void Torrent::setRatioLimit(double limit) {
        mData.ratioLimit = limit;
        mRpc->setTorrentProperty(mData.id, updateKeyString(TorrentData::UpdateKey::RatioLimit), limit);
    }

    void Torrent::setPeersLimit(int limit) {
        mData.peersLimit = limit;
        mRpc->setTorrentProperty(mData.id, updateKeyString(TorrentData::UpdateKey::PeersLimit), limit);
    }

    void Torrent::setHonorSessionLimits(bool honor) {
        mData.honorSessionLimits = honor;
        mRpc->setTorrentProperty(mData.id, updateKeyString(TorrentData::UpdateKey::HonorSessionLimits), honor);
    }

    void Torrent::setBandwidthPriority(TorrentData::Priority priority) {
        mData.bandwidthPriority = priority;
        mRpc->setTorrentProperty(
            mData.id,
            updateKeyString(TorrentData::UpdateKey::BandwidthPriority),
            priorityMapper.toJsonValue(priority)
        );
    }

    void Torrent::setIdleSeedingLimitMode(TorrentData::IdleSeedingLimitMode mode) {
        mData.idleSeedingLimitMode = mode;
        mRpc->setTorrentProperty(
            mData.id,
            updateKeyString(TorrentData::UpdateKey::IdleSeedingLimitMode),
            idleSeedingLimitModeMapper.toJsonValue(mode)
        );
    }

    void Torrent::setIdleSeedingLimit(int limit) {
        mData.idleSeedingLimit = limit;
        mRpc->setTorrentProperty(mData.id, updateKeyString(TorrentData::UpdateKey::IdleSeedingLimit), limit);
    }

    void Torrent::addTrackers(const QStringList& announceUrls) {
        mRpc->setTorrentProperty(mData.id, addTrackerKey, QJsonArray::fromStringList(announceUrls), true);
    }

    void Torrent::setTracker(int trackerId, const QString& announce) {
        mRpc->setTorrentProperty(mData.id, replaceTrackerKey, QJsonArray{trackerId, announce}, true);
    }

    void Torrent::removeTrackers(const std::vector<int>& ids) {
        mRpc->setTorrentProperty(mData.id, removeTrackerKey, toJsonArray(ids), true);
    }

    void Torrent::setFilesEnabled(bool enabled) {
        if (enabled != mFilesEnabled) {
            mFilesEnabled = enabled;
            if (mFilesEnabled) {
                mRpc->getTorrentsFiles({mData.id}, false);
            } else {
                mFiles.clear();
            }
        }
    }

    void Torrent::setFilesWanted(const std::vector<int>& fileIds, bool wanted) {
        mRpc->setTorrentProperty(mData.id, wanted ? wantedFilesKey : unwantedFilesKey, toJsonArray(fileIds));
    }

    void Torrent::setFilesPriority(const std::vector<int>& fileIds, TorrentFile::Priority priority) {
        QLatin1String propertyName;
        switch (priority) {
        case TorrentFile::Priority::Low:
            propertyName = lowPriorityKey;
            break;
        case TorrentFile::Priority::Normal:
            propertyName = normalPriorityKey;
            break;
        case TorrentFile::Priority::High:
            propertyName = highPriorityKey;
            break;
        }
        mRpc->setTorrentProperty(mData.id, propertyName, toJsonArray(fileIds));
    }

    void Torrent::renameFile(const QString& path, const QString& newName) {
        mRpc->renameTorrentFile(mData.id, path, newName);
    }

    void Torrent::setPeersEnabled(bool enabled) {
        if (enabled != mPeersEnabled) {
            mPeersEnabled = enabled;
            if (mPeersEnabled) {
                mRpc->getTorrentsPeers({mData.id}, false);
            } else {
                mPeers.clear();
            }
        }
    }

    bool Torrent::update(const QJsonObject& object) {
        const bool c = mData.update(object, false);
        emit updated();
        if (c) {
            emit changed();
        }
        return c;
    }

    bool Torrent::update(const std::vector<std::optional<TorrentData::UpdateKey>>& keys, const QJsonArray& values) {
        const bool c = mData.update(keys, values, false);
        emit updated();
        if (c) {
            emit changed();
        }
        return c;
    }

    void Torrent::updateFiles(const QJsonObject& torrentMap) {
        std::vector<int> changed{};

        const QJsonArray fileStats = torrentMap.value("fileStats"_l1).toArray();
        if (!fileStats.isEmpty()) {
            if (mFiles.empty()) {
                const QJsonArray fileJsons = torrentMap.value("files"_l1).toArray();
                if (fileJsons.size() == fileStats.size()) {
                    const auto count = fileJsons.size();
                    mFiles.reserve(static_cast<size_t>(count));
                    changed.reserve(static_cast<size_t>(count));
                    for (QJsonArray::size_type i = 0; i < count; ++i) {
                        mFiles.emplace_back(i, fileJsons[i].toObject(), fileStats[i].toObject());
                        changed.push_back(static_cast<int>(i));
                    }
                } else {
                    logWarning("fileStats and files arrays have different sizes for torrent {}", *this);
                }
            } else {
                if (static_cast<size_t>(fileStats.size()) == mFiles.size()) {
                    for (QJsonArray::size_type i = 0, max = fileStats.size(); i < max; ++i) {
                        TorrentFile& file = mFiles[static_cast<size_t>(i)];
                        if (file.update(fileStats[i].toObject())) {
                            changed.push_back(static_cast<int>(i));
                        }
                    }
                } else {
                    logWarning("fileStats array has different size than in previous update for torrent {}", *this);
                }
            }
        }

        emit filesUpdated(changed);
        emit mRpc->torrentFilesUpdated(this, changed);
    }

    namespace {
        using NewPeer = std::pair<QJsonObject, QString>;

        class PeersListUpdater : public ItemListUpdater<Peer, NewPeer, std::vector<NewPeer>> {
        public:
            std::vector<std::pair<int, int>> removedIndexRanges;
            std::vector<std::pair<int, int>> changedIndexRanges;
            int addedCount = 0;

        protected:
            std::vector<NewPeer>::iterator
            findNewItemForItem(std::vector<NewPeer>& newPeers, const Peer& peer) override {
                const auto& address = peer.address;
                return std::find_if(newPeers.begin(), newPeers.end(), [address](const auto& newPeer) {
                    const auto& [json, newPeerAddress] = newPeer;
                    return newPeerAddress == address;
                });
            }

            void onAboutToRemoveItems(size_t, size_t) override{};

            void onRemovedItems(size_t first, size_t last) override {
                removedIndexRanges.emplace_back(static_cast<int>(first), static_cast<int>(last));
            }

            bool updateItem(Peer& peer, NewPeer&& newPeer) override {
                const auto& [json, address] = newPeer;
                return peer.update(json);
            }

            void onChangedItems(size_t first, size_t last) override {
                changedIndexRanges.emplace_back(static_cast<int>(first), static_cast<int>(last));
            }

            Peer createItemFromNewItem(NewPeer&& newPeer) override {
                auto& [json, address] = newPeer;
                return Peer(std::move(address), json);
            }

            void onAboutToAddItems(size_t) override {}

            void onAddedItems(size_t count) override { addedCount = static_cast<int>(count); };
        };
    }

    void Torrent::updatePeers(const QJsonObject& torrentMap) {
        std::vector<NewPeer> newPeers;
        {
            const QJsonArray peers(torrentMap.value("peers"_l1).toArray());
            newPeers.reserve(static_cast<size_t>(peers.size()));
            for (const auto& i : peers) {
                QJsonObject json = i.toObject();
                QString address(json.value(Peer::addressKey).toString());
                newPeers.emplace_back(std::move(json), std::move(address));
            }
        }

        PeersListUpdater updater;
        updater.update(mPeers, std::move(newPeers));

        emit peersUpdated(updater.removedIndexRanges, updater.changedIndexRanges, updater.addedCount);
        emit mRpc
            ->torrentPeersUpdated(this, updater.removedIndexRanges, updater.changedIndexRanges, updater.addedCount);
    }

    void Torrent::checkSingleFile(const QJsonObject& torrentMap) {
        mData.singleFile = (torrentMap.value(prioritiesKey).toArray().size() == 1);
    }
}

fmt::format_context::iterator
fmt::formatter<libtremotesf::Torrent>::format(const libtremotesf::Torrent& torrent, format_context& ctx) FORMAT_CONST {
    return format_to(ctx.out(), "Torrent(id={}, name={})", torrent.data().id, torrent.data().name);
}
