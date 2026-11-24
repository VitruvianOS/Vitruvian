RunnableAddOn(Security
	SOURCES
		Security.cpp
		SecurityWindow.cpp
		FirewallView.cpp
		AddRuleWindow.cpp
		FirewallServices.cpp
		FirewallService.cpp
		FirewallBackend.cpp
		NftablesBackend.cpp
		IptablesBackend.cpp
		UfwBackend.cpp
		FirewallRunner.cpp
		SecurityReplicant.cpp
	LIBS shared localestub
	RDEF Security.rdef
)
UsePrivateHeaders(Security interface shared support)
