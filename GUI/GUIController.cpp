#include "GUIController.h"

GUIController::GUIController()
{
    editor = new Editor();
}

GUIController::~GUIController()
{
    delete editor;
}

void GUIController::startNewDocument()
{
    Editor* freshEditor = new Editor();

    delete editor;
    editor = freshEditor;
}

bool GUIController::openFile(const std::string& filename)
{
    Editor* freshEditor = new Editor();

    // A fresh Editor is unmodified, so open() won't
    // trigger the console-based save confirmation.
    if (!freshEditor->open(filename))
    {
        delete freshEditor;
        return false;
    }

    delete editor;
    editor = freshEditor;

    return true;
}

bool GUIController::saveFile()
{
    return editor->save();
}

bool GUIController::saveAsFile(const std::string& filename)
{
    return editor->saveAs(filename);
}

bool GUIController::hasFilename() const
{
    return editor->hasFilename();
}

bool GUIController::insertText(
    int line, int column, const std::string& text)
{
    if (line < 1 || column < 1)
        return false;

    return editor->insert(line - 1, column - 1, text);
}

bool GUIController::deleteText(
    int line, int column, int length)
{
    if (line < 1 || column < 1 || length <= 0)
        return false;

    return editor->remove(line - 1, column - 1, length);
}

bool GUIController::undo()
{
    return editor->undo();
}

bool GUIController::redo()
{
    return editor->redo();
}

bool GUIController::canUndo() const
{
    return editor->canUndo();
}

bool GUIController::canRedo() const
{
    return editor->canRedo();
}

bool GUIController::searchText(
    const std::string& text,
    DynamicArray<Match>& results)
{
    return editor->search(text, results);
}

bool GUIController::replaceOne(
    const Match& position,
    const std::string& searchText,
    const std::string& replacementText)
{
    return editor->replace(position, searchText, replacementText);
}

bool GUIController::replaceAllText(
    const std::string& searchText,
    const std::string& replacementText)
{
    return editor->replaceAll(searchText, replacementText);
}

std::string GUIController::getText() const
{
    return editor->getDocument().getText();
}

int GUIController::getLineCount() const
{
    return editor->getDocument().getLineCount();
}

int GUIController::getCharCount() const
{
    return editor->getDocument().getCharCount();
}
bool GUIController::insertNewLine()
{
    std::string currentText = editor->getDocument().getText();

    // Add a newline to the existing document.
    if (currentText.empty())
        return editor->insertBlockAtEnd("\n", true);

    return editor->insertBlockAtEnd("\n", true);
}
bool GUIController::splitLine(int line, int column)
{
    if (line < 1 || column < 1)
        return false;

    return editor->splitLine(line - 1, column - 1);
}

bool GUIController::joinLine(int line)
{
    if (line < 1)
        return false;

    return editor->joinLine(line - 1);
}

bool GUIController::typeText(
    int line, int column, const std::string& text)
{
    if (line < 1 || column < 1)
        return false;

    return editor->typeText(line - 1, column - 1, text);
}

void GUIController::beginGroup()
{
    editor->beginGroup();
}

void GUIController::endGroup()
{
    editor->endGroup();
}

int GUIController::lastEditLine() const
{
    return editor->getLastEditLine();
}

int GUIController::lastEditColumn() const
{
    return editor->getLastEditColumn();
}
