IF (NOT THIS_IS_MIC)
	INCLUDE_DIRECTORIES(${OSPRAY_DIR}/ospray)
	
	FIND_PACKAGE(LibXml2)
	
	IF (NOT LIBXML2_FOUND)
		MESSAGE("could not find lilbxml!")
	ENDIF()

	INCLUDE_DIRECTORIES(${OSPRAY_DIR})
	INCLUDE_DIRECTORIES(${LIBXML2_INCLUDE_DIR})
	ADD_DEFINITIONS(${LIBXML2_DEFINITIONS})


	#ADD_EXECUTABLE(its2obj
	#	its2obj.cpp
	#)

	ADD_LIBRARY(ospray_minisg 
		miniSG.cpp
		importer.cpp
		importOBJ.cpp
		importSTL.cpp
		importMSG.cpp
		importTRI.cpp
		importRIVL.cpp
		)
	TARGET_LINK_LIBRARIES(ospray_minisg ${LIBXML2_LIBRARIES})

ENDIF(NOT THIS_IS_MIC)