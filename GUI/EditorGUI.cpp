#include <windows.h>
#include <commdlg.h>
#include <string>
#include <cstdio>
#include <cstring>
#include <cctype>

#include "GUIController.h"

// =====================================================
// CONTROL AND MENU IDS
// =====================================================

#define ID_DOCUMENT       101
#define ID_STATUS         102

#define ID_NEW            401
#define ID_OPEN           402
#define ID_SAVE           403
#define ID_SAVE_AS        404
#define ID_EXIT           405

#define ID_UNDO           410
#define ID_REDO           411
#define ID_FIND           412
#define ID_REPLACE        413
#define ID_SELECT_ALL     414
#define ID_COPY           415
#define ID_PASTE          416
#define ID_CUT            417

#define ID_STATUS_TOGGLE  430

#define ID_ABOUT          440
#define ID_ABOUT_FILENAME 441

// =====================================================
// MLA STUDIO THEME
// =====================================================

const COLORREF MLA_BG = RGB(255, 255, 255);
const COLORREF MLA_TEXT = RGB(35, 35, 35);
const COLORREF MLA_MUTED = RGB(110, 110, 110);
const COLORREF MLA_STATUS = RGB(245, 245, 245);
const COLORREF MLA_BORDER = RGB(225, 225, 225);

// =====================================================
// GLOBAL VARIABLES
// =====================================================

HWND hDocument = nullptr;
HWND hStatus = nullptr;
HWND hFindReplaceDialog = nullptr;

WNDPROC OriginalEditProcedure = nullptr;

GUIController* controller = nullptr;

HFONT fontEditor = nullptr;
HFONT fontStatus = nullptr;

HBRUSH brushBackground = nullptr;
HBRUSH brushStatus = nullptr;

UINT WM_FINDREPLACE = 0;

FINDREPLACEA findReplaceData = {};

char findBuffer[256] = "";
char replaceBuffer[256] = "";

char currentFilename[MAX_PATH] = "";

bool showStatusBar = true;
bool documentSaved = true;   // an untouched empty document has nothing to save

// Groups several backend edits into ONE undo/redo step for as long as
// the object lives (cut, paste, typing over a selection, ...).
// Nested groups are allowed; the outermost one decides.
struct EditGroup
{
    bool active = false;

    explicit EditGroup(bool startNow = true)
    {
        if (startNow)
            begin();
    }

    void begin()
    {
        if (!active && controller)
        {
            controller->beginGroup();
            active = true;
        }
    }

    ~EditGroup()
    {
        if (active && controller)
            controller->endGroup();
    }

    EditGroup(const EditGroup&) = delete;
    EditGroup& operator=(const EditGroup&) = delete;
};

// =====================================================
// FUNCTION DECLARATIONS
// =====================================================

void RefreshDocument();
void RefreshDocumentAt(int line, int column);

bool GetCaretPosition(int& line, int& column);

void OpenFindDialog(HWND hwnd);
void OpenReplaceDialog(HWND hwnd);

void FindNextOccurrence(HWND hwnd);
void ReplaceCurrentOccurrence(HWND hwnd);
void ReplaceAllOccurrences(HWND hwnd);

void UpdateStatusBar();

void CopySelectedText();
void CutSelectedText();
bool DeleteSelectedText(int& line, int& column);
void PasteClipboardText();

void ShowAboutMLAStudio(HWND hwnd);
void ShowAboutFilename(HWND hwnd);

LRESULT CALLBACK DocumentEditProcedure(
    HWND hwnd,
    UINT msg,
    WPARAM wp,
    LPARAM lp
);

// =====================================================
// THEME INITIALIZATION
// =====================================================

void InitializeTheme()
{
    fontEditor = CreateFontA(
        20,
        0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        "Times New Roman"
    );

    fontStatus = CreateFontA(
        13,
        0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        "Segoe UI"
    );

    brushBackground = CreateSolidBrush(MLA_BG);
    brushStatus = CreateSolidBrush(MLA_STATUS);
}

// =====================================================
// RESOURCE CLEANUP
// =====================================================

void CleanupTheme()
{
    if (fontEditor)
        DeleteObject(fontEditor);

    if (fontStatus)
        DeleteObject(fontStatus);

    if (brushBackground)
        DeleteObject(brushBackground);

    if (brushStatus)
        DeleteObject(brushStatus);
}

// =====================================================
// HELPER FUNCTIONS
// =====================================================

std::string GetControlText(HWND control)
{
    if (!control)
        return "";

    int length = GetWindowTextLengthA(control);

    char* buffer = new char[length + 1];

    GetWindowTextA(control, buffer, length + 1);

    std::string result(buffer);

    delete[] buffer;

    return result;
}

void ShowError(const char* message)
{
    MessageBoxA(
        NULL,
        message,
        "MLA Studio",
        MB_OK | MB_ICONERROR
    );
}

void ShowSuccess(const char* message)
{
    MessageBoxA(
        NULL,
        message,
        "MLA Studio",
        MB_OK | MB_ICONINFORMATION
    );
}

// =====================================================
// STATUS BAR
// =====================================================

void UpdateStatusBar()
{
    if (!controller || !hStatus)
        return;

    char status[200];

    std::snprintf(
        status,
        sizeof(status),
        "  Lines: %d     Characters: %d",
        controller->getLineCount(),
        controller->getCharCount()
    );

    SetWindowTextA(hStatus, status);
}

// =====================================================
// DOCUMENT DISPLAY
// =====================================================

void RefreshDocument()
{
    if (!controller || !hDocument)
        return;

    std::string text = controller->getText();

    // Convert backend LF into Windows CR-LF.

    std::string displayText;

    for (std::size_t i = 0; i < text.length(); i++)
    {
        if (text[i] == '\n' &&
            (i == 0 || text[i - 1] != '\r'))
        {
            displayText += '\r';
        }

        displayText += text[i];
    }

    SetWindowTextA(
        hDocument,
        displayText.c_str()
    );

    UpdateStatusBar();
}

