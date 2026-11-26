AddOn(AccessibilityReplicant SHARED
	SOURCES
	AccessibilityReplicant.cpp
	LIBS shared localestub media2
	RDEF AccessibilityReplicant.rdef
)
set_target_properties(AccessibilityReplicant PROPERTIES PREFIX "" SUFFIX "")
UsePrivateHeaders(AccessibilityReplicant app interface shared)
DoCatalogs("x-vnd.vos-AccessibilityReplicant" apps/accessibility_replicant)
