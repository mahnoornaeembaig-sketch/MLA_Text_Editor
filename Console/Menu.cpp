#include "Menu.h"
#include "InputHelper.h"

#include <iostream>
#include <string>
#include <limits>
#include <cctype>

using namespace std;

// ============================================================
// ADDITIONAL EDITING FEATURES
// Terminal equivalents of the features the GUI offers:
//   - insert text at any line / column
//   - split line (Enter) and join lines (Backspace / Delete)
//   - multi-line range delete
//   - copy / cut / paste (internal clipboard)
//   - find next with wrap-around
// These helpers use only the public Editor interface, so the
// existing Menu members and Menu.h are left unchanged.
// ============================================================
namespace
{
    // Terminal clipboard (the GUI uses the Windows clipboard)
    string clipboardText;

    // State for "Find Next" (the GUI keeps this in the caret position)
    string lastSearchText;
    string lastFindDocumentText;
    int nextFindLine = 0;
    int nextFindCol = 0;

    // Offset of the first character of a 0-based line inside the
    // full document text, or npos if the line does not exist.
    size_t lineStartOffset(const string& text, int lineIndex)
    {
        size_t pos = 0;

        for (int i = 0; i < lineIndex; i++)
        {
            size_t nl = text.find('\n', pos);

            if (nl == string::npos)
                return string::npos;

            pos = nl + 1;
        }

        return pos;
    }

    // Length of a 0-based line (0 if the line does not exist).
    int lineLengthAt(const string& text, int lineIndex)
    {
        size_t start = lineStartOffset(text, lineIndex);

        if (start == string::npos)
            return 0;

        size_t end = text.find('\n', start);

        if (end == string::npos)
            end = text.length();

        return static_cast<int>(end - start);
    }

    // Content of a 0-based line.
    string lineTextAt(const string& text, int lineIndex)
    {
        size_t start = lineStartOffset(text, lineIndex);

        if (start == string::npos)
            return "";

        return text.substr(
            start,
            static_cast<size_t>(lineLengthAt(text, lineIndex))
        );
    }

    // A position is valid when the line exists and the column is
    // between the first character and just after the last one.
    bool isValidPosition(
        Editor& editor,
        const string& text,
        int line,
        int col)
    {
        int lineCount = editor.getDocument().getLineCount();

        if (lineCount < 1)
            lineCount = 1;

        if (line < 0 || line >= lineCount)
            return false;

        if (col < 0 || col > lineLengthAt(text, line))
            return false;

        return true;
    }

    // Reads a 1-based line/column and converts it to 0-based.
    bool readPosition(
        Editor& editor,
        const string& text,
        const char* linePrompt,
        const char* colPrompt,
        int& line,
        int& col)
    {
        line = InputHelper::readInt(linePrompt) - 1;
        col = InputHelper::readInt(colPrompt) - 1;

        if (!isValidPosition(editor, text, line, col))
        {
            cout << "Invalid position. Check the line and column numbers.\n";
            return false;
        }

        return true;
    }

    // Reads the range to copy / cut. Either the whole document or a
    // start and end position (end is exclusive). All values 0-based.
    bool readSelection(
        Editor& editor,
        const string& text,
        int& startLine,
        int& startCol,
        int& endLine,
        int& endCol)
    {
        int lineCount = editor.getDocument().getLineCount();

        if (lineCount < 1)
            lineCount = 1;

        string all = InputHelper::readLine(
            "Select the entire document? (y/n): "
        );

        if (!all.empty() && (all[0] == 'y' || all[0] == 'Y'))
        {
            startLine = 0;
            startCol = 0;
            endLine = lineCount - 1;
            endCol = lineLengthAt(text, endLine);

            return true;
        }

        if (!readPosition(
                editor, text,
                "Enter start line number: ",
                "Enter start column number: ",
                startLine, startCol))
            return false;

        if (!readPosition(
                editor, text,
                "Enter end line number: ",
                "Enter end column number (selection stops before it): ",
                endLine, endCol))
            return false;

        if (endLine < startLine ||
            (endLine == startLine && endCol <= startCol))
        {
            cout << "The end position must come after the start position.\n";
            return false;
        }

        return true;
    }