// =====================================================
// CARET POSITION
// =====================================================

bool GetCaretPosition(int& line, int& column)
{
    if (!hDocument)
        return false;

    DWORD start = 0;
    DWORD end = 0;

    SendMessageA(
        hDocument,
        EM_GETSEL,
        (WPARAM)&start,
        (LPARAM)&end
    );

    if (start != end)
        return false;

    line = static_cast<int>(
        SendMessageA(
            hDocument,
            EM_LINEFROMCHAR,
            start,
            0
        )
    );

    int lineStart = static_cast<int>(
        SendMessageA(
            hDocument,
            EM_LINEINDEX,
            line,
            0
        )
    );

    if (lineStart < 0)
        return false;

    column = static_cast<int>(start) - lineStart;

    return line >= 0 && column >= 0;
}

// =====================================================
// REFRESH AND RESTORE CARET
// =====================================================

void RefreshDocumentAt(int line, int column)
{
        RefreshDocument();

    int lineCount = static_cast<int>(
        SendMessageA(hDocument, EM_GETLINECOUNT, 0, 0)
    );

    if (line >= lineCount)
        line = lineCount - 1;

    if (line < 0)
        line = 0;

    int lineStart = static_cast<int>(
        SendMessageA(
            hDocument,
            EM_LINEINDEX,
            line,
            0
        )
    );

    if (lineStart >= 0)
    {
        int lineLength = static_cast<int>(
            SendMessageA(hDocument, EM_LINELENGTH, lineStart, 0)
        );

        if (column > lineLength)
            column = lineLength;

        if (column < 0)
            column = 0;

        int position = lineStart + column;


        SendMessageA(
            hDocument,
            EM_SETSEL,
            position,
            position
        );

        // FIX: SetWindowText resets the scroll position, so bring
        // the caret back into view.
        SendMessageA(hDocument, EM_SCROLLCARET, 0, 0);
    }

    SetFocus(hDocument);
}

// =====================================================
// FILE OPERATIONS
// =====================================================

bool SaveAsDocument(HWND hwnd)
{
    char filename[MAX_PATH] = "";

    OPENFILENAMEA ofn = {};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;

    ofn.lpstrFilter =
        "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";

    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "txt";

    ofn.Flags =
        OFN_OVERWRITEPROMPT |
        OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameA(&ofn))
        return false;

    if (!controller->saveAsFile(filename))
    {
        ShowError("Could not save the document.");
        return false;
    }

    strcpy(currentFilename, filename);
    documentSaved = true;

    return true;
}

bool SaveDocument(HWND hwnd)
{
    if (!controller->hasFilename())
        return SaveAsDocument(hwnd);

    if (!controller->saveFile())
    {
        ShowError("Could not save the document.");
        return false;
    }

    documentSaved = true;

    return true;
}

bool ConfirmDocumentReplacement(HWND hwnd)
{
    if (documentSaved)
        return true;

    int choice = MessageBoxA(
        hwnd,
        "Do you want to save the current document before continuing?\n\n"
        "YES = Save\n"
        "NO = Continue without saving\n"
        "CANCEL = Stay in the current document",
        "MLA Studio",
        MB_YESNOCANCEL | MB_ICONQUESTION
    );

    if (choice == IDCANCEL)
        return false;

    if (choice == IDYES)
        return SaveDocument(hwnd);

    return true;
}

void OpenDocument(HWND hwnd)
{
    if (!ConfirmDocumentReplacement(hwnd))
        return;

    char filename[MAX_PATH] = "";

    OPENFILENAMEA ofn = {};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;

    ofn.lpstrFilter =
        "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";

    ofn.nFilterIndex = 1;

    ofn.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameA(&ofn))
        return;

    if (!controller->openFile(filename))
    {
        ShowError("Could not open the selected file.");
        return;
    }

    strcpy(currentFilename, filename);
    documentSaved = true;

    RefreshDocument();

    SetFocus(hDocument);
}

// =====================================================
// NEW DOCUMENT
// =====================================================

void NewDocument(HWND hwnd)
{
    if (!ConfirmDocumentReplacement(hwnd))
        return;

    controller->startNewDocument();

    currentFilename[0] = '\0';
    documentSaved = true;

    RefreshDocument();

    SendMessageA(
        hDocument,
        EM_SETSEL,
        0,
        0
    );

    SetFocus(hDocument);
}

// =====================================================
// FIND / REPLACE DIALOG
// =====================================================

void OpenFindDialog(HWND hwnd)
{
    if (hFindReplaceDialog)
    {
        SetForegroundWindow(hFindReplaceDialog);
        return;
    }

    ZeroMemory(&findReplaceData, sizeof(findReplaceData));

    findReplaceData.lStructSize = sizeof(findReplaceData);
    findReplaceData.hwndOwner = hwnd;
    findReplaceData.lpstrFindWhat = findBuffer;
    findReplaceData.wFindWhatLen = sizeof(findBuffer);
    findReplaceData.Flags = FR_DOWN;

    hFindReplaceDialog = FindTextA(&findReplaceData);
}

void OpenReplaceDialog(HWND hwnd)
{
    if (hFindReplaceDialog)
    {
        SetForegroundWindow(hFindReplaceDialog);
        return;
    }

    ZeroMemory(&findReplaceData, sizeof(findReplaceData));

    findReplaceData.lStructSize = sizeof(findReplaceData);
    findReplaceData.hwndOwner = hwnd;

    findReplaceData.lpstrFindWhat = findBuffer;
    findReplaceData.wFindWhatLen = sizeof(findBuffer);

    findReplaceData.lpstrReplaceWith = replaceBuffer;
    findReplaceData.wReplaceWithLen = sizeof(replaceBuffer);

    findReplaceData.Flags = FR_DOWN;

    hFindReplaceDialog = ReplaceTextA(&findReplaceData);
}

