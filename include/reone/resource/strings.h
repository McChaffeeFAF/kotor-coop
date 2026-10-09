/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "talktable.h"

#include "types.h"

#include <array>

namespace reone {

namespace resource {

class IStrings {
public:
    virtual ~IStrings() = default;

    virtual std::string getText(int strRef) = 0;
    virtual std::string getSound(int strRef) = 0;
};

class Strings : public IStrings {
public:
    static constexpr std::size_t kTalkTableSlotCount = 7;

    Strings() = default;

    void init(const std::filesystem::path &gameDir);

    void setTalkTable(std::size_t slot, std::shared_ptr<TalkTable> table);
    bool loadTalkTable(std::size_t slot, const std::filesystem::path &path);

    /**
     * K2's talk table, for the rows the K2 systems merge appends
     * (design note k2_merge). Strrefs flagged with kK2StrRefFlag resolve
     * here; without it they resolve to nothing, never to a K1 string.
     */
    void setK2TalkTable(std::shared_ptr<TalkTable> table) { _k2Table = std::move(table); }
    bool loadK2TalkTable(const std::filesystem::path &gameDir);
    bool hasK2TalkTable() const { return static_cast<bool>(_k2Table); }

    /// Our own strings (the K2 systems merge's Grey feat), for strrefs flagged
    /// with kOwnStrRefFlag.
    void setOwnTalkTable(std::shared_ptr<TalkTable> table) { _ownTable = std::move(table); }

    std::string getText(int strRef) override;
    std::string getSound(int strRef) override;

private:
    std::array<std::shared_ptr<TalkTable>, kTalkTableSlotCount> _tables;
    std::shared_ptr<TalkTable> _k2Table;
    std::shared_ptr<TalkTable> _ownTable;

    const TalkTable::String *findString(int strRef) const;

    void process(std::string &str);
    void stripDeveloperNotes(std::string &str);
};

class LocString {
public:
    LocString() :
        _id(-1) {}

    LocString(int32_t id, std::string ref, IStrings &strings) :
        _id(id), _ref(ref), _loc((_id >= 0) ? strings.getText(id) : ref) {}

    operator std::string_view() const {
        return str();
    }

    const std::string &str() const {
        return _loc;
    }

    const int32_t &id() const {
        return _id;
    }

private:
    int32_t _id;
    std::string _ref;
    std::string _loc;
};

class StrRef {
public:
    StrRef() :
        _id(-1) {}
    StrRef(int32_t id, IStrings &strings) :
        _id(id) {
        if (_id >= 0) {
            _str = strings.getText(id);
        }
    }

    operator std::string_view() const {
        return str();
    }

    const std::string &str() const {
        return _str;
    }

private:
    int32_t _id;
    std::string _str;
};

} // namespace resource

} // namespace reone