    // Text between two positions (end exclusive), '\n' between lines.
    string extractRange(
        const string& text,
        int startLine,
        int startCol,
        int endLine,
        int endCol)
    {
        size_t a = lineStartOffset(text, startLine);
        size_t b = lineStartOffset(text, endLine);

        if (a == string::npos || b == string::npos)
            return "";

        a += static_cast<size_t>(startCol);
        b += static_cast<size_t>(endCol);

        if (b > text.length())
            b = text.length();

        if (a >= b)
            return "";

        return text.substr(a, b - a);
    }

    // Inserts text (which may contain line breaks) at a 0-based
    // position. Same algorithm as the GUI paste: insert each line
    // chunk, then split the line at every line break.
    bool insertTextAt(
        Editor& editor,
        int line,
        int col,
        const string& rawText,
        int& endLine,
        int& endCol)
    {
        // CR-LF and lone CR become LF
        string text;

        for (size_t i = 0; i < rawText.length(); i++)
        {
            if (rawText[i] == '\r')
            {
                if (i + 1 < rawText.length() && rawText[i + 1] == '\n')
                    i++;

                text += '\n';
            }
            else
            {
                text += rawText[i];
            }
        }

        int currentLine = line;
        int currentCol = col;
        size_t pos = 0;

        while (true)
        {
            size_t nl = text.find('\n', pos);

            string chunk = (nl == string::npos)
                ? text.substr(pos)
                : text.substr(pos, nl - pos);

            if (!chunk.empty())
            {
                if (!editor.insert(currentLine, currentCol, chunk))
                    return false;

                currentCol += static_cast<int>(chunk.length());
            }

            if (nl == string::npos)
                break;

            if (!editor.splitLine(currentLine, currentCol))
                return false;

            currentLine++;
            currentCol = 0;
            pos = nl + 1;
        }

        endLine = currentLine;
        endCol = currentCol;

        return true;
    }

    // Deletes everything from (startLine, startCol) up to, but not
    // including, (endLine, endCol). Same algorithm as the GUI uses
    // for a selection that spans lines. All values 0-based.
    bool deleteRange(
        Editor& editor,
        int startLine,
        int startCol,
        int endLine,
        int endCol)
    {
        // Line lengths are read from the document as it was before
        // any change, because the lines shift while deleting.
        string original = editor.getDocument().getText();

        if (startLine == endLine)
        {
            int length = endCol - startCol;

            if (length <= 0)
                return false;

            return editor.remove(startLine, startCol, length);
        }

        // 1. Remove the tail of the first line.
        int startLength = lineLengthAt(original, startLine);
        int suffixLength = startLength - startCol;

        if (suffixLength > 0 &&
            !editor.remove(startLine, startCol, suffixLength))
            return false;

        // 2. Empty each middle line and join it into the first line.
        for (int originalLine = startLine + 1;
             originalLine < endLine;
             originalLine++)
        {
            int length = lineLengthAt(original, originalLine);

            if (length > 0 &&
                !editor.remove(startLine + 1, 0, length))
                return false;

            if (!editor.joinLine(startLine))
                return false;
        }

        // 3. Remove the selected head of the last line and join the
        //    rest of it onto the first line.
        if (endCol > 0 &&
            !editor.remove(startLine + 1, 0, endCol))
            return false;

        return editor.joinLine(startLine);
    }