// =====================================================
// FIND NEXT OCCURRENCE
// =====================================================

void FindNextOccurrence(HWND hwnd)
{
    if (!controller)
        return;

    std::string searchText(findBuffer);

    if (searchText.empty())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    DynamicArray<Match> results;

    if (!controller->searchText(searchText, results))
    {
        ShowError("Search could not be completed.");
        return;
    }

    if (results.isEmpty())
    {
        MessageBeep(MB_ICONWARNING);

        SetWindowTextA(
            hStatus,
            "  No matches found."
        );

        return;
    }

    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;

    SendMessageA(
        hDocument,
        EM_GETSEL,
        (WPARAM)&selectionStart,
        (LPARAM)&selectionEnd
    );

    int currentLine = static_cast<int>(
        SendMessageA(
            hDocument,
            EM_LINEFROMCHAR,
            selectionEnd,
            0
        )
    );

    int currentLineStart = static_cast<int>(
        SendMessageA(
            hDocument,
            EM_LINEINDEX,
            currentLine,
            0
        )
    );

    int currentColumn =
        static_cast<int>(selectionEnd) - currentLineStart;

    int chosenIndex = -1;

    // Find the first match at or after the caret.

    for (int i = 0; i < results.getSize(); i++)
    {
        const Match* match = results.getAt(i);

        if (!match)
            continue;

        if (match->line > currentLine ||
            (match->line == currentLine &&
             match->col >= currentColumn))
        {
            chosenIndex = i;
            break;
        }
    }

    // Wrap around.

    if (chosenIndex == -1)
        chosenIndex = 0;

    const Match* match = results.getAt(chosenIndex);

    if (!match)
        return;

    int lineStart = static_cast<int>(
        SendMessageA(
            hDocument,
            EM_LINEINDEX,
            match->line,
            0
        )
    );

    if (lineStart < 0)
        return;

    int start = lineStart + match->col;
    int end = start + static_cast<int>(searchText.length());

    SendMessageA(
        hDocument,
        EM_SETSEL,
        start,
        end
    );

    SendMessageA(hDocument, EM_SCROLLCARET, 0, 0);

    SetFocus(hDocument);
}

// =====================================================
// REPLACE CURRENT OCCURRENCE
// =====================================================

void ReplaceCurrentOccurrence(HWND hwnd)
{
    std::string searchText(findBuffer);
    std::string replacementText(replaceBuffer);

    if (searchText.empty())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    DWORD start = 0;
    DWORD end = 0;

    SendMessageA(
        hDocument,
        EM_GETSEL,
        (WPARAM)&start,
        (LPARAM)&end
    );

    if (start == end)
    {
        FindNextOccurrence(hwnd);
        return;
    }

    std::string documentText = GetControlText(hDocument);

    if (end > documentText.length())
        return;

    std::string selectedText =
        documentText.substr(start, end - start);

    // Normalize CR-LF in selected text.

    std::string normalizedSelection;

    for (std::size_t i = 0; i < selectedText.length(); i++)
    {
        if (selectedText[i] == '\r' &&
            i + 1 < selectedText.length() &&
            selectedText[i + 1] == '\n')
        {
            normalizedSelection += '\n';
            i++;
        }
        else
        {
            normalizedSelection += selectedText[i];
        }
    }

    if (normalizedSelection != searchText)
    {
        FindNextOccurrence(hwnd);
        return;
    }

    int line = static_cast<int>(
        SendMessageA(
            hDocument,
            EM_LINEFROMCHAR,
            start,
            0
        )
    );

    int lineStart = static_cast<int>(
        SendMessageA(
            hDocument,
            EM_LINEINDEX,
            line,
            0
        )
    );

    if (lineStart < 0)
        return;

    int column = static_cast<int>(start) - lineStart;

    Match position(line, column);

    if (!controller->replaceOne(
        position,
        searchText,
        replacementText))
    {
        ShowError("Replacement failed.");
        return;
    }

    documentSaved = false;

    RefreshDocumentAt(line, column);

    FindNextOccurrence(hwnd);
}

// =====================================================
// REPLACE ALL
// =====================================================

void ReplaceAllOccurrences(HWND hwnd)
{
    std::string searchText(findBuffer);
    std::string replacementText(replaceBuffer);

    if (searchText.empty())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    if (!controller->replaceAllText(
        searchText,
        replacementText))
    {
        ShowError("Replace All failed.");
        return;
    }

    documentSaved = false;

    RefreshDocument();

    MessageBoxA(
        hwnd,
        "All matching occurrences have been replaced.",
        "MLA Studio",
        MB_OK | MB_ICONINFORMATION
    );
}

// =====================================================
// COPY
// =====================================================

void CopySelectedText()
{
    // Native Windows EDIT clipboard copy.
    SendMessageA(hDocument, WM_COPY, 0, 0);
}

