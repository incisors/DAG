/**
 * @file value.h
 * @brief Type-erased, immutable, zero-copy data holder with small-buffer
 *        optimization (SBO).
 *
 * A Value is the unit of data that flows along graph edges. Small,
 * trivially-copyable types (int, double, small structs -- anything <= 16 bytes)
 * are stored inline with no heap allocation; copying such a Value is a memcpy.
 * Larger or non-trivial types fall back to a shared_ptr, so copying them stays
 * O(1) (a refcount bump) -- fan-out never deep-copies a big payload.
 *
 * Layout note: the shared_ptr and the inline buffer coexist (not a union), and
 * the inline buffer only ever holds trivially-copyable/destructible types, so
 * the compiler-generated copy/move/destructor are all correct -- no hand-rolled
 * lifetime management. The cost is a slightly larger Value; the win is dropping
 * one heap allocation per scalar value, which dominates at high throughput.
 *
 * Type erasure is via a std::type_info pointer + static_cast (no dynamic_cast).
 */
#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <stdexcept>
#include <utility>

namespace dag {

/// Thrown when Value::as<T> is called with the wrong type or on an empty Value.
class BadValueCast : public std::runtime_error {
public:
    explicit BadValueCast(const std::string& what) : std::runtime_error(what) {}
};

class Value {
public:
    Value() = default;

    /// Construct a Value owning a copy/move of @p v.
    template <class T>
    static Value make(T v) {
        Value out;
        out.type_ = &typeid(T);
        if (inlineable<T>()) {
            ::new (static_cast<void*>(out.buf_)) T(std::move(v));
            out.inline_ = true;
        } else {
            out.heap_ = std::make_shared<const T>(std::move(v));
        }
        return out;
    }

    bool empty() const noexcept { return type_ == nullptr; }

    const std::type_info& type() const noexcept {
        return type_ ? *type_ : typeid(void);
    }

    /// True if the stored value is exactly of type T.
    template <class T>
    bool is() const noexcept {
        return type_ != nullptr && *type_ == typeid(T);
    }

    /// Borrow the stored value as T (no copy). Throws BadValueCast on mismatch.
    template <class T>
    const T& as() const {
        if (!type_) {
            throw BadValueCast("dag::Value::as<T>() called on an empty Value");
        }
        if (*type_ != typeid(T)) {
            throw BadValueCast(std::string("dag::Value type mismatch: stored '") +
                               type_->name() + "', requested '" + typeid(T).name() + "'");
        }
        if (inline_) {
            return *std::launder(reinterpret_cast<const T*>(buf_));
        }
        return *static_cast<const T*>(heap_.get());
    }

private:
    static constexpr std::size_t kBuf = 16;

    template <class T>
    static constexpr bool inlineable() {
        return std::is_trivially_copyable<T>::value && sizeof(T) <= kBuf &&
               alignof(T) <= alignof(std::max_align_t);
    }

    // Raw inline storage (only ever holds trivially-copyable Ts -> no explicit
    // construction/destruction needed; memberwise copy/move is a valid copy).
    alignas(std::max_align_t) unsigned char buf_[kBuf];
    std::shared_ptr<const void> heap_;     // used iff !inline_ and !empty
    const std::type_info* type_ = nullptr; // null iff empty
    bool inline_ = false;
};

/// Convenience free function: dag::make_value(42).
template <class T>
inline Value make_value(T v) {
    return Value::make<T>(std::move(v));
}

}  // namespace dag