    // ---------------- Insert Text at Position ----------------
    void handleInsertAt(Editor& editor)
    {
        string text = editor.getDocument().getText();

        int line = 0;
        int col = 0;

        if (!readPosition(
                editor, text,
                "Enter line number: ",
                "Enter column number: ",
                line, col))
            return;

        cout << "\n------ Writing Mode (insert at Line "
             << line + 1 << ", Column " << col + 1 << ") ------\n";
        cout << "Enter your text below.\n";
        cout << "Type :done on a separate line to finish.\n\n";

        string block;
        bool firstLine = true;

        while (true)
        {
            string input = InputHelper::readLine("> ");

            if (input == ":done")
                break;

            if (!firstLine)
                block += '\n';

            block += input;
            firstLine = false;
        }

        if (block.empty())
        {
            cout << "\nNo text was inserted.\n";
            return;
        }

        int endLine = 0;
        int endCol = 0;

        if (insertTextAt(editor, line, col, block, endLine, endCol))
        {
            cout << "\nText inserted successfully! Cursor is now at Line "
                 << endLine + 1 << ", Column " << endCol + 1 << ".\n";
        }
        else
        {
            cout << "\nUnable to insert text. Some of it may already be "
                    "in the document; use Undo to revert.\n";
        }
    }

    // ---------------- Split Line (Enter key) ----------------
    void handleSplitLine(Editor& editor)
    {
        string text = editor.getDocument().getText();

        int line = 0;
        int col = 0;

        if (!readPosition(
                editor, text,
                "Enter line number to split: ",
                "Enter column where the new line should begin: ",
                line, col))
            return;

        if (editor.splitLine(line, col))
        {
            cout << "Line split successfully. The text from Column "
                 << col + 1 << " now starts Line " << line + 2 << ".\n";
        }
        else
        {
            cout << "Unable to split line.\n";
        }
    }

    // ---------------- Join Lines (Backspace / Delete key) ----------------
    void handleJoinLine(Editor& editor)
    {
        int line = InputHelper::readInt(
            "Enter line number to join with the line below it: "
        );

        line--;

        if (editor.joinLine(line))
        {
            cout << "Lines joined successfully.\n";
        }
        else
        {
            cout << "Unable to join lines. A line below must exist.\n";
        }
    }

    // ---------------- Delete Line ----------------
    void handleDeleteLine(Editor& editor)
    {
        int line = InputHelper::readInt("Enter line number to delete: ") - 1;
        int lineCount = editor.getDocument().getLineCount();

        if (line < 0 || line >= lineCount)
        {
            cout << "Invalid line number.\n";
            return;
        }

        const Line* target = editor.getDocument().getLine(line);

        if (target == nullptr)
        {
            cout << "Unable to access the selected line.\n";
            return;
        }

        int length = static_cast<int>(target->text.length());

        // The document must always contain at least one line.
        if (lineCount == 1)
        {
            if (length == 0 || editor.remove(line, 0, length))
                cout << "Line deleted successfully. The document is now empty.\n";
            else
                cout << "Unable to delete the line.\n";

            return;
        }

        // Remove the line's contents, then remove its empty line node
        // by joining it with the neighbouring line.
        if (length > 0 && !editor.remove(line, 0, length))
        {
            cout << "Unable to delete the line's text.\n";
            return;
        }

        int lineToJoin = (line == 0) ? 0 : line - 1;

        if (editor.joinLine(lineToJoin))
            cout << "Line deleted successfully.\n";
        else
            cout << "Unable to delete the line. The document may be partly changed; use Undo.\n";
    }

    // ---------------- Copy ----------------
    void handleCopy(Editor& editor)
    {
        string text = editor.getDocument().getText();

        int startLine = 0;
        int startCol = 0;
        int endLine = 0;
        int endCol = 0;

        if (!readSelection(
                editor, text,
                startLine, startCol, endLine, endCol))
            return;

        string selected = extractRange(
            text, startLine, startCol, endLine, endCol
        );

        if (selected.empty())
        {
            cout << "Nothing to copy.\n";
            return;
        }

        clipboardText = selected;

        cout << "Copied " << clipboardText.length()
             << " character(s) to the clipboard.\n";
    }