// Delete the current selection through the DSA backend.
// Returned line/column identify the position where the caret should remain.
bool DeleteSelectedText(int& line, int& column)
{
    if (!controller || !hDocument)
        return false;

    DWORD start = 0;
    DWORD end = 0;

    SendMessageA(
        hDocument, EM_GETSEL,
        (WPARAM)&start, (LPARAM)&end
    );

        if (start == end)
        return false;

    // One selection delete = one undo step, however many lines it spans.
    EditGroup group;

    line = static_cast<int>(
        SendMessageA(hDocument, EM_LINEFROMCHAR, start, 0)
    );

    int endLine = static_cast<int>(
        SendMessageA(hDocument, EM_LINEFROMCHAR, end, 0)
    );

    int startLineIndex = static_cast<int>(
        SendMessageA(hDocument, EM_LINEINDEX, line, 0)
    );

    int endLineIndex = static_cast<int>(
        SendMessageA(hDocument, EM_LINEINDEX, endLine, 0)
    );

    if (line < 0 || endLine < line ||
        startLineIndex < 0 || endLineIndex < 0)
        return false;

    column = static_cast<int>(start) - startLineIndex;
    int endColumn = static_cast<int>(end) - endLineIndex;

    int startLength = static_cast<int>(
        SendMessageA(hDocument, EM_LINELENGTH, startLineIndex, 0)
    );

    if (column < 0 || column > startLength || endColumn < 0)
        return false;

    // A selection contained on one line is a normal range deletion.
    if (line == endLine)
    {
        int selectedLength = static_cast<int>(end - start);

        return controller->deleteText(
            line + 1, column + 1, selectedLength
        );
    }

    // Capture line lengths before changing the backend.
    // The native EDIT control is intentionally left unchanged until
    // the complete backend operation has been attempted.
    int endLength = static_cast<int>(
        SendMessageA(hDocument, EM_LINELENGTH, endLineIndex, 0)
    );

    if (endColumn > endLength)
        endColumn = endLength;

    // First remove the part of the starting line after the selection start.
    int startSuffixLength = startLength - column;

    if (startSuffixLength > 0 &&
        !controller->deleteText(
            line + 1, column + 1, startSuffixLength))
        return false;

    // Remove each complete intermediate line, then join it to the
    // starting line. The next original line consequently remains at
    // line + 1 after each join.
    for (int originalLine = line + 1;
         originalLine < endLine;
         ++originalLine)
    {
        int nativeLineStart = static_cast<int>(
            SendMessageA(
                hDocument, EM_LINEINDEX, originalLine, 0
            )
        );

        int originalLength = static_cast<int>(
            SendMessageA(
                hDocument, EM_LINELENGTH, nativeLineStart, 0
            )
        );

        if (originalLength > 0 &&
            !controller->deleteText(
                line + 2, 1, originalLength))
            return false;

        if (!controller->joinLine(line + 1))
            return false;
    }

    // The original end line is now immediately after the starting line.
    // Remove its selected prefix, then join it to preserve its suffix.
    if (endColumn > 0 &&
        !controller->deleteText(
            line + 2, 1, endColumn))
        return false;

    if (!controller->joinLine(line + 1))
        return false;

    return true;
}

void CutSelectedText()
{
    DWORD start = 0;
    DWORD end = 0;

    SendMessageA(
        hDocument, EM_GETSEL,
        (WPARAM)&start, (LPARAM)&end
    );

    if (start == end)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    // Copy first so the clipboard receives the original selected text.
    SendMessageA(hDocument, WM_COPY, 0, 0);

    int line = 0;
    int column = 0;

    documentSaved = false;

    if (!DeleteSelectedText(line, column))
    {
        ShowError("Could not cut the selected text.");
        RefreshDocument();
        return;
    }

    documentSaved = false;
    RefreshDocumentAt(line, column);
}

// =====================================================
// PASTE
// =====================================================

void PasteClipboardText()
{
    if (!OpenClipboard(hDocument))
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    HANDLE hData = GetClipboardData(CF_TEXT);

    if (!hData)
    {
        CloseClipboard();
        MessageBeep(MB_ICONWARNING);
        return;
    }

    char* clipboardText =
        static_cast<char*>(GlobalLock(hData));

    if (!clipboardText)
    {
        CloseClipboard();
        return;
    }

    std::string text(clipboardText);

    GlobalUnlock(hData);

        CloseClipboard();

    if (text.empty())
        return;

    // Deleting the selection and inserting the clipboard text
    // must undo together.
    EditGroup group;


    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;

    SendMessageA(
        hDocument,
        EM_GETSEL,
        (WPARAM)&selectionStart,
        (LPARAM)&selectionEnd
    );

    int line = static_cast<int>(
        SendMessageA(
            hDocument,
            EM_LINEFROMCHAR,
            selectionStart,
            0
        )
    );

    int startLineIndex = static_cast<int>(
        SendMessageA(hDocument, EM_LINEINDEX, line, 0)
    );

    if (startLineIndex < 0)
        return;

    int column = static_cast<int>(selectionStart) - startLineIndex;

    // Replace any selected text, including selections spanning lines.
    if (selectionStart != selectionEnd)
    {
        int deletedLine = 0;
        int deletedColumn = 0;

        documentSaved = false;

        if (!DeleteSelectedText(deletedLine, deletedColumn))
        {
            ShowError("Could not replace the selected text.");
            RefreshDocument();
            return;
        }

        line = deletedLine;
        column = deletedColumn;
    }

    // Normalize clipboard line endings (CR-LF and lone CR become LF).

    std::string normalizedText;

    for (std::size_t i = 0; i < text.length(); i++)
    {
        if (text[i] == '\r')
        {
            if (i + 1 < text.length() &&
                text[i + 1] == '\n')
            {
                i++;
            }

            normalizedText += '\n';
        }
        else
        {
            normalizedText += text[i];
        }
    }

    int currentLine = line;
    int currentColumn = column;

    if (!normalizedText.empty())
        documentSaved = false;

    // FIX: paste line by line in chunks instead of one character at a
    // time. This is much faster and creates far fewer undo entries.
    std::size_t pos = 0;

    while (true)
    {
        std::size_t nl = normalizedText.find('\n', pos);

        std::string chunk = (nl == std::string::npos)
            ? normalizedText.substr(pos)
            : normalizedText.substr(pos, nl - pos);

        if (!chunk.empty())
        {
            if (!controller->insertText(
                currentLine + 1,
                currentColumn + 1,
                chunk))
            {
                ShowError("Could not paste text.");
                RefreshDocument();
                return;
            }

            currentColumn += static_cast<int>(chunk.length());
        }

        if (nl == std::string::npos)
            break;

        if (!controller->splitLine(
            currentLine + 1,
            currentColumn + 1))
        {
            ShowError("Could not paste text.");
            RefreshDocument();
            return;
        }

        currentLine++;
        currentColumn = 0;
        pos = nl + 1;
    }

    documentSaved = false;

    RefreshDocumentAt(
        currentLine,
        currentColumn
    );
}

