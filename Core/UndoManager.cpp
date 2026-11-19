#include "UndoManager.h"
#include <string>

UndoManager::UndoManager() : limit(100), lastLine(0), lastCol(0) {
}

void UndoManager::record(const EditOperation& op) {

    if (op.type == OperationType::INSERT_TEXT &&
        op.newText.empty()) {
        return;
    }

    if (op.type == OperationType::DELETE_TEXT &&
        op.oldText.empty()) {
        return;
    }

    // A new edit clears the previous redo history.
    redoHistory.clear();

    history.push(op);
    trimHistory();
}

// MERGE TYPING INTO THE PREVIOUS UNDO STEP
bool UndoManager::mergeTyping(std::size_t line, std::size_t col,
                              const std::string& text) {
    if (text.empty() || text.find('\n') != std::string::npos) {
        return false;
    }

    EditOperation top;

    if (!history.peek(top)) {
        return false;
    }

    auto isBlank = [](char c) { return c == ' ' || c == '\t'; };

    if (top.type != OperationType::INSERT_TEXT ||
        top.groupId != 0 ||
        top.line != line ||
        top.col + top.newText.length() != col ||
        top.newText.empty() ||
        isBlank(top.newText[top.newText.length() - 1])) {
        return false;
    }

    // Replace the top entry with the extended one.
    history.pop(top);
    top.newText += text;
    history.push(top);

    // Typing is a new edit: redo history no longer applies.
    redoHistory.clear();
    return true;
}

// UNDO
bool UndoManager::undo(Document& doc) {

    EditOperation op;

    if (!history.pop(op)) {
        return false;
    }

    if (!applyInverse(op, doc)) {
        history.push(op);
        return false;
    }

    rememberPosition(op, true);
    redoHistory.push(op);

    std::size_t groupId = op.groupId;

    if (groupId == 0) {
        return true;
    }

    EditOperation next;

    while (history.peek(next) &&
           next.groupId == groupId) {

        history.pop(next);

        if (!applyInverse(next, doc)) {
            // Put it back so history stays consistent with the document.
            history.push(next);
            return false;
        }

        rememberPosition(next, true);
        redoHistory.push(next);
    }

    return true;
}

// REDO
bool UndoManager::redo(Document& doc) {

    EditOperation op;

    if (!redoHistory.pop(op)) {
        return false;
    }

    if (!applyForward(op, doc)) {
        redoHistory.push(op);
        return false;
    }

    rememberPosition(op, false);
    history.push(op);
    trimHistory();

    std::size_t groupId = op.groupId;

    if (groupId == 0) {
        return true;
    }

    EditOperation next;

    while (redoHistory.peek(next) &&
           next.groupId == groupId) {

        redoHistory.pop(next);

        if (!applyForward(next, doc)) {
            redoHistory.push(next);
            return false;
        }

        rememberPosition(next, false);
        history.push(next);
        trimHistory();
    }

    return true;
}

// CHECK UNDO
bool UndoManager::canUndo() const {
    return !history.isEmpty();
}

// CHECK REDO
bool UndoManager::canRedo() const {
    return !redoHistory.isEmpty();
}

// CLEAR BOTH STACKS
void UndoManager::clear() {
    history.clear();
    redoHistory.clear();
}

bool UndoManager::setLimit(std::size_t newLimit) {

    if (newLimit == 0) {
        return false;
    }

    limit = newLimit;
    trimHistory();

    return true;
}

std::size_t UndoManager::getLimit() const {
    return limit;
}

std::size_t UndoManager::getSize() const {
    return history.getSize();
}

std::size_t UndoManager::getLastLine() const {
    return lastLine;
}

std::size_t UndoManager::getLastCol() const {
    return lastCol;
}

// Remove the oldest WHOLE groups while over the limit.
// Never splits a group and never removes the newest group.
void UndoManager::trimHistory() {
    while (history.getSize() > limit) {
        EditOperation oldest;
        EditOperation newest;

        if (!history.peekBottom(oldest) || !history.peek(newest)) {
            return;
        }

        std::size_t gid = oldest.groupId;

        // Only one group left (it alone exceeds the limit): keep it.
        if (gid != 0 && newest.groupId == gid) {
            return;
        }

        history.removeBottom();

        if (gid == 0) {
            continue;
        }

        EditOperation next;

        while (history.peekBottom(next) && next.groupId == gid) {
            history.removeBottom();
        }
    }
}

