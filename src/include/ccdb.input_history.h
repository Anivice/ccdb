#ifndef CCDB_INPUT_HISTORY_H
#define CCDB_INPUT_HISTORY_H

#include <map>
#include <string>
#include <vector>

namespace ccdb
{
    // Owned by the input watcher; entries survive reopening the '/' prompt.
    class InputHistory
    {
    public:
        void begin()
        {
            position_ = entries_.size();
            edits_.clear();
        }

        void submit(const std::u32string & line)
        {
            if (!line.empty()) entries_.push_back(line);
            begin();
        }

        bool previous(std::u32string & line)
        {
            if (position_ == 0) return false;
            return move_to(position_ - 1, line);
        }

        bool next(std::u32string & line)
        {
            if (position_ == entries_.size()) return false;
            return move_to(position_ + 1, line);
        }

    private:
        bool move_to(const std::size_t position, std::u32string & line)
        {
            // Preserve the draft and edits while browsing, without changing
            // previously submitted entries. A new prompt discards these edits.
            edits_[position_] = line;
            position_ = position;
            if (const auto it = edits_.find(position_); it != edits_.end()) {
                line = it->second;
            } else {
                line = entries_[position_];
            }
            return true;
        }

        std::vector<std::u32string> entries_;
        std::map<std::size_t, std::u32string> edits_;
        std::size_t position_ = 0;
    };
}

#endif