    // ---------------- Cut ----------------
    void handleCut(Editor& editor)
    {
        string text = editor.getDocument().getText();

        int startLine = 0;
        int startCol = 0;
        int endLine = 0;
        int endCol = 0;

        if (!readSelection(
                editor, text,
                startLine, startCol, endLine, endCol))
            return;

        string selected = extractRange(
            text, startLine, startCol, endLine, endCol
        );

        if (selected.empty())
        {
            cout << "Nothing to cut.\n";
            return;
        }

        // Copy first, as the GUI does, so the clipboard always
        // receives the original text.
        clipboardText = selected;

        if (deleteRange(editor, startLine, startCol, endLine, endCol))
        {
            cout << "Cut " << clipboardText.length()
                 << " character(s) to the clipboard.\n";
        }
        else
        {
            cout << "Could not cut the selected text. The document may "
                    "be partly changed; use Undo to revert.\n";
        }
    }

    // ---------------- Paste ----------------
    void handlePaste(Editor& editor)
    {
        if (clipboardText.empty())
        {
            cout << "The clipboard is empty. Use Copy or Cut first.\n";
            return;
        }

        string text = editor.getDocument().getText();

        int line = 0;
        int col = 0;

        if (!readPosition(
                editor, text,
                "Enter line number to paste at: ",
                "Enter column number to paste at: ",
                line, col))
            return;

        int endLine = 0;
        int endCol = 0;

        if (insertTextAt(editor, line, col, clipboardText, endLine, endCol))
        {
            cout << "Pasted successfully! Cursor is now at Line "
                 << endLine + 1 << ", Column " << endCol + 1 << ".\n";
        }
        else
        {
            cout << "Could not paste text. Some of it may already be in "
                    "the document; use Undo to revert.\n";
        }
    }

    // ---------------- Find Next (wraps around) ----------------
    void handleFindNext(Editor& editor)
    {
        string searchText = InputHelper::readLine(
            "Enter text to search (leave empty to reuse the last search): "
        );

        if (searchText.empty())
            searchText = lastSearchText;

        if (searchText.empty())
        {
            cout << "Search text cannot be empty.\n";
            return;
        }

        // If the document changed since the last Find Next (edit, undo,
        // New, Open ...), the remembered position no longer points to
        // the same place, so start again from the beginning.
        string currentText = editor.getDocument().getText();

        if (currentText != lastFindDocumentText)
        {
            lastFindDocumentText = currentText;
            nextFindLine = 0;
            nextFindCol = 0;
        }

        // A new search starts again from the beginning.
        if (searchText != lastSearchText)
        {
            lastSearchText = searchText;
            nextFindLine = 0;
            nextFindCol = 0;
        }

        DynamicArray<Match> results;

        if (!editor.search(searchText, results))
        {
            cout << "Search failed.\n";
            return;
        }

        if (results.getSize() == 0)
        {
            cout << "No matches found.\n";
            return;
        }

        // First match at or after the current position.
        int chosen = -1;

        for (int i = 0; i < results.getSize(); i++)
        {
            Match* match = results.getAt(i);

            if (match == nullptr)
                continue;

            if (match->line > nextFindLine ||
                (match->line == nextFindLine &&
                 match->col >= nextFindCol))
            {
                chosen = i;
                break;
            }
        }

        // Wrap around to the first match.
        if (chosen == -1)
        {
            cout << "Reached the end of the document. "
                    "Continuing from the beginning.\n";
            chosen = 0;
        }

        Match* found = results.getAt(chosen);

        if (found == nullptr)
        {
            cout << "Search failed.\n";
            return;
        }

        string text = editor.getDocument().getText();

        cout << "Match " << chosen + 1 << " of " << results.getSize()
             << " at Line " << found->line + 1
             << ", Column " << found->col + 1 << ":\n";
        cout << "  " << lineTextAt(text, found->line) << '\n';

        // The next search continues after this match.
        nextFindLine = found->line;
        nextFindCol = found->col + static_cast<int>(searchText.length());
    }
}

// Constructor
Menu::Menu()
    : running(true)
{
}

// Main Menu
void Menu::displayMenu()
{
    cout << "\n========== TEXT EDITOR ==========\n";
    cout << "1. New Document\n";
    cout << "2. Open Document\n";
    cout << "3. Exit\n";
    cout << "=================================\n";
}