// =====================================================
// ABOUT MLA STUDIO
// =====================================================

void ShowAboutMLAStudio(HWND hwnd)
{
    MessageBoxA(
        hwnd,
        "MLA Studio\n\n"
        "C++14 Text Editor\n"
        "Developed as a Data Structures and Algorithms project.\n\n"
        "Features:\n"
        "- Document Editing\n"
        "- Find and Replace\n"
        "- Undo and Redo\n"
        "- Copy and Paste\n"
        "- Document Statistics",
        "About MLA Studio",
        MB_OK | MB_ICONINFORMATION
    );
}

// =====================================================
// ABOUT FILENAME
// =====================================================

void ShowAboutFilename(HWND hwnd)
{
    std::string path = currentFilename;

    std::string filename = "Untitled";

    if (!path.empty())
    {
        std::size_t position =
            path.find_last_of("\\/");

        if (position != std::string::npos)
            filename = path.substr(position + 1);
        else
            filename = path;
    }

    std::string text = controller->getText();

    int words = 0;
    bool insideWord = false;

    int charactersWithoutWhitespace = 0;

    for (std::size_t i = 0; i < text.length(); i++)
    {
        unsigned char ch =
            static_cast<unsigned char>(text[i]);

        if (std::isspace(ch))
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

    char info[1200];

    std::snprintf(
        info,
        sizeof(info),
        "File Name: %s\n\n"
        "File Path: %s\n\n"
        "Total Lines: %d\n"
        "Total Characters: %d\n"
        "Characters (excluding whitespace): %d\n"
        "Total Words: %d\n\n"
        "File Status: %s\n"
        "Encoding: ANSI",
        filename.c_str(),
        path.empty() ? "Not saved yet" : path.c_str(),
        controller->getLineCount(),
        controller->getCharCount(),
        charactersWithoutWhitespace,
        words,
        documentSaved ? "Saved" : "Unsaved"
    );

    MessageBoxA(
        hwnd,
        info,
        "About Filename",
        MB_OK | MB_ICONINFORMATION
    );
}

// =====================================================
// MENU CREATION
// =====================================================

void CreateEditorMenu(HWND hwnd)
{
    HMENU hMenu = CreateMenu();

    HMENU hFile = CreatePopupMenu();
    HMENU hEdit = CreatePopupMenu();
    HMENU hView = CreatePopupMenu();
    HMENU hHelp = CreatePopupMenu();

    // FILE

    AppendMenuA(hFile, MF_STRING, ID_NEW, "New\tCtrl+N");
    AppendMenuA(hFile, MF_STRING, ID_OPEN, "Open...\tCtrl+O");
    AppendMenuA(hFile, MF_STRING, ID_SAVE, "Save\tCtrl+S");
    AppendMenuA(hFile, MF_STRING, ID_SAVE_AS, "Save As...");

    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);

    AppendMenuA(hFile, MF_STRING, ID_EXIT, "Exit");

    // EDIT

    AppendMenuA(hEdit, MF_STRING, ID_UNDO, "Undo\tCtrl+Z");
    AppendMenuA(hEdit, MF_STRING, ID_REDO, "Redo\tCtrl+Y");

    AppendMenuA(hEdit, MF_SEPARATOR, 0, NULL);

    AppendMenuA(hEdit, MF_STRING, ID_CUT, "Cut\tCtrl+X");
    AppendMenuA(hEdit, MF_STRING, ID_COPY, "Copy\tCtrl+C");
    AppendMenuA(hEdit, MF_STRING, ID_PASTE, "Paste\tCtrl+V");

    AppendMenuA(hEdit, MF_SEPARATOR, 0, NULL);

    AppendMenuA(hEdit, MF_STRING, ID_FIND, "Find...\tCtrl+F");
    AppendMenuA(hEdit, MF_STRING, ID_REPLACE, "Replace...\tCtrl+H");

    AppendMenuA(hEdit, MF_SEPARATOR, 0, NULL);

    AppendMenuA(hEdit, MF_STRING, ID_SELECT_ALL, "Select All\tCtrl+A");

    // VIEW

    AppendMenuA(
        hView,
        MF_STRING | MF_CHECKED,
        ID_STATUS_TOGGLE,
        "Status Bar"
    );

    // HELP

    AppendMenuA(
        hHelp,
        MF_STRING,
        ID_ABOUT,
        "About MLA Studio"
    );

    AppendMenuA(
        hHelp,
        MF_STRING,
        ID_ABOUT_FILENAME,
        "About Filename"
    );

    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "File");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hEdit, "Edit");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hView, "View");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hHelp, "Help");

    SetMenu(hwnd, hMenu);
}

// =====================================================
// EDITOR CONTROLS
// =====================================================

