add_executable(Sudoku
	ProgressWindow.cpp
	Sudoku.cpp
	SudokuField.cpp
	SudokuGenerator.cpp
	SudokuSolver.cpp
	SudokuView.cpp
	SudokuWindow.cpp
)

target_link_libraries(Sudoku PRIVATE be root tracker)

target_include_directories(Sudoku PRIVATE
	"../../kits/tracker/"
)
