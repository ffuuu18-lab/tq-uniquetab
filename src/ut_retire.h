// ut_retire.h - the two-deep retire list. A table that is published by pointer and read without a
// lock (the group, KIND and button-page tables, the buttons file's objects) is not freed when a
// newer one replaces it: it is kept through the next two replacements of its kind, then freed by
// the thread that replaces them. A reader takes the published pointer and uses it within one call
// (a frame, a key event, one resolve); two replacements are a game switch or a file change apart,
// so no reader is left on a table when it is freed. Pure: test_viewgate checks the lifetimes.
#pragma once

#include <stddef.h>

namespace ut {

const int kUtRetireDepth = 2;

template <class T>
struct UtRetired {
    T* table = nullptr;
    unsigned when = 0;   // the switch (or the replacement) it was retired at
    size_t bytes = 0;    // its size, for the log and the memory lines
};

template <class T>
struct UtRetireList {
    UtRetired<T> held[kUtRetireDepth];
    int count = 0;

    bool holds(const T* t) const {
        for (int i = 0; i < count; ++i)
            if (held[i].table == t) return true;
        return false;
    }

    // `t`, retired at `when`, is kept. Returns the table that falls out - the one retired two
    // retirements before - for the caller to free now (its `table` is null when none does). A null
    // `t`, or one the list holds already, changes nothing.
    UtRetired<T> retire(T* t, unsigned when, size_t bytes) {
        UtRetired<T> out;
        if (!t || holds(t)) return out;
        if (count == kUtRetireDepth) {
            out = held[0];
            for (int i = 1; i < kUtRetireDepth; ++i) held[i - 1] = held[i];
            --count;
        }
        held[count].table = t;
        held[count].when = when;
        held[count].bytes = bytes;
        ++count;
        return out;
    }

    size_t heldBytes() const {
        size_t b = 0;
        for (int i = 0; i < count; ++i) b += held[i].bytes;
        return b;
    }
};

}  // namespace ut
