
add_library(linprog STATIC "")

target_sources(linprog PRIVATE
	ActiveSetSolver.cpp
	Constraint.cpp
	LayoutOptimizer.cpp
	LinearSpec.cpp
	Summand.cpp
	Variable.cpp
)