void CreateEditorControls(HWND hwnd)
{
    hDocument = CreateWindowExA(
        0,
        "EDIT",
        "",
        WS_CHILD |
        WS_VISIBLE |
        WS_VSCROLL |
        WS_HSCROLL |
        ES_MULTILINE |
        ES_AUTOVSCROLL |
        ES_AUTOHSCROLL |
        ES_WANTRETURN,
        0,
        0,
        1000,
        700,
        hwnd,
        (HMENU)(INT_PTR)ID_DOCUMENT,
        GetModuleHandle(NULL),
        NULL
    );

    // FIX: raise the default ~32K text limit of the EDIT control.
    SendMessageA(hDocument, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);

    SendMessageA(
        hDocument,
        WM_SETFONT,
        (WPARAM)fontEditor,
        TRUE
    );

    OriginalEditProcedure = (WNDPROC)SetWindowLongPtrA(
        hDocument,
        GWLP_WNDPROC,
        (LONG_PTR)DocumentEditProcedure
    );

    hStatus = CreateWindowA(
        "STATIC",
        "",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0,
        0,
        1000,
        26,
        hwnd,
        (HMENU)(INT_PTR)ID_STATUS,
        GetModuleHandle(NULL),
        NULL
    );

    SendMessageA(
        hStatus,
        WM_SETFONT,
        (WPARAM)fontStatus,
        TRUE
    );

    UpdateStatusBar();
}

// =====================================================
// CUSTOM DOCUMENT KEYBOARD HANDLER
// =====================================================

LRESULT CALLBACK DocumentEditProcedure(
    HWND hwnd,
    UINT msg,
    WPARAM wp,
    LPARAM lp
)
{
    if (!OriginalEditProcedure)
    {
        return DefWindowProcA(hwnd, msg, wp, lp);
    }

    if (!controller)
    {
        return CallWindowProcA(
            OriginalEditProcedure,
            hwnd,
            msg,
            wp,
            lp
        );
    }

    // ================================================
    // KEYBOARD SHORTCUTS
    // ================================================

    if (msg == WM_KEYDOWN &&
        (GetKeyState(VK_CONTROL) & 0x8000))
    {
        switch (wp)
        {
                        case 'Z':
                SendMessageA(
                    GetParent(hwnd),
                    WM_COMMAND,
                    (GetKeyState(VK_SHIFT) & 0x8000) ? ID_REDO : ID_UNDO,
                    0
                );

                return 0;

            case 'Y':
                SendMessageA(
                    GetParent(hwnd),
                    WM_COMMAND,
                    ID_REDO,
                    0
                );
                return 0;

            case 'S':
                SendMessageA(
                    GetParent(hwnd),
                    WM_COMMAND,
                    ID_SAVE,
                    0
                );
                return 0;

            case 'O':
                SendMessageA(
                    GetParent(hwnd),
                    WM_COMMAND,
                    ID_OPEN,
                    0
                );
                return 0;

            case 'N':
                SendMessageA(
                    GetParent(hwnd),
                    WM_COMMAND,
                    ID_NEW,
                    0
                );
                return 0;

            case 'F':
                SendMessageA(
                    GetParent(hwnd),
                    WM_COMMAND,
                    ID_FIND,
                    0
                );
                return 0;

            case 'H':
                SendMessageA(
                    GetParent(hwnd),
                    WM_COMMAND,
                    ID_REPLACE,
                    0
                );
                return 0;

            case 'A':
                SendMessageA(
                    hwnd,
                    EM_SETSEL,
                    0,
                    -1
                );
                return 0;

            case 'X':
                SendMessageA(
                    GetParent(hwnd),
                    WM_COMMAND,
                    ID_CUT,
                    0
                );
                return 0;

            case 'C':
                SendMessageA(
                    hwnd,
                    WM_COPY,
                    0,
                    0
                );
                return 0;

            case 'V':
                PasteClipboardText();
                return 0;
        }
    }

    // ================================================
    // PREVENT UNSUPPORTED BACKEND OPERATIONS
    // ================================================

    if (msg == WM_PASTE)
    {
        PasteClipboardText();
        return 0;
    }

    if (msg == WM_CUT)
    {
        CutSelectedText();
        return 0;
    }

    if (msg == WM_CLEAR)
    {
        int line = 0;
        int column = 0;

        documentSaved = false;

        if (DeleteSelectedText(line, column))
        {
            RefreshDocumentAt(line, column);
        }
        else
        {
            MessageBeep(MB_ICONWARNING);
        }

        return 0;
    }

        if (msg == WM_UNDO)
    {
        SendMessageA(GetParent(hwnd), WM_COMMAND, ID_UNDO, 0);
        return 0;
    }

    // ================================================
    // DELETE KEY
    // ================================================

    if (msg == WM_KEYDOWN && wp == VK_DELETE)
    {
        DWORD start = 0;
        DWORD end = 0;

        SendMessageA(
            hwnd,
            EM_GETSEL,
            (WPARAM)&start,
            (LPARAM)&end
        );

        int line = static_cast<int>(
            SendMessageA(
                hwnd,
                EM_LINEFROMCHAR,
                start,
                0
            )
        );

        int lineStart = static_cast<int>(
            SendMessageA(
                hwnd,
                EM_LINEINDEX,
                line,
                0
            )
        );

        int column = static_cast<int>(start) - lineStart;

        if (start != end)
        {
            int selectedLine = 0;
            int selectedColumn = 0;

            documentSaved = false;

            if (DeleteSelectedText(selectedLine, selectedColumn))
            {
                RefreshDocumentAt(selectedLine, selectedColumn);
            }
            else
            {
                ShowError("Could not delete the selected text.");
                RefreshDocument();
            }

            return 0;
        }

        int lineLength = static_cast<int>(
            SendMessageA(
                hwnd,
                EM_LINELENGTH,
                lineStart,
                0
            )
        );

        if (column < lineLength)
        {
            if (controller->deleteText(
                line + 1,
                column + 1,
                1))
            {
                documentSaved = false;

                RefreshDocumentAt(line, column);
            }
        }
        else
        {
            if (controller->joinLine(line + 1))
            {
                documentSaved = false;

                RefreshDocumentAt(line, column);
            }
        }

        return 0;
    }

    // ================================================
    // CHARACTER INPUT
    // ================================================

    if (msg == WM_CHAR)
    {
        // FIX (main bug): Ctrl+key shortcuts also generate WM_CHAR with
        // control codes (Ctrl+A = 1, Ctrl+C = 3, Ctrl+V = 22, Ctrl+X = 24,
        // Ctrl+Z = 26 ...). These must never reach the editing logic below
        // or the native EDIT control. Note Ctrl+H = 8 (same as Backspace),
        // Ctrl+I = 9 (Tab) and Ctrl+M = 13 (Enter), so anything typed while
        // Ctrl is held is ignored as well.
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (wp < 32 && (ctrlDown ||
            (wp != 8 && wp != 9 && wp != 13)))
        {
            return 0;
        }

        // 127 is DEL (Ctrl+Backspace); do not insert it as text.
        if (wp == 127)
            return 0;

        int line = 0;
        int column = 0;

        DWORD selectionStart = 0;
        DWORD selectionEnd = 0;

        SendMessageA(
            hwnd, EM_GETSEL,
            (WPARAM)&selectionStart,
            (LPARAM)&selectionEnd
        );

                // Replacing a selection (delete + the new character) is a
        // single undo step; plain typing is not grouped.
        EditGroup typingGroup(false);

        // Typing, Enter, Tab or Backspace over a selection first
        // removes that selection through the backend.
                if (selectionStart != selectionEnd)
        {
            typingGroup.begin();

            documentSaved = false;

            if (!DeleteSelectedText(line, column))
            {
                ShowError("Could not delete the selected text.");
                RefreshDocument();
                return 0;
            }

            documentSaved = false;
            RefreshDocumentAt(line, column);

            // Backspace removes the selection itself; it must not
            // also delete the character immediately before it.
            if (wp == 8)
                return 0;
        }
        else if (!GetCaretPosition(line, column))
        {
            MessageBeep(MB_ICONWARNING);
            return 0;
        }

        // BACKSPACE

        if (wp == 8)
        {
            if (column > 0)
            {
                if (controller->deleteText(
                    line + 1,
                    column,
                    1))
                {
                    documentSaved = false;

                    RefreshDocumentAt(line, column - 1);
                }
            }
            else if (line > 0)
            {
                int previousLineStart = static_cast<int>(
                    SendMessageA(
                        hwnd,
                        EM_LINEINDEX,
                        line - 1,
                        0
                    )
                );

                int previousLineLength = static_cast<int>(
                    SendMessageA(
                        hwnd,
                        EM_LINELENGTH,
                        previousLineStart,
                        0
                    )
                );

                if (controller->joinLine(line))
                {
                    documentSaved = false;

                    RefreshDocumentAt(
                        line - 1,
                        previousLineLength
                    );
                }
            }

            return 0;
        }

        // ENTER

        if (wp == 13)
        {
            if (controller->splitLine(
                line + 1,
                column + 1))
            {
                documentSaved = false;

                RefreshDocumentAt(line + 1, 0);
            }

            return 0;
        }

        // TAB

        if (wp == 9)
        {
            if (controller->insertText(
                line + 1,
                column + 1,
                "\t"))
            {
                documentSaved = false;

                RefreshDocumentAt(line, column + 1);
            }

            return 0;
        }

        // NORMAL CHARACTER INPUT

        if (wp >= 32 && wp <= 255)
        {
            std::string character(
                1,
                static_cast<char>(wp)
            );

                        if (controller->typeText(
                line + 1,
                column + 1,
                character))
            {
                documentSaved = false;

                RefreshDocumentAt(line, column + 1);
            }

            return 0;
        }
    }

    return CallWindowProcA(
        OriginalEditProcedure,
        hwnd,
        msg,
        wp,
        lp
    );
}

