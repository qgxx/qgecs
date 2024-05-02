#ifndef __SPARSE_SET_H__
#define __SPARSE_SET_H__

#include <vector>
#include <array>
#include <memory>
#include <cassert>
#include <limits>
#include <type_traits>

template <typename T, size_t PageSize, typename = std::enable_if<std::is_integral_v<T>>>
class sparse_set final {
public:
    void add(T t) {
        density_.push_back(t);
        assure(t);
        index(t) = density_.size() - 1;
    }

    void remove(T t) {
        if (!contain(t)) return;
        auto& idx = index(t);
        if (idx == density_.size() - 1) {
            idx = null;
            density_.pop_back();
        }
        else {
            auto last = density_.back();
            index(last) = idx;
            std::swap(density_[idx], density_.back());
            idx = null;
            density_.pop_back();
        }
    }

    bool contain(T t) const {
        assert(t != null);

        auto p = page(t);
        auto o = offset(t);

        return (p < sparse_.size() && sparse_[p]->at(0) != null);
    }

    void clear() {
        density_.clear();
        sparse_.clear();
    }

    auto begin() { return density_.begin(); }
    auto end() { return density_.end(); }

private:
    std::vector<T> density_;
    std::vector<std::unique_ptr<std::array<T, PageSize>>> sparse_;
    static constexpr T null = std::numeric_limits<T>::max();

    size_t offset(T t) const { return t % PageSize; }
    size_t page(T t) const { return t / PageSize; } 
    T index(T t) const { return sparse_[page(t)]->at(offset(t)); }
    T& index(T t) { return sparse_[page(t)]->at(offset(t)); }
    void assure(T t) {
        auto p = page(t);
        if (p >= sparse_.size()) {
            for (size_t i = sparse_.size(); i <= p; i++) {
                sparse_.emplace_back(std::make_unique<std::array<T, PageSize>>());
                sparse_[i]->fill(null);
            }
        }
    } 
};

#endif // !__SPARSE_SET_H__  