// Where should the caret go after this step was undone / redone?
void UndoManager::rememberPosition(const EditOperation& op, bool undoing) {
    lastLine = op.line;
    lastCol = op.col;

    switch (op.type) {
    case OperationType::INSERT_TEXT:
        lastCol = undoing ? op.col : op.col + op.newText.length();
        break;

    case OperationType::DELETE_TEXT:
        // A single deletion (Backspace) leaves the caret after the
        // restored text; inside a group (cut) it goes to the start.
        if (undoing && op.groupId == 0) {
            lastCol = op.col + op.oldText.length();
        }
        break;

    case OperationType::REPLACE:
        lastCol = op.col + (undoing ? op.oldText.length()
                                    : op.newText.length());
        break;

    case OperationType::SPLIT_LINE:
        if (!undoing) {
            lastLine = op.line + 1;
            lastCol = 0;
        }
        break;

    case OperationType::JOIN_LINE:
        if (undoing) {
            lastLine = op.line + 1;
            lastCol = 0;
        }
        break;

    case OperationType::INSERT_LINE:
    case OperationType::DELETE_LINE:
        lastCol = 0;
        break;
    }
}

// APPLY INVERSE OPERATION
bool UndoManager::applyInverse(
    const EditOperation& op,
    Document& doc) {

    int line = static_cast<int>(op.line);
    int col = static_cast<int>(op.col);

    switch (op.type) {

    case OperationType::INSERT_TEXT:
        return doc.deleteRange(
            line, col,
            static_cast<int>(op.newText.length()));

    case OperationType::DELETE_TEXT:
        return doc.insertText(line, col, op.oldText);

    case OperationType::REPLACE:

        if (!op.newText.empty()) {
            if (!doc.deleteRange(
                line, col,
                static_cast<int>(op.newText.length()))) {
                return false;
            }
        }

        return doc.insertText(line, col, op.oldText);

    case OperationType::INSERT_LINE:
        return doc.deleteLine(line);

    case OperationType::DELETE_LINE:
        return doc.insertLine(line, op.oldText);

    case OperationType::SPLIT_LINE: {

        const Line* first = doc.getLine(line);
        const Line* second = doc.getLine(line + 1);

        if (first == nullptr || second == nullptr) {
            return false;
        }

        std::string merged =
            first->text + second->text;

        if (!doc.setLine(line, merged)) {
            return false;
        }

        return doc.deleteLine(line + 1);
    }

    // Undo JOIN_LINE by splitting at the original boundary.
    case OperationType::JOIN_LINE:
        return doc.splitLine(line, col);

    }

    return false;
}

// APPLY FORWARD OPERATION
bool UndoManager::applyForward(
    const EditOperation& op,
    Document& doc) {

    int line = static_cast<int>(op.line);
    int col = static_cast<int>(op.col);

    switch (op.type) {

    case OperationType::INSERT_TEXT:
        return doc.insertText(line, col, op.newText);

    case OperationType::DELETE_TEXT:
        return doc.deleteRange(
            line, col,
            static_cast<int>(op.oldText.length()));

    case OperationType::REPLACE:

        if (!op.oldText.empty()) {
            if (!doc.deleteRange(
                line, col,
                static_cast<int>(op.oldText.length()))) {
                return false;
            }
        }

        return doc.insertText(line, col, op.newText);

    case OperationType::INSERT_LINE:
        return doc.insertLine(line, op.newText);

    case OperationType::DELETE_LINE:
        return doc.deleteLine(line);

    case OperationType::SPLIT_LINE:
        return doc.splitLine(line, col);

    // Redo JOIN_LINE by combining the current line with the next.
    case OperationType::JOIN_LINE: {

        const Line* first = doc.getLine(line);
        const Line* second = doc.getLine(line + 1);

        if (first == nullptr || second == nullptr) {
            return false;
        }

        std::string combined =
            first->text + second->text;

        if (!doc.setLine(line, combined)) {
            return false;
        }

        return doc.deleteLine(line + 1);
    }

    }

    return false;
}
