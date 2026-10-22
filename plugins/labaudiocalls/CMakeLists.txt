include(BuildVeyonPlugin)

# "Call the teacher" is its own plugin: Veyon Master treats every feature of a plugin as related, so the
# always-running tray worker would otherwise make Lab audio look active on every student computer.
build_veyon_plugin(labaudiocalls
	NAME LabAudioCalls
	SOURCES
	LabAudioCallsPlugin.cpp
	LabAudioCallsPlugin.h
	LabAudioCallBoard.h
	LabAudioCallProtocol.h
	LabAudioCalls.cpp
	LabAudioCalls.h
	labaudiocalls.qrc
	)
