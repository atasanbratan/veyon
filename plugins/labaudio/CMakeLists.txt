include(BuildVeyonPlugin)

find_package(Qt${QT_MAJOR_VERSION} COMPONENTS Multimedia REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

build_veyon_plugin(labaudio
	NAME LabAudio
	SOURCES
	LabAudioFeaturePlugin.cpp
	LabAudioFeaturePlugin.h
	LabAudioHub.cpp
	LabAudioHub.h
	LabAudioClient.cpp
	LabAudioClient.h
	LabAudioCrypto.cpp
	LabAudioCrypto.h
	LabAudioEngine.cpp
	LabAudioEngine.h
	LabAudioGroupWork.cpp
	LabAudioGroupWork.h
	LabAudioProtocol.h
	LabAudioRecorder.cpp
	LabAudioRecorder.h
	labaudio.qrc
	)

target_link_libraries(labaudio PRIVATE Qt${QT_MAJOR_VERSION}::Multimedia PkgConfig::OPUS OpenSSL::Crypto)
