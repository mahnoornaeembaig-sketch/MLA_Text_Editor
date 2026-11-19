#ifndef GUI_CONTROLLER_H
#define GUI_CONTROLLER_H

#include "Editor.h"
#include <string>

class GUIController
{
private:
    Editor* editor;

public:
    GUIController();
    ~GUIController();

    GUIController(const GUIController&) = delete;
    GUIController& operator=(const GUIController&) = delete;

    // Document operations
    void startNewDocument();
    bool openFile(const std::string& filename);
    bool saveFile();
    bool saveAsFile(const std::string& filename);
    bool hasFilename() const;

    // Editing operations (GUI positions are 1-based)
    bool insertText(int line, int column,
                    const std::string& text);
    bool insertNewLine();
    bool deleteText(int line, int column, int length);

    bool splitLine(int line, int column);
    bool joinLine(int line);

    // Typing that merges into the previous undo step (see Editor).
    bool typeText(int line, int column, const std::string& text);

    // Group several edits into ONE undo/redo step. May be nested.
    void beginGroup();
    void endGroup();

    // Where the last undo/redo changed the document (0-based).
    int lastEditLine() const;
    int lastEditColumn() const;

    // Undo / Redo
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    // Search and Replace
    bool searchText(const std::string& text,
                    DynamicArray<Match>& results);

    bool replaceOne(const Match& position,
                    const std::string& searchText,
                    const std::string& replacementText);

    bool replaceAllText(const std::string& searchText,
                        const std::string& replacementText);

    // Document display and information
    std::string getText() const;
    int getLineCount() const;
    int getCharCount() const;
};

#endif
