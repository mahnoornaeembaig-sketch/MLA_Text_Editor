#ifndef EDITOR_H
#define EDITOR_H

#include <string>
#include <cstddef>

#include "Document.h"
#include "UndoManager.h"
#include "FileManager.h"
#include "TextProcessor.h"
#include "EditOperation.h"
#include "DynamicArray.h"
#include "Match.h"

class Editor
{
private:
    // Main components
    Document document;
    UndoManager undoManager;
    FileManager fileManager;

    // Current file information
    std::string currentFilename;
    bool modified;

    // Used to group operations that must undo/redo as ONE step
    // (Replace All, cut, paste, typing over a selection ...)
    std::size_t nextGroupId;
    std::size_t activeGroup = 0;   // id used by beginGroup()..endGroup()
    int groupDepth = 0;            // beginGroup() calls may be nested

    // Records an operation; joins the active group, if there is one.
    void recordOp(EditOperation op);

    // Checks whether unsaved changes should be saved or discarded
    bool confirmSaveIfModified();

public:
    // Constructor
    Editor();

    // Get document for Menu
    Document &getDocument();

    // Document and file operations
    bool newDocument();
    bool newDocument(const std::string &filename);
    bool open(const std::string &filename);
    bool save();
    bool saveAs(const std::string &filename);

    // Editing operations
    bool insert(int line, int col, const std::string &text);
    bool splitLine(int line, int col);
    bool joinLine(int line);
    bool remove(int line, int col, int length);

    bool insertBlockAtEnd(const std::string& text, bool hasInput);

    // Everything recorded between beginGroup() and the matching
    // endGroup() is undone / redone together. Calls may be nested.
    void beginGroup();
    void endGroup();

    // Insert typed text. Consecutive typing in one word becomes a
    // single undo step instead of one step per character.
    bool typeText(int line, int col, const std::string &text);

    // Where the last undo/redo changed the document (0-based).
    int getLastEditLine() const;
    int getLastEditColumn() const;
    // Search and replace
    bool search(const std::string &searchText,
                DynamicArray<Match> &results);

    bool replace(const Match &position,
                 const std::string &searchText,
                 const std::string &replacementText);

    bool replaceAll(const std::string &searchText,
                    const std::string &replacementText);

    // Undo
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    // File status
    bool hasFilename() const;

    // Information and exit
    void showInfo() const;
    bool exit();

};

#endif