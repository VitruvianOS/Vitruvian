AddOn(
	linprog
	STATIC

	SOURCES
	ActiveSetSolver.cpp
	Constraint.cpp
	LayoutOptimizer.cpp
	LinearSpec.cpp
	Summand.cpp
	Variable.cpp
)

UsePrivateHeaders(linprog shared)
