// ut_mikind.h - the KIND table: the catalogue's monster infrequents, looked up by record in any
// spelling (case, '/' or '\\'), with their catalogue name and item level, and the refusal's words
// naming the stored roll. No game, no hook, no log: the table is built once from catalogue.bin and
// read lock-free afterwards, so a suite links this unit over a catalogue.bin of its own.
#pragma once

#include <stddef.h>

#include <string>

namespace ut {

struct UtMiTable;

// The table's key: lower case, '\\' -> '/'. `out` is always terminated (truncated at `cap`).
void utMiFold(const char* in, char* out, size_t cap);

// Reads catalogue.bin at `path` and keeps its monster infrequents (the catalogue's KIND bit),
// sorted by folded record. Null (and the reason in *err) when the file cannot be read. The table
// lives while its game is in force; a game switch retires it (ut_live.cpp, ut_retire.h).
UtMiTable* utMiLoad(const char* path, std::string* err);

// Its size in bytes (the rows and their strings; 0 for no table), and its free (null: nothing).
size_t utMiBytes(const UtMiTable* t);
void utMiFree(UtMiTable* t);

// The records the table holds; -1 for no table.
int utMiCount(const UtMiTable* t);

// 1 = a monster infrequent, 0 = not one (or not a catalogue record), -1 = no table.
int utMiKind(const UtMiTable* t, const char* record);

// A monster infrequent's catalogue name and item level; false (name "", level 0) = not one.
bool utMiFacts(const UtMiTable* t, const char* record, char* name, size_t nameCap, int* level);

// kUtDepRefuseHave of a monster infrequent: the reason naming the roll the collection holds
// (storeRollOf, the newest row: record name, item level, prefix, suffix, seed; "roll not readable"
// without a row). "" for every other record. Takes the journal's lock: never inside an SEH frame.
const char* utMiRollWhy(const UtMiTable* t, const char* record, char* buf, size_t cap);

}  // namespace ut
