# Packaging
set(CPACK_PACKAGE_NAME "LemonLime")
set(CPACK_PACKAGE_VENDOR "Project LemonLime Developers")
set(CPACK_PACKAGE_DESCRIPTION "A tiny judging environment for OI contest based on Lemon + LemonPlus.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Project-LemonLime/Project_LemonLime")
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/assets/lemon-lime.ico")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSES")

SET(COMMON_IGNORE_FILES "/CMakeFiles/" "_CPack_Packages/" "/Testing/"
    ".cmake$"   ".directory$" "CMakeCache.txt"
    "/.svn/"  "/CVS/" "~$" ".swp$" ".log$" ".gz$"
    "/src/config.h$")

SET(CPACK_PACKAGE_IGNORE_FILES ${PRJ_COMMON_IGNORE_FILES} )

# Must Here
include(CPack)