// Create New Document
void Menu::handleNew()
{
    if (editor.newDocument())
    {
        cout << "\nNew document created successfully.\n";
        editingMenu();
    }
    else
    {
        cout << "Unable to create new document.\n";
    }
}

// Open Existing Document
void Menu::handleOpen()
{
    string filename = InputHelper::readLine(
        "Enter filename: "
    );

    if (filename.empty())
    {
        cout << "Filename cannot be empty.\n";
        return;
    }

    if (editor.open(filename))
    {
        cout << "\nDocument opened successfully.\n";

        if (editor.getDocument().getCharCount() > 0)
            handleDisplay();

        editingMenu();
    }
    else
    {
        cout << "Unable to open document.\n";
    }
}

// Editing Menu

void Menu::editingMenu()
{
    while (true)
    {
        cout << "\n========== TEXT EDITOR ==========\n";

        bool hasText = editor.getDocument().getCharCount() > 0;
        bool canUndo = editor.canUndo();
        bool canRedo = editor.canRedo();

        int option = 1;

        int insertChoice = -1;
        int deleteChoice = -1;
        int searchChoice = -1;
        int replaceChoice = -1;
        int undoChoice = -1;
        int redoChoice = -1;
        int displayChoice = -1;
        int infoChoice = -1;
        int saveChoice = -1;
        int saveAsChoice = -1;
        int backChoice = -1;

        // Additional options (terminal equivalents of the GUI features)
        int insertAtChoice = -1;
        int pasteChoice = -1;
        int deleteLineChoice = -1;
        int splitChoice = -1;
        int joinChoice = -1;
        int copyChoice = -1;
        int cutChoice = -1;
        int findNextChoice = -1;

        // Insert is always available
        insertChoice = option;
        cout << option++ << ". Insert Text\n";

        // Show editing options only when document has text
        if (hasText)
        {
            deleteChoice = option;
            cout << option++ << ". Delete Text\n";

            searchChoice = option;
            cout << option++ << ". Search Text\n";

            replaceChoice = option;
            cout << option++ << ". Replace Text\n";

            displayChoice = option;
            cout << option++ << ". Display Document\n";

            infoChoice = option;
            cout << option++ << ". Document Information\n";
        }

        // Insert at a position and Paste are always available,
        // like typing and pasting in the GUI
        insertAtChoice = option;
        cout << option++ << ". Insert Text at Position\n";

        pasteChoice = option;
        cout << option++ << ". Paste Text\n";

        // More editing options when the document has text
        if (hasText)
        {
            deleteLineChoice = option;
            cout << option++ << ". Delete Line\n";

            splitChoice = option;
            cout << option++ << ". Split Line\n";

            joinChoice = option;
            cout << option++ << ". Join Lines\n";

            copyChoice = option;
            cout << option++ << ". Copy Text\n";

            cutChoice = option;
            cout << option++ << ". Cut Text\n";

            findNextChoice = option;
            cout << option++ << ". Find Next\n";
        }

        // Undo option
        if (canUndo)
        {
            undoChoice = option;
            cout << option++ << ". Undo\n";
        }

        // Redo option
        if (canRedo)
        {
            redoChoice = option;
            cout << option++ << ". Redo\n";
        }

        // Save options
        saveChoice = option;
        cout << option++ << ". Save\n";

        saveAsChoice = option;
        cout << option++ << ". Save As\n";

        // Back option
        backChoice = option;
        cout << option++ << ". Back to Main Menu\n";

        cout << "=================================\n";

        int choice;

        cout << "Enter your choice: ";

        if (!(cin >> choice))
        {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');

            cout << "Invalid input! Enter a number.\n";
            continue;
        }

        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        if (choice == insertChoice)
        {
            handleInsert();
        }
        else if (choice == deleteChoice && deleteChoice != -1)
        {
            handleDelete();
        }
        else if (choice == searchChoice && searchChoice != -1)
        {
            handleSearch();
        }
        else if (choice == replaceChoice && replaceChoice != -1)
        {
            handleReplace();
        }
        else if (choice == displayChoice && displayChoice != -1)
        {
            handleDisplay();
        }
        else if (choice == infoChoice && infoChoice != -1)
        {
            handleInfo();
        }
        else if (choice == undoChoice && undoChoice != -1)
        {
            handleUndo();
        }
        else if (choice == redoChoice && redoChoice != -1)
        {
            handleRedo();
        }
        else if (choice == insertAtChoice && insertAtChoice != -1)
        {
            handleInsertAt(editor);
        }
        else if (choice == pasteChoice && pasteChoice != -1)
        {
            handlePaste(editor);
        }
        else if (choice == deleteLineChoice && deleteLineChoice != -1)
        {
            handleDeleteLine(editor);
        }
        else if (choice == splitChoice && splitChoice != -1)
        {
            handleSplitLine(editor);
        }
        else if (choice == joinChoice && joinChoice != -1)
        {
            handleJoinLine(editor);
        }
        else if (choice == copyChoice && copyChoice != -1)
        {
            handleCopy(editor);
        }
        else if (choice == cutChoice && cutChoice != -1)
        {
            handleCut(editor);
        }
        else if (choice == findNextChoice && findNextChoice != -1)
        {
            handleFindNext(editor);
        }
        else if (choice == saveChoice)
        {
            handleSave();
        }
        else if (choice == saveAsChoice)
        {
            handleSaveAs();
        }
        else if (choice == backChoice)
        {
            if (editor.exit())
            {
                return;
            }
        }
        else
        {
            cout << "Invalid choice! Please try again.\n";
        }
    }
}

