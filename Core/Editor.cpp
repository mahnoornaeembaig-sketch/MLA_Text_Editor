#include "Editor.h"
#include <iostream>
#include <limits>

// Constructor
Editor::Editor()
    : currentFilename(""),
      modified(false),
      nextGroupId(1)
{
}

// Record an operation, joining the active group (if any).
// The group id is allocated lazily, so a group that ends up holding a
// single operation costs nothing extra.
void Editor::recordOp(EditOperation op)
{
    if (op.groupId == 0 && groupDepth > 0)
    {
        if (activeGroup == 0)
        {
            activeGroup = nextGroupId++;

            if (nextGroupId == 0)
                nextGroupId = 1;
        }

        op.groupId = activeGroup;
    }

    undoManager.record(op);
}

void Editor::beginGroup()
{
    if (groupDepth++ == 0)
        activeGroup = 0;
}

void Editor::endGroup()
{
    if (groupDepth > 0 && --groupDepth == 0)
        activeGroup = 0;
}

// Typing: extends the previous typing run when possible.
bool Editor::typeText(int line, int col, const std::string& text)
{
    if (text.empty())
        return false;

    if (!document.insertText(line, col, text))
        return false;

    modified = true;

    if (groupDepth == 0 &&
        undoManager.mergeTyping(static_cast<std::size_t>(line),
                                static_cast<std::size_t>(col), text))
    {
        return true;
    }

    EditOperation op;
    op.type = OperationType::INSERT_TEXT;
    op.line = static_cast<std::size_t>(line);
    op.col = static_cast<std::size_t>(col);
    op.newText = text;

    recordOp(op);
    return true;
}

int Editor::getLastEditLine() const
{
    return static_cast<int>(undoManager.getLastLine());
}

int Editor::getLastEditColumn() const
{
    return static_cast<int>(undoManager.getLastCol());
}

// Get document for Menu
Document& Editor::getDocument()
{
    return document;
}

// Ask the user what to do with unsaved changes
bool Editor::confirmSaveIfModified()
{
    if (!modified)
        return true;

    char choice;

    std::cout << "\nYou have unsaved changes.\n";
    std::cout << "Save changes? (y = yes, n = no, c = cancel): ";
    std::cin >> choice;

    std::cin.ignore(
        std::numeric_limits<std::streamsize>::max(), '\n'
    );

    if (choice == 'y' || choice == 'Y')
    {
        if (currentFilename.empty())
        {
            std::string filename;

            std::cout << "Enter filename: ";
            std::getline(std::cin, filename);

            return saveAs(filename);
        }

        return save();
    }
else if (choice == 'n' || choice == 'N')
{
    modified = false;
    return true;
}

    // Cancel the requested operation
    return false;
}

// Create a new empty document
bool Editor::newDocument()
{
    if (!confirmSaveIfModified())
        return false;

    document.clear();
    undoManager.clear();

    currentFilename.clear();
    modified = false;

    return true;
}
// Open an existing file
bool Editor::open(const std::string& filename)
{
    if (filename.empty())
        return false;

    if (!confirmSaveIfModified())
        return false;

    if (!fileManager.openFile(document, filename))
        return false;

    currentFilename = filename;
    modified = false;

    // Clear previous document's undo history
    undoManager.clear();

    return true;
}

// Save changes to the current file
bool Editor::save()
{
    if (currentFilename.empty())
        return false;

    if (!fileManager.saveFile(document, currentFilename))
        return false;

    modified = false;

    return true;
}

// Save document using a new filename
bool Editor::saveAs(const std::string& filename)
{
    if (filename.empty())
        return false;

    if (!fileManager.saveFile(document, filename))
        return false;

    currentFilename = filename;
    modified = false;

    return true;
}

// Insert text into the document
bool Editor::insert(int line, int col, const std::string& text)
{
    if (text.empty())
        return false;

    // Step 1: Change the document
    if (!document.insertText(line, col, text))
        return false;

    // Step 2: Record the operation
    EditOperation op;

    op.type = OperationType::INSERT_TEXT;
    op.line = static_cast<std::size_t>(line);
    op.col = static_cast<std::size_t>(col);
    op.newText = text;

    recordOp(op);

    // Step 3: Mark document as modified
    modified = true;

    return true;
}
// Insert text automatically at the end of the document
bool Editor::insertBlockAtEnd(const std::string& text, bool hasInput)
{
    if (!hasInput)
        return false;

    std::size_t groupId = nextGroupId++;
    bool inserted = false;

    // Check whether the document initially contains
    // only its default empty line.
    bool useInitialLine = document.isEmpty();

    std::size_t start = 0;

    while (true)
    {
        // Find the next newline without using string::find().
        std::size_t end = start;

        while (end < text.length() && text[end] != '\n')
        {
            end++;
        }

        std::string lineText = text.substr(start, end - start);

        EditOperation op;
        op.groupId = groupId;

        if (useInitialLine)
        {
            // Use the document's existing empty line.
            if (!lineText.empty())
            {
                if (!document.setLine(0, lineText))
                    return false;

                op.type = OperationType::INSERT_TEXT;
                op.line = 0;
                op.col = 0;
                op.newText = lineText;

                undoManager.record(op);
                inserted = true;
            }

            useInitialLine = false;
        }
        else
        {
            // Add a new line at the end.
            int newLineIndex = document.getLineCount();

            if (!document.insertLine(newLineIndex, lineText))
                return false;

            op.type = OperationType::INSERT_LINE;
            op.line = static_cast<std::size_t>(newLineIndex);
            // Needed so Redo can put the text back.
            op.newText = lineText;

            undoManager.record(op);
            inserted = true;
        }

        if (end == text.length())
            break;

        start = end + 1;
    }

    if (inserted)
        modified = true;

    return inserted;
}
// Remove a range of text from the document
bool Editor::remove(int line, int col, int length)
{
    if (length <= 0)
        return false;

    const Line* current = document.getLine(line);

    if (current == nullptr ||
        col < 0 ||
        col > static_cast<int>(current->text.length()) ||
        length > static_cast<int>(current->text.length()) - col)
    {
        return false;
    }

    // Save deleted text for undo
    std::string deletedText =
        current->text.substr(
            static_cast<std::size_t>(col),
            static_cast<std::size_t>(length)
        );

    // Step 1: Change the document
    if (!document.deleteRange(line, col, length))
        return false;

    // Step 2: Record the operation
    EditOperation op;

    op.type = OperationType::DELETE_TEXT;
    op.line = static_cast<std::size_t>(line);
    op.col = static_cast<std::size_t>(col);
    op.oldText = deletedText;

    recordOp(op);

    // Step 3: Mark document as modified
    modified = true;

    return true;
}