// =====================================================
// MAIN WINDOW PROCEDURE
// =====================================================

LRESULT CALLBACK WindowProcedure(
    HWND hwnd,
    UINT msg,
    WPARAM wp,
    LPARAM lp
)
{
    switch (msg)
    {
        // =============================================
        // WINDOW CREATION
        // =============================================

        case WM_CREATE:
        {
            WM_FINDREPLACE = RegisterWindowMessageA(
                FINDMSGSTRINGA
            );

            CreateEditorMenu(hwnd);

            CreateEditorControls(hwnd);

            RefreshDocument();

            SetFocus(hDocument);

            return 0;
        }

        // =============================================
        // NATIVE FIND / REPLACE MESSAGES
        // =============================================

        default:
        {
            if (WM_FINDREPLACE != 0 &&
                msg == WM_FINDREPLACE)
            {
                FINDREPLACEA* data =
                    (FINDREPLACEA*)lp;

                if (!data)
                    return 0;

                if (data->Flags & FR_DIALOGTERM)
                {
                    hFindReplaceDialog = nullptr;
                    return 0;
                }

                if (data->Flags & FR_FINDNEXT)
                {
                    FindNextOccurrence(hwnd);
                    return 0;
                }

                if (data->Flags & FR_REPLACE)
                {
                    ReplaceCurrentOccurrence(hwnd);
                    return 0;
                }

                if (data->Flags & FR_REPLACEALL)
                {
                    ReplaceAllOccurrences(hwnd);
                    return 0;
                }
            }

            break;
        }
    }

    switch (msg)
    {
        // =============================================
        // WINDOW BACKGROUND
        // =============================================

        case WM_ERASEBKGND:
        {
            RECT rect;

            GetClientRect(hwnd, &rect);

            FillRect(
                (HDC)wp,
                &rect,
                brushBackground
            );

            return 1;
        }

        // =============================================
        // STATIC CONTROL COLORS
        // =============================================

        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wp;
            HWND control = (HWND)lp;

            if (control == hStatus)
            {
                SetTextColor(hdc, MLA_MUTED);
                SetBkColor(hdc, MLA_STATUS);

                return (LRESULT)brushStatus;
            }

            SetTextColor(hdc, MLA_TEXT);
            SetBkColor(hdc, MLA_BG);

            return (LRESULT)brushBackground;
        }

        // =============================================
        // EDIT CONTROL COLORS
        // =============================================

        case WM_CTLCOLOREDIT:
        {
            HDC hdc = (HDC)wp;

            SetTextColor(hdc, MLA_TEXT);
            SetBkColor(hdc, MLA_BG);

            return (LRESULT)brushBackground;
        }

        // =============================================
        // WINDOW RESIZING
        // =============================================

        case WM_SIZE:
        {
            int width = LOWORD(lp);
            int height = HIWORD(lp);

            int statusHeight = showStatusBar ? 26 : 0;

            if (hDocument)
            {
                MoveWindow(
                    hDocument,
                    0,
                    0,
                    width,
                    height - statusHeight,
                    TRUE
                );
            }

            if (hStatus)
            {
                ShowWindow(
                    hStatus,
                    showStatusBar ? SW_SHOW : SW_HIDE
                );

                MoveWindow(
                    hStatus,
                    0,
                    height - 26,
                    width,
                    26,
                    TRUE
                );
            }

            return 0;
        }

        // =============================================
        // MENU COMMANDS
        // =============================================

        case WM_COMMAND:
        {
            switch (LOWORD(wp))
            {
                // FILE

                case ID_NEW:
                {
                    NewDocument(hwnd);
                    break;
                }

                case ID_OPEN:
                {
                    OpenDocument(hwnd);
                    break;
                }

                case ID_SAVE:
                {
                    SaveDocument(hwnd);
                    break;
                }

                case ID_SAVE_AS:
                {
                    SaveAsDocument(hwnd);
                    break;
                }

                case ID_EXIT:
                {
                    if (ConfirmDocumentReplacement(hwnd))
                        DestroyWindow(hwnd);

                    break;
                }

                // EDIT

                                case ID_UNDO:
                {
                    if (!controller->undo())
                    {
                        MessageBeep(MB_ICONWARNING);
                        break;
                    }

                    documentSaved = false;

                    // Show the change: caret + scroll go to where the
                    // undo happened instead of jumping to the top.
                    RefreshDocumentAt(
                        controller->lastEditLine(),
                        controller->lastEditColumn()
                    );

                    break;
                }

                                case ID_REDO:
                {
                    if (!controller->redo())
                    {
                        MessageBeep(MB_ICONWARNING);
                        break;
                    }

                    documentSaved = false;

                    // Show the change: caret + scroll go to where the
                    // redo happened instead of jumping to the top.
                    RefreshDocumentAt(
                        controller->lastEditLine(),
                        controller->lastEditColumn()
                    );

                    break;
                }

                case ID_CUT:
                {
                    CutSelectedText();
                    break;
                }

                case ID_COPY:
                {
                    CopySelectedText();

                    SetFocus(hDocument);

                    break;
                }

                case ID_PASTE:
                {
                    PasteClipboardText();

                    break;
                }

                case ID_FIND:
                {
                    OpenFindDialog(hwnd);

                    break;
                }

                case ID_REPLACE:
                {
                    OpenReplaceDialog(hwnd);

                    break;
                }

                case ID_SELECT_ALL:
                {
                    SendMessageA(
                        hDocument,
                        EM_SETSEL,
                        0,
                        -1
                    );

                    SetFocus(hDocument);

                    break;
                }

                // VIEW

                case ID_STATUS_TOGGLE:
                {
                    showStatusBar = !showStatusBar;

                    HMENU menu = GetMenu(hwnd);

                    CheckMenuItem(
                        menu,
                        ID_STATUS_TOGGLE,
                        MF_BYCOMMAND |
                        (showStatusBar ? MF_CHECKED : MF_UNCHECKED)
                    );

                    RECT rect;

                    GetClientRect(hwnd, &rect);

                    SendMessageA(
                        hwnd,
                        WM_SIZE,
                        SIZE_RESTORED,
                        MAKELPARAM(
                            rect.right,
                            rect.bottom
                        )
                    );

                    break;
                }

                // HELP

                case ID_ABOUT:
                {
                    ShowAboutMLAStudio(hwnd);

                    break;
                }

                case ID_ABOUT_FILENAME:
                {
                    ShowAboutFilename(hwnd);

                    break;
                }
            }

            return 0;
        }

        // =============================================
        // CLOSE WINDOW
        // =============================================

        case WM_CLOSE:
        {
            if (ConfirmDocumentReplacement(hwnd))
                DestroyWindow(hwnd);

            return 0;
        }

        // =============================================
        // DESTROY WINDOW
        // =============================================

        case WM_DESTROY:
        {
            PostQuitMessage(0);

            return 0;
        }
    }

    return DefWindowProcA(
        hwnd,
        msg,
        wp,
        lp
    );
}

// =====================================================
// WINMAIN
// =====================================================

int WINAPI WinMain(
    HINSTANCE hInst,
    HINSTANCE hPrev,
    LPSTR args,
    int nCmdShow
)
{
    controller = new GUIController();

    InitializeTheme();

    const char CLASS_NAME[] = "MLAStudioWindow";

    WNDCLASSA wc = {};

    wc.lpfnWndProc = WindowProcedure;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = brushBackground;

    if (!RegisterClassA(&wc))
    {
        delete controller;
        controller = nullptr;

        CleanupTheme();

        return 0;
    }

    HWND hwnd = CreateWindowExA(
        0,
        CLASS_NAME,
        "MLA Studio",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1200,
        820,
        NULL,
        NULL,
        hInst,
        NULL
    );

    if (!hwnd)
    {
        delete controller;
        controller = nullptr;

        CleanupTheme();

        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    UpdateWindow(hwnd);

    MSG msg = {};

    while (GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);

        DispatchMessageA(&msg);
    }

    delete controller;

    controller = nullptr;

    CleanupTheme();

    return static_cast<int>(msg.wParam);
}