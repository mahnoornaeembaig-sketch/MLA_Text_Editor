#ifndef UNDOMANAGER_H
#define UNDOMANAGER_H

#include <cstddef>
#include <string>

#include "Stack.h"
#include "EditOperation.h"
#include "Document.h"

class UndoManager {
private:
    Stack<EditOperation> history;
    Stack<EditOperation> redoHistory;

    std::size_t limit;

    // Position affected by the most recent undo/redo step (0-based).
    // The GUI uses it to put the caret where the change happened.
    std::size_t lastLine;
    std::size_t lastCol;

    // Drops the oldest whole groups until the limit is respected.
    // A group is never split, and the newest group is never dropped.
    void trimHistory();
    void rememberPosition(const EditOperation& op, bool undoing);

    bool applyInverse(const EditOperation& op, Document& doc);
    bool applyForward(const EditOperation& op, Document& doc);

public:
    UndoManager();

    void record(const EditOperation& op);

    // Extends the previous typing run (same line, directly adjacent,
    // not ended by whitespace) instead of creating a new undo step.
    // Call it AFTER the document was changed. Returns false when the
    // new text could not be merged and must be recorded normally.
    bool mergeTyping(std::size_t line, std::size_t col,
                     const std::string& text);

    bool undo(Document& doc);
    bool redo(Document& doc);

    bool canUndo() const;
    bool canRedo() const;

    void clear();

    bool setLimit(std::size_t newLimit);

    std::size_t getLimit() const;
    std::size_t getSize() const;
    std::size_t getLastLine() const;
    std::size_t getLastCol() const;
};

#endif
