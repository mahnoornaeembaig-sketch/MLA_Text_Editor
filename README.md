# MLA STUDIO – DSA TEXT EDITOR
**Data Structures and Algorithms Term Project**

---

## PROJECT OVERVIEW

MLA Studio is a C++14 text editor developed as a Data Structures and Algorithms term project. The name **MLA** represents **Mahnoor, Laiba, and Aleena**, the three team members behind the project.

It provides two interfaces sharing the same backend:

- **GUI:** Native Windows application (Win32).
- **Console:** Menu-driven terminal application.

The project uses custom-built data structures instead of standard containers.

## CONTRIBUTORS

1. Mahnoor Naeem (CS-25054)
2. Aleena Khalid (CS-25056)
3. Laiba Qasim (CS-25069)

## PROJECT STRUCTURE

- **GUI/** – Graphical user interface.
- **Console/** – Console-based interface.
- **Core/** – Shared editor logic and functionality.
- **DataStructures/** – Custom data structure implementations.

## STEPS TO RUN THE PROGRAM

**1.** Clone or download the repository.

**2.** Open a terminal in the project folder.

**3. Compile the project:**

**Windows GUI (MinGW):**

```bash
g++ -std=c++14 -mwindows -ICore -IDataStructures -o MLAStudio.exe GUI/EditorGUI.cpp GUI/GUIController.cpp Core/Editor.cpp Core/Document.cpp DataStructures/DoublyLinkedList.cpp Core/UndoManager.cpp Core/TextProcessor.cpp Core/TextUtils.cpp Core/FileManager.cpp -lcomdlg32 -lgdi32
```

**Console version:**

```bash
g++ -std=c++14 -ICore -IDataStructures -o editor.exe Console/main.cpp Console/Menu.cpp Core/Editor.cpp Core/Document.cpp DataStructures/DoublyLinkedList.cpp Core/UndoManager.cpp Core/TextProcessor.cpp Core/TextUtils.cpp Core/FileManager.cpp
```

**4. Run the program:**

For GUI:

```powershell
.\MLAStudio.exe
```

For Console:

```powershell
.\editor.exe
```

## DATA STRUCTURES IMPLEMENTED

1. **Doubly Linked List** – Stores document lines.
2. **Dynamic Array** – Stores search matches and replacement records.
3. **Stack** – Manages Undo and Redo operations.

## DESIGN PATTERN

- **Command Pattern** – Represents editing operations as reversible commands, supporting Undo and Redo functionality.

## FUNCTIONALITIES IMPLEMENTED

- Create, Open, Save and Save As documents.
- Insert and delete text, words and lines.
- Delete multiple lines.
- Split and join lines.
- Search and Find Next.
- Replace one occurrence or all occurrences.
- Cut, Copy and Paste.
- Undo and Redo (up to 100 history steps).
- Document information and status bar.
- Input validation and error handling.

## WHAT WORKS

- GUI and Console interfaces.
- Custom data structures.
- File handling.
- Text editing operations.
- Search and Replace.
- Undo and Redo.
- Multi-line operations.
- Error handling and validation.

## KNOWN LIMITATIONS

- GUI is Windows-only (Win32 API).
- Unicode input outside the current ANSI code page is unsupported.
- Only one document can be open at a time.
- Search is case-sensitive.

## TOOLS AND TECHNOLOGIES

- C++14
- Win32 API
- MinGW / g++
- Visual Studio Code
- Git and GitHub

## REFERENCES

- C++ Reference (cppreference)
- GeeksforGeeks
- Data Structures and Algorithms course materials

---