# Hardware acceleration kit: GL renderer priority/selection and the overclock
# backends, shared by the Hardware preflet and BGLView.
AddOn(hwaccel
	SHARED

	SOURCES
	GLRendererPriority.cpp
	GLRendererSelect.cpp
	GLRendererSelectScore.cpp
	Overclock.cpp

	LIBS be
)
