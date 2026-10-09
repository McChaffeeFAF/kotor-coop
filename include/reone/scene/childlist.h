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

#include <cstddef>
#include <list>
#include <unordered_map>

namespace reone {

namespace scene {

/**
 * A set of child nodes that iterates in insertion order.
 *
 * Scene node children used to be an unordered_set of pointers, so update and
 * draw order followed pointer values and changed from run to run. This keeps
 * the set's interface (insert, erase by key or iterator, find, count) with
 * O(1) operations, and iteration visits children in the order they were
 * added. Re-inserting a present child keeps its place.
 */
template <class T>
class ChildList {
public:
    using List = std::list<T *>;
    using iterator = typename List::const_iterator;
    using const_iterator = typename List::const_iterator;
    using value_type = T *;
    using size_type = std::size_t;

    std::pair<iterator, bool> insert(T *value) {
        auto found = _index.find(value);
        if (found != _index.end()) {
            return {found->second, false};
        }
        auto it = _list.insert(_list.end(), value);
        _index.emplace(value, it);
        return {it, true};
    }

    iterator erase(const_iterator it) {
        _index.erase(*it);
        return _list.erase(it);
    }

    size_type erase(T *value) {
        auto found = _index.find(value);
        if (found == _index.end()) {
            return 0;
        }
        _list.erase(found->second);
        _index.erase(found);
        return 1;
    }

    iterator find(T *value) const {
        auto found = _index.find(value);
        return found != _index.end() ? found->second : _list.end();
    }

    size_type count(T *value) const { return _index.count(value); }

    void clear() {
        _list.clear();
        _index.clear();
    }

    bool empty() const { return _list.empty(); }
    size_type size() const { return _list.size(); }

    const_iterator begin() const { return _list.begin(); }
    const_iterator end() const { return _list.end(); }

    T *front() const { return _list.front(); }
    T *back() const { return _list.back(); }

private:
    List _list;
    std::unordered_map<T *, typename List::iterator> _index;
};

} // namespace scene

} // namespace reone
