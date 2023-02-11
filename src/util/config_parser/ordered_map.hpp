//
//  Hashmap wrapper where we can iterate through
//  items in insertion-order.
//

#pragma once

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <map>
#include <vector>

template <typename T, typename V>
struct OrderedMap {
    using Map = std::map<T, V>;
    Map m_;

    // Array of all map keys in `insert-order`
    std::vector<T> keys_;

    OrderedMap() {
        m_.clear();
        keys_.clear();
    };

    OrderedMap(const OrderedMap& other) = default;
    OrderedMap(OrderedMap&& other) = default;

    OrderedMap(const std::initializer_list<std::pair<T, V>> kw_args) {
        for (auto& [key, value] : kw_args) {
            auto pair = std::make_pair(key, value);
            insert(pair);
        }
    }

    void
    insert(std::pair<T, V>& args) {
        auto& key = args.first;
        if (std::find(std::begin(keys_), std::end(keys_), key) == std::end(keys_)) {
            keys_.push_back(key);
        }
        m_.insert(args);
    }

    void
    insert(const T& key, V value) {
        auto pair = std::make_pair(key, value);
        insert(pair);
    }

    bool
    remove(const T& key) {
        if (contains(key)) {
            keys_.erase(std::remove(keys_.begin(), keys_.end(), key), keys_.end());
            m_.erase(key);
            return true;
        }
        return false;
    }

    OrderedMap&
    operator=(const OrderedMap& other) {
        this->m_ = other.m_;
        this->keys_ = other.keys_;
        return *this;
    }

    V&
    operator[](const T& key) {
        if (std::find(std::begin(keys_), std::end(keys_), key) == std::end(keys_)) {
            keys_.push_back(key);
        }

        V& v = m_[key];
        return v;
    }

    V&
    operator[](T&& key) {
        if (std::find(std::begin(keys_), std::end(keys_), key) == std::end(keys_)) {
            keys_.push_back(key);
        }

        V& v = m_[key];
        return v;
    }

    std::size_t
    size() {
        return m_.size();
    }

    bool
    contains(const T& s) {
        return m_.contains(s);
    }

    void
    for_each(std::function<void(T, V&)> cb) {
        for (auto& k : keys_) {
            cb(k, m_[k]);
        }
    }
};