// Insert Text
void Menu::handleInsert()
{
    cout << "\n------ Writing Mode ------\n";
    cout << "Enter your text below.\n";
    cout << "Type :done on a separate line to finish.\n\n";

    string block;
    string line;

    bool firstLine = true;

    while (true)
    {
        line = InputHelper::readLine("> ");

        if (line == ":done")
            break;

        if (!firstLine)
            block += '\n';

        block += line;
        firstLine = false;
    }

    if (editor.insertBlockAtEnd(block, !firstLine))
    {
        cout << "\nText inserted successfully!\n";
    }
    else
    {
        cout << "\nNo text was inserted.\n";
    }
}
// Delete Text
void Menu::handleDelete()
{
    int line = InputHelper::readInt(
        "Enter line number: "
    );

    int col = InputHelper::readInt(
        "Enter starting column: "
    );

    int length = InputHelper::readInt(
        "Enter number of characters to delete: "
    );

    line--;
    col--;

    if (editor.remove(line, col, length))
        cout << "Text deleted successfully.\n";
    else
        cout << "Unable to delete text. Check your inputs.\n";
}

// Search Text
void Menu::handleSearch()
{
    string searchText = InputHelper::readLine(
        "Enter text to search: "
    );

    DynamicArray<Match> results;

    if (!editor.search(searchText, results))
    {
        cout << "Search failed.\n";
        return;
    }

    if (results.getSize() == 0)
    {
        cout << "No matches found.\n";
        return;
    }

    cout << "\nMatches found: " << results.getSize() << '\n';

    for (int i = 0; i < results.getSize(); i++)
    {
        Match* match = results.getAt(i);

        if (match != nullptr)
        {
            cout << "Line: " << match->line + 1
                 << ", Column: " << match->col + 1
                 << '\n';
        }
    }
}