// Search for all occurrences of text
bool Editor::search(
    const std::string& searchText,
    DynamicArray<Match>& results)
{
    return TextProcessor::searchAll(
        document, searchText, results
    );
}

// Replace one occurrence
bool Editor::replace(
    const Match& position,
    const std::string& searchText,
    const std::string& replacementText)
{
    if (searchText.empty())
        return false;

    if (searchText == replacementText)
        return false;

    ReplacementRecord record;

    // Step 1: Change the document
    if (!TextProcessor::replaceOccurrence(
            document,
            position,
            searchText,
            replacementText,
            record))
    {
        return false;
    }

    // Step 2: Record the operation
    EditOperation op;

    op.type = OperationType::REPLACE;
    op.line = static_cast<std::size_t>(record.position.line);
    op.col = static_cast<std::size_t>(record.position.col);
    op.oldText = record.oldText;
    op.newText = record.newText;

    recordOp(op);

    // Step 3: Mark document as modified
    modified = true;

    return true;
}

// Replace all occurrences
bool Editor::replaceAll(
    const std::string& searchText,
    const std::string& replacementText)
{
    if (searchText.empty() || searchText == replacementText)
        return false;

    DynamicArray<ReplacementRecord> records;

    // Step 1: TextProcessor changes the document
    if (!TextProcessor::replaceAll(
            document, searchText, replacementText, records))
    {
        return false;
    }

    // No occurrences were replaced
    if (records.getSize() == 0)
        return true;

    // One group ID makes all replacements undo together
    std::size_t group = nextGroupId++;

    if (nextGroupId == 0)
        nextGroupId = 1;

    // Step 2: Record every replacement
    for (int i = 0; i < records.getSize(); i++)
    {
        ReplacementRecord* record = records.getAt(i);

        if (record == nullptr)
            continue;

        EditOperation op;

        op.type = OperationType::REPLACE;
        op.line = static_cast<std::size_t>(
            record->position.line
        );
        op.col = static_cast<std::size_t>(
            record->position.col
        );
        op.oldText = record->oldText;
        op.newText = record->newText;
        op.groupId = group;

        undoManager.record(op);
    }

    // Step 3: Mark document as modified
    modified = true;

    return true;
}

// Undo the last editing operation
bool Editor::undo()
{
    if (!undoManager.undo(document))
        return false;

    modified = true;

    return true;
}

// Check whether undo is available
bool Editor::canUndo() const
{
    return undoManager.getSize() > 0;
}

// Check whether document has a filename
bool Editor::hasFilename() const
{
    return !currentFilename.empty();
}
bool Editor::redo()
{
    if (!undoManager.redo(document))
        return false;

    modified = true;
    return true;
}

bool Editor::canRedo() const
{
    return undoManager.canRedo();
}
// Display document information
void Editor::showInfo() const
{
    std::cout << "\n===== DOCUMENT INFORMATION =====\n";

    if (currentFilename.empty())
        std::cout << "Filename: Untitled\n";
    else
        std::cout << "Filename: " << currentFilename << '\n';

    std::cout << "Total lines: "
              << document.getLineCount() << '\n';

    std::cout << "Total characters: "
              << document.getCharCount() << '\n';

    std::cout << "Modified: "
              << (modified ? "Yes" : "No") << '\n';

    std::cout << "Undo operations available: "
              << undoManager.getSize() << '\n';

    std::cout << "================================\n";
}

// Check for unsaved changes before exiting
bool Editor::exit()
{
    return confirmSaveIfModified();
}
// Split an existing line at a given character position
bool Editor::splitLine(int line, int col)
{
    if (line < 0 || col < 0)
        return false;

    // Step 1: Split the document line
    if (!document.splitLine(line, col))
        return false;

    // Step 2: Record operation for Undo/Redo
    EditOperation op;

    op.type = OperationType::SPLIT_LINE;
    op.line = static_cast<std::size_t>(line);
    op.col = static_cast<std::size_t>(col);

    recordOp(op);

    // Step 3: Mark document as modified
    modified = true;

    return true;
}

// Join the current line with the next line
bool Editor::joinLine(int line)
{
    if (line < 0)
        return false;

    const Line* first = document.getLine(line);
    const Line* second = document.getLine(line + 1);

    if (!first || !second)
        return false;

    std::string firstText = first->text;
    std::size_t joinPosition = firstText.length();

    std::string combined = firstText + second->text;

    if (!document.setLine(line, combined))
        return false;

    if (!document.deleteLine(line + 1))
    {
        document.setLine(line, firstText);
        return false;
    }

    EditOperation op;
    op.type = OperationType::JOIN_LINE;
    op.line = static_cast<std::size_t>(line);
    op.col = joinPosition;

    recordOp(op);
    modified = true;

    return true;
}