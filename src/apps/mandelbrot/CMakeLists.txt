Application(Mandelbrot
	SOURCES
	FractalEngine.cpp
	Mandelbrot.cpp
	LIBS localestub tracker translation
	RDEF Mandelbrot.rdef
)
UsePrivateHeaders(Mandelbrot interface shared system)