// Replace Text
void Menu::handleReplace()
{
    cout << "\n1. Replace one occurrence\n";
    cout << "2. Replace all occurrences\n";

    int choice = InputHelper::readInt("Enter choice: ");

    string searchText = InputHelper::readLine(
        "Enter text to find: "
    );

    string replacementText = InputHelper::readLine(
        "Enter replacement text: "
    );

    if (choice == 1)
    {
        DynamicArray<Match> results;

        if (!editor.search(searchText, results))
        {
            cout << "Search failed.\n";
            return;
        }

        if (results.getSize() == 0)
        {
            cout << "No matches found.\n";
            return;
        }

        for (int i = 0; i < results.getSize(); i++)
        {
            Match* match = results.getAt(i);

            if (match != nullptr)
            {
                cout << i + 1
                     << ". Line: " << match->line + 1
                     << ", Column: " << match->col + 1
                     << '\n';
            }
        }

        int occurrence = InputHelper::readInt(
            "Select occurrence: "
        );

        if (occurrence < 1 || occurrence > results.getSize())
        {
            cout << "Invalid occurrence.\n";
            return;
        }

        Match* selected = results.getAt(occurrence - 1);

        if (selected != nullptr &&
            editor.replace(*selected, searchText, replacementText))
        {
            cout << "Text replaced successfully.\n";
        }
        else
        {
            cout << "Replacement failed.\n";
        }
    }
    else if (choice == 2)
    {
        // Tell the user when there was nothing to replace
        // (Editor::replaceAll reports success even for zero matches)
        DynamicArray<Match> found;

        if (editor.search(searchText, found) && found.getSize() == 0)
        {
            cout << "No matches found.\n";
            return;
        }

        if (editor.replaceAll(searchText, replacementText))
            cout << "All occurrences replaced successfully.\n";
        else
            cout << "Replace all failed.\n";
    }
    else
    {
        cout << "Invalid choice.\n";
    }
}

// Undo
void Menu::handleUndo()
{
    if (editor.undo())
        cout << "Last operation undone successfully.\n";
    else
        cout << "Nothing to undo.\n";
}
void Menu::handleRedo()
{
    if (editor.redo())
    {
        cout << "Redo successful!\n";
    }
    else
    {
        cout << "Nothing to redo.\n";
    }
}

// Display Document
void Menu::handleDisplay()
{
    cout << "\n========== DOCUMENT ==========\n";

    editor.getDocument().display(cout);

    cout << "==============================\n";
}

// Document Information
void Menu::handleInfo()
{
    editor.showInfo();

    // Extra statistics the GUI shows under "About Filename"
    string text = editor.getDocument().getText();

    int words = 0;
    int charactersWithoutWhitespace = 0;
    bool insideWord = false;

    for (size_t i = 0; i < text.length(); i++)
    {
        unsigned char ch = static_cast<unsigned char>(text[i]);

        if (isspace(ch))
        {
            insideWord = false;
        }
        else
        {
            charactersWithoutWhitespace++;

            if (!insideWord)
            {
                words++;
                insideWord = true;
            }
        }
    }

    cout << "Characters (excluding whitespace): "
         << charactersWithoutWhitespace << '\n';
    cout << "Total Words: " << words << '\n';
}

// Save
void Menu::handleSave()
{
    // If document has no filename, ask for one
    if (!editor.hasFilename())
    {
        cout << "\nThis document has not been saved yet.\n";
        handleSaveAs();
        return;
    }

    if (editor.save())
        cout << "Document saved successfully.\n";
    else
        cout << "Unable to save document.\n";
}

// Save As
void Menu::handleSaveAs()
{
    string filename = InputHelper::readLine(
        "Enter filename: "
    );

    if (filename.empty())
    {
        cout << "Filename cannot be empty.\n";
        return;
    }

    if (editor.saveAs(filename))
        cout << "Document saved successfully.\n";
    else
        cout << "Unable to save document.\n";
}

// Exit
void Menu::handleExit()
{
    if (editor.exit())
    {
        cout << "Exiting Text Editor...\n";
        running = false;
    }
    else
    {
        cout << "Exit cancelled.\n";
    }
}

// Main Program Loop
void Menu::run()
{
    while (running)
    {
        displayMenu();

        int choice = InputHelper::readInt(
            "Enter your choice: "
        );

        switch (choice)
        {
            case 1:
                handleNew();
                break;

            case 2:
                handleOpen();
                break;

            case 3:
                handleExit();
                break;

            default:
                cout << "Invalid choice. Select 1, 2 or 3.\n";
        }
    }